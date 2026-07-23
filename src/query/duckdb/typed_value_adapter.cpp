#include "typed_value_adapter.hpp"

#include "duckdb/common/types/vector.hpp"

#include <limits>
#include <stdexcept>

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

LogicalType LogicalTypeForKind(duckdb_api::ValueKind kind) {
	switch (kind) {
	case duckdb_api::ValueKind::BIGINT:
		return LogicalType::BIGINT;
	case duckdb_api::ValueKind::VARCHAR:
		return LogicalType::VARCHAR;
	case duckdb_api::ValueKind::BOOLEAN:
		return LogicalType::BOOLEAN;
	case duckdb_api::ValueKind::DOUBLE:
		return LogicalType::DOUBLE;
	}
	throw std::logic_error("runtime value contract contains an unknown scalar kind");
}

// Query never re-parses PlannedColumn::logical_type itself; it maps
// Semantics' own closed ScalarKind() derivation onto the Runtime/Query
// value-kind vocabulary.
duckdb_api::ValueKind ValueKindForScalarKind(duckdb_api::PlannedColumnScalarKind kind) {
	switch (kind) {
	case duckdb_api::PlannedColumnScalarKind::BIGINT:
		return duckdb_api::ValueKind::BIGINT;
	case duckdb_api::PlannedColumnScalarKind::VARCHAR:
		return duckdb_api::ValueKind::VARCHAR;
	case duckdb_api::PlannedColumnScalarKind::BOOLEAN:
		return duckdb_api::ValueKind::BOOLEAN;
	case duckdb_api::PlannedColumnScalarKind::DOUBLE:
		return duckdb_api::ValueKind::DOUBLE;
	}
	throw std::logic_error("planned column contains an unknown scalar kind");
}

Value DuckdbScalarValue(const duckdb_api::TypedValue &value, const PlannedValueColumn &expected) {
	if (!value.valid) {
		return Value(LogicalTypeForKind(expected.type.element_kind));
	}
	switch (expected.type.element_kind) {
	case duckdb_api::ValueKind::BIGINT:
		return Value::BIGINT(value.bigint_value);
	case duckdb_api::ValueKind::VARCHAR:
		return Value(value.varchar_value);
	case duckdb_api::ValueKind::BOOLEAN:
		return Value::BOOLEAN(value.boolean_value);
	case duckdb_api::ValueKind::DOUBLE:
		return Value::DOUBLE(value.double_value);
	}
	throw std::logic_error("runtime value contract contains an unknown scalar kind");
}

Value DuckdbElementValue(const duckdb_api::TypedScalarValue &value, duckdb_api::ValueKind kind) {
	if (!value.valid) {
		return Value(LogicalTypeForKind(kind));
	}
	switch (kind) {
	case duckdb_api::ValueKind::BIGINT:
		return Value::BIGINT(value.bigint_value);
	case duckdb_api::ValueKind::VARCHAR:
		return Value(value.varchar_value);
	case duckdb_api::ValueKind::BOOLEAN:
		return Value::BOOLEAN(value.boolean_value);
	case duckdb_api::ValueKind::DOUBLE:
		return Value::DOUBLE(value.double_value);
	}
	throw std::logic_error("runtime value contract contains an unknown array element kind");
}

void ValidateTypedBatch(const duckdb_api::TypedBatch &batch, const std::vector<PlannedValueColumn> &expected_columns,
                        std::uint64_t max_batch_rows, duckdb_api::ExecutionControl &control) {
	if (batch.rows.empty()) {
		throw std::logic_error("batch stream returned an empty successful batch");
	}
	if (max_batch_rows == 0 || batch.rows.size() > max_batch_rows || batch.rows.size() > STANDARD_VECTOR_SIZE) {
		throw std::logic_error("batch stream exceeded its planned row ceiling");
	}
	if (batch.column_types.size() != expected_columns.size()) {
		throw std::logic_error("batch stream returned the wrong column arity");
	}
	for (std::size_t column_index = 0; column_index < expected_columns.size(); column_index++) {
		if (batch.column_types[column_index] != expected_columns[column_index].type) {
			throw std::logic_error("batch stream returned a mismatched column type");
		}
	}
	if (!batch.IsSchemaAligned(control)) {
		throw std::logic_error("batch stream returned structurally misaligned values");
	}
	for (const auto &row : batch.rows) {
		if (row.values.size() != expected_columns.size()) {
			throw std::logic_error("batch stream returned the wrong row arity");
		}
		for (std::size_t column_index = 0; column_index < expected_columns.size(); column_index++) {
			const auto &value = row.values[column_index];
			const auto &expected = expected_columns[column_index];
			if (value.Type() != expected.type) {
				throw std::logic_error("batch stream returned a mismatched value type");
			}
			if (!value.valid && !expected.nullable) {
				throw std::logic_error("batch stream returned NULL for a required column");
			}
		}
	}
}

