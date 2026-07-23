#include "duckdb_api/execution.hpp"

#include <utility>

namespace duckdb_api {

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

TypedValue::TypedValue()
    : kind(ValueKind::VARCHAR), valid(false), bigint_value(0), varchar_value(), boolean_value(false),
      double_value(0.0) {
}

TypedValue::TypedValue(ValueKind kind_p, int64_t bigint_value_p, std::string varchar_value_p, bool boolean_value_p)
    : kind(kind_p), valid(true), bigint_value(bigint_value_p), varchar_value(std::move(varchar_value_p)),
      boolean_value(boolean_value_p), double_value(0.0) {
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

void TypedBatch::Clear() {
	column_kinds.clear();
	rows.clear();
}

bool TypedBatch::IsSchemaAligned() const noexcept {
	for (std::size_t row_index = 0; row_index < rows.size(); row_index++) {
		const auto &values = rows[row_index].values;
		if (values.size() != column_kinds.size()) {
			return false;
		}
		for (std::size_t column_index = 0; column_index < values.size(); column_index++) {
			if (values[column_index].kind != column_kinds[column_index]) {
				return false;
			}
		}
	}
	return true;
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
