#include "typed_value_adapter.hpp"

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

Value DuckdbValue(const duckdb_api::TypedValue &value, const PlannedValueColumn &expected) {
	if (!value.valid) {
		return Value(LogicalTypeForKind(expected.kind));
	}
	switch (expected.kind) {
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

void ValidateTypedBatch(const duckdb_api::TypedBatch &batch, const std::vector<PlannedValueColumn> &expected_columns,
                        std::uint64_t max_batch_rows) {
	if (batch.rows.empty()) {
		throw std::logic_error("batch stream returned an empty successful batch");
	}
	if (max_batch_rows == 0 || batch.rows.size() > max_batch_rows || batch.rows.size() > STANDARD_VECTOR_SIZE) {
		throw std::logic_error("batch stream exceeded its planned row ceiling");
	}
	if (batch.column_kinds.size() != expected_columns.size()) {
		throw std::logic_error("batch stream returned the wrong column arity");
	}
	for (std::size_t column_index = 0; column_index < expected_columns.size(); column_index++) {
		if (batch.column_kinds[column_index] != expected_columns[column_index].kind) {
			throw std::logic_error("batch stream returned a mismatched column kind");
		}
	}
	for (const auto &row : batch.rows) {
		if (row.values.size() != expected_columns.size()) {
			throw std::logic_error("batch stream returned the wrong row arity");
		}
		for (std::size_t column_index = 0; column_index < expected_columns.size(); column_index++) {
			const auto &value = row.values[column_index];
			const auto &expected = expected_columns[column_index];
			if (value.kind != expected.kind) {
				throw std::logic_error("batch stream returned a mismatched value kind");
			}
			if (!value.valid && !expected.nullable) {
				throw std::logic_error("batch stream returned NULL for a required column");
			}
		}
	}
}

} // namespace

LogicalType PlannedLogicalType(const duckdb_api::PlannedColumn &column) {
	return LogicalTypeForKind(ValueKindForScalarKind(column.ScalarKind()));
}

std::vector<PlannedValueColumn> PlannedValueColumns(const duckdb_api::ScanPlan &plan) {
	std::vector<PlannedValueColumn> result;
	result.reserve(plan.OutputColumns().size());
	for (const auto &column : plan.OutputColumns()) {
		result.push_back({ValueKindForScalarKind(column.ScalarKind()), column.nullable});
	}
	return result;
}

void WriteTypedBatch(DataChunk &output, const duckdb_api::TypedBatch &batch,
                     const std::vector<PlannedValueColumn> &expected_columns, std::uint64_t max_batch_rows) {
	ValidateTypedBatch(batch, expected_columns, max_batch_rows);
	if (output.ColumnCount() != expected_columns.size()) {
		throw std::logic_error("DuckDB output chunk does not match the planned column arity");
	}
	for (idx_t row_index = 0; row_index < batch.rows.size(); row_index++) {
		for (idx_t column_index = 0; column_index < expected_columns.size(); column_index++) {
			output.SetValue(column_index, row_index,
			                DuckdbValue(batch.rows[row_index].values[column_index], expected_columns[column_index]));
		}
	}
	output.SetCardinality(batch.rows.size());
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