std::vector<idx_t> ChildCounts(const duckdb_api::TypedBatch &batch,
                               const std::vector<PlannedValueColumn> &expected_columns) {
	std::vector<idx_t> result(expected_columns.size(), 0);
	for (std::size_t column_index = 0; column_index < expected_columns.size(); column_index++) {
		if (expected_columns[column_index].type.shape != duckdb_api::ValueShape::ARRAY) {
			continue;
		}
		std::uint64_t count = 0;
		for (const auto &row : batch.rows) {
			const auto size = static_cast<std::uint64_t>(row.values[column_index].elements.size());
			if (size > std::numeric_limits<std::uint64_t>::max() - count) {
				throw std::logic_error("batch stream array cardinality overflowed");
			}
			count += size;
		}
		if (count > static_cast<std::uint64_t>(std::numeric_limits<idx_t>::max())) {
			throw std::logic_error("batch stream array cardinality exceeded DuckDB limits");
		}
		result[column_index] = static_cast<idx_t>(count);
	}
	return result;
}

void CheckCancellation(duckdb_api::ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
}

class NeverCancelled final : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

} // namespace

PlannedValueColumn::PlannedValueColumn(duckdb_api::ValueKind kind, bool nullable_p)
    : type(duckdb_api::OutputValueType::Scalar(kind)), nullable(nullable_p) {
}

PlannedValueColumn::PlannedValueColumn(duckdb_api::OutputValueType type_p, bool nullable_p)
    : type(type_p), nullable(nullable_p) {
}

LogicalType PlannedLogicalType(const duckdb_api::PlannedColumn &column) {
	const auto child = LogicalTypeForKind(ValueKindForScalarKind(column.ElementKind()));
	const auto expected_name = child.ToString() + (column.shape == duckdb_api::PlannedColumnShape::ARRAY ? "[]" : "");
	if (column.logical_type != expected_name ||
	    (column.shape == duckdb_api::PlannedColumnShape::SCALAR && column.element_nullable)) {
		throw std::logic_error("planned column contains a contradictory structural type");
	}
	return column.shape == duckdb_api::PlannedColumnShape::ARRAY ? LogicalType::LIST(child) : child;
}

std::vector<PlannedValueColumn> PlannedValueColumns(const duckdb_api::ScanPlan &plan) {
	std::vector<PlannedValueColumn> result;
	result.reserve(plan.OutputColumns().size());
	for (const auto &column : plan.OutputColumns()) {
		const auto kind = ValueKindForScalarKind(column.ElementKind());
		const auto type = column.shape == duckdb_api::PlannedColumnShape::ARRAY
		                      ? duckdb_api::OutputValueType::Array(kind, column.element_nullable)
		                      : duckdb_api::OutputValueType::Scalar(kind);
		result.push_back(PlannedValueColumn(type, column.nullable));
	}
	return result;
}

void WriteTypedBatch(DataChunk &output, const duckdb_api::TypedBatch &batch,
                     const std::vector<PlannedValueColumn> &expected_columns, std::uint64_t max_batch_rows) {
	NeverCancelled control;
	WriteTypedBatch(output, batch, expected_columns, max_batch_rows, control);
}

void WriteTypedBatch(DataChunk &output, const duckdb_api::TypedBatch &batch,
                     const std::vector<PlannedValueColumn> &expected_columns, std::uint64_t max_batch_rows,
                     duckdb_api::ExecutionControl &control) {
	ValidateTypedBatch(batch, expected_columns, max_batch_rows, control);
	if (output.ColumnCount() != expected_columns.size()) {
		throw std::logic_error("DuckDB output chunk does not match the planned column arity");
	}
	const auto child_counts = ChildCounts(batch, expected_columns);
	CheckCancellation(control);
	for (idx_t column_index = 0; column_index < expected_columns.size(); column_index++) {
		if (expected_columns[column_index].type.shape == duckdb_api::ValueShape::ARRAY) {
			ListVector::Reserve(output.data[column_index], child_counts[column_index]);
		}
	}
	for (idx_t row_index = 0; row_index < batch.rows.size(); row_index++) {
		CheckCancellation(control);
		for (idx_t column_index = 0; column_index < expected_columns.size(); column_index++) {
			const auto &expected = expected_columns[column_index];
			const auto &value = batch.rows[row_index].values[column_index];
			if (expected.type.shape == duckdb_api::ValueShape::SCALAR) {
				output.SetValue(column_index, row_index, DuckdbScalarValue(value, expected));
			}
		}
	}
	for (idx_t column_index = 0; column_index < expected_columns.size(); column_index++) {
		const auto &expected = expected_columns[column_index];
		if (expected.type.shape != duckdb_api::ValueShape::ARRAY) {
			continue;
		}
		auto &list = output.data[column_index];
		auto *entries = ListVector::GetData(list);
		auto &child = ListVector::GetEntry(list);
		idx_t child_index = 0;
		for (idx_t row_index = 0; row_index < batch.rows.size(); row_index++) {
			CheckCancellation(control);
			const auto &value = batch.rows[row_index].values[column_index];
			entries[row_index].offset = child_index;
			entries[row_index].length = static_cast<idx_t>(value.elements.size());
			FlatVector::SetNull(list, row_index, !value.valid);
			for (const auto &element : value.elements) {
				CheckCancellation(control);
				child.SetValue(child_index++, DuckdbElementValue(element, expected.type.element_kind));
			}
		}
		ListVector::SetListSize(list, child_index);
	}
	CheckCancellation(control);
	output.SetCardinality(batch.rows.size());
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
