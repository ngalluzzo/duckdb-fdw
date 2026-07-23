#include "duckdb_api/execution.hpp"

#include <cmath>
#include <utility>

namespace duckdb_api {

namespace {

TypedScalarValue Scalar(ValueKind kind, bool valid, int64_t bigint_value, std::string varchar_value, bool boolean_value,
                        double double_value) {
	TypedScalarValue result;
	result.kind = kind;
	result.valid = valid;
	result.bigint_value = bigint_value;
	result.varchar_value = std::move(varchar_value);
	result.boolean_value = boolean_value;
	result.double_value = double_value == 0.0 ? 0.0 : double_value;
	return result;
}

bool ScalarPayloadAligned(ValueKind kind, bool valid, int64_t bigint_value, const std::string &varchar_value,
                          bool boolean_value, double double_value) noexcept {
	const bool canonical_zero = double_value != 0.0 || !std::signbit(double_value);
	if (!valid) {
		return bigint_value == 0 && varchar_value.empty() && !boolean_value && double_value == 0.0 && canonical_zero;
	}
	switch (kind) {
	case ValueKind::BIGINT:
		return varchar_value.empty() && !boolean_value && double_value == 0.0 && canonical_zero;
	case ValueKind::VARCHAR:
		return bigint_value == 0 && !boolean_value && double_value == 0.0 && canonical_zero;
	case ValueKind::BOOLEAN:
		return bigint_value == 0 && varchar_value.empty() && double_value == 0.0 && canonical_zero;
	case ValueKind::DOUBLE:
		return bigint_value == 0 && varchar_value.empty() && !boolean_value && std::isfinite(double_value) &&
		       canonical_zero;
	}
	return false;
}

void CheckAlignmentCancellation(ExecutionControl *control) {
	if (control != nullptr && control->IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
}

bool IsSchemaAligned(const TypedBatch &batch, ExecutionControl *control) {
	for (const auto &type : batch.column_types) {
		CheckAlignmentCancellation(control);
		if (type.shape == ValueShape::SCALAR && type.element_nullable) {
			return false;
		}
		if (type.shape != ValueShape::SCALAR && type.shape != ValueShape::ARRAY) {
			return false;
		}
	}
	for (std::size_t row_index = 0; row_index < batch.rows.size(); row_index++) {
		CheckAlignmentCancellation(control);
		const auto &values = batch.rows[row_index].values;
		if (values.size() != batch.column_types.size()) {
			return false;
		}
		for (std::size_t column_index = 0; column_index < values.size(); column_index++) {
			CheckAlignmentCancellation(control);
			const auto &value = values[column_index];
			const auto &type = batch.column_types[column_index];
			if (value.Type() != type) {
				return false;
			}
			if (type.shape == ValueShape::SCALAR) {
				if (!value.elements.empty() ||
				    !ScalarPayloadAligned(value.kind, value.valid, value.bigint_value, value.varchar_value,
				                          value.boolean_value, value.double_value)) {
					return false;
				}
				continue;
			}
			if (value.bigint_value != 0 || !value.varchar_value.empty() || value.boolean_value ||
			    value.double_value != 0.0 || std::signbit(value.double_value) ||
			    (!value.valid && !value.elements.empty())) {
				return false;
			}
			for (const auto &element : value.elements) {
				CheckAlignmentCancellation(control);
				if (element.kind != type.element_kind || (!element.valid && !type.element_nullable) ||
				    !ScalarPayloadAligned(element.kind, element.valid, element.bigint_value, element.varchar_value,
				                          element.boolean_value, element.double_value)) {
					return false;
				}
			}
		}
	}
	return true;
}

} // namespace

ExecutionError::ExecutionError(ErrorStage stage_p, std::string field_p, std::string safe_message_p)
    : stage(stage_p), field(std::move(field_p)), safe_message(std::move(safe_message_p)) {
}

const char *ExecutionError::what() const noexcept {
	return safe_message.c_str();
}

ErrorStage ExecutionError::Stage() const noexcept {
	return stage;
}

const std::string &ExecutionError::Field() const {
	return field;
}

const std::string &ExecutionError::SafeMessage() const {
	return safe_message;
}

OutputValueType::OutputValueType()
    : shape(ValueShape::SCALAR), element_kind(ValueKind::VARCHAR), element_nullable(false) {
}

OutputValueType::OutputValueType(ValueKind kind)
    : shape(ValueShape::SCALAR), element_kind(kind), element_nullable(false) {
}

OutputValueType::OutputValueType(ValueShape shape_p, ValueKind element_kind_p, bool element_nullable_p)
    : shape(shape_p), element_kind(element_kind_p), element_nullable(element_nullable_p) {
}

OutputValueType OutputValueType::Scalar(ValueKind kind) {
	return {ValueShape::SCALAR, kind, false};
}

OutputValueType OutputValueType::Array(ValueKind element_kind, bool element_nullable) {
	return {ValueShape::ARRAY, element_kind, element_nullable};
}

bool OutputValueType::operator==(const OutputValueType &other) const noexcept {
	return shape == other.shape && element_kind == other.element_kind && element_nullable == other.element_nullable;
}

bool OutputValueType::operator!=(const OutputValueType &other) const noexcept {
	return !(*this == other);
}

TypedScalarValue TypedScalarValue::BigInt(int64_t value) {
	return Scalar(ValueKind::BIGINT, true, value, "", false, 0.0);
}

TypedScalarValue TypedScalarValue::Varchar(std::string value) {
	return Scalar(ValueKind::VARCHAR, true, 0, std::move(value), false, 0.0);
}

TypedScalarValue TypedScalarValue::Boolean(bool value) {
	return Scalar(ValueKind::BOOLEAN, true, 0, "", value, 0.0);
}

TypedScalarValue TypedScalarValue::Double(double value) {
	return Scalar(ValueKind::DOUBLE, true, 0, "", false, value);
}

TypedScalarValue TypedScalarValue::Null(ValueKind kind) {
	return Scalar(kind, false, 0, "", false, 0.0);
}

TypedValue::TypedValue()
    : kind(ValueKind::VARCHAR), shape(ValueShape::SCALAR), element_nullable(false), valid(false), bigint_value(0),
      varchar_value(), boolean_value(false), double_value(0.0), elements() {
}

TypedValue::TypedValue(ValueKind kind_p, int64_t bigint_value_p, std::string varchar_value_p, bool boolean_value_p)
    : kind(kind_p), shape(ValueShape::SCALAR), element_nullable(false), valid(true), bigint_value(bigint_value_p),
      varchar_value(std::move(varchar_value_p)), boolean_value(boolean_value_p), double_value(0.0), elements() {
}

TypedValue TypedValue::BigInt(int64_t value) {
	TypedValue result;
	result.kind = ValueKind::BIGINT;
	result.valid = true;
	result.bigint_value = value;
	result.boolean_value = false;
	return result;
}

TypedValue TypedValue::Varchar(std::string value) {
	TypedValue result;
	result.kind = ValueKind::VARCHAR;
	result.valid = true;
	result.bigint_value = 0;
	result.varchar_value = std::move(value);
	result.boolean_value = false;
	return result;
}

TypedValue TypedValue::Boolean(bool value) {
	TypedValue result;
	result.kind = ValueKind::BOOLEAN;
	result.valid = true;
	result.bigint_value = 0;
	result.boolean_value = value;
	return result;
}

TypedValue TypedValue::Double(double value) {
	TypedValue result;
	result.kind = ValueKind::DOUBLE;
	result.valid = true;
	result.bigint_value = 0;
	result.boolean_value = false;
	// RFC 0020: -0.0 is normalized to 0.0 so every consumer sees one
	// canonical zero.
	result.double_value = value == 0.0 ? 0.0 : value;
	return result;
}

TypedValue TypedValue::Null(ValueKind kind) {
	TypedValue result;
	result.kind = kind;
	result.valid = false;
	result.bigint_value = 0;
	result.boolean_value = false;
	return result;
}

TypedValue TypedValue::Null(OutputValueType type) {
	TypedValue result;
	result.kind = type.element_kind;
	result.shape = type.shape;
	result.element_nullable = type.element_nullable;
	result.valid = false;
	return result;
}

TypedValue TypedValue::Array(ValueKind element_kind, bool element_nullable, std::vector<TypedScalarValue> elements_p) {
	TypedValue result;
	result.kind = element_kind;
	result.shape = ValueShape::ARRAY;
	result.element_nullable = element_nullable;
	result.valid = true;
	result.elements = std::move(elements_p);
	return result;
}

OutputValueType TypedValue::Type() const noexcept {
	return {shape, kind, element_nullable};
}

void TypedBatch::Clear() {
	column_types.clear();
	rows.clear();
}

bool TypedBatch::IsSchemaAligned() const noexcept {
	try {
		return duckdb_api::IsSchemaAligned(*this, nullptr);
	} catch (...) {
		return false;
	}
}

bool TypedBatch::IsSchemaAligned(ExecutionControl &control) const {
	return duckdb_api::IsSchemaAligned(*this, &control);
}

const char *ExecutionCancelled::what() const noexcept {
	return "execution cancelled";
}

ExecutionControl::~ExecutionControl() noexcept {
}

BatchStream::~BatchStream() noexcept {
}

ScanExecutor::~ScanExecutor() noexcept {
}

} // namespace duckdb_api
