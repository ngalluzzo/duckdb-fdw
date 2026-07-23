#pragma once

#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <cstdint>
#include <vector>

namespace duckdb {
namespace duckdb_api_query_internal {

// Query Experience's immutable DuckDB write contract for one planned column.
// It copies only the scalar kind and nullability that the adapter must enforce;
// response paths and protocol decoding remain behind Runtime's provider API.
struct PlannedValueColumn {
	PlannedValueColumn(duckdb_api::ValueKind kind, bool nullable);
	PlannedValueColumn(duckdb_api::OutputValueType type, bool nullable);

	duckdb_api::OutputValueType type;
	bool nullable;
};

LogicalType PlannedLogicalType(const duckdb_api::PlannedColumn &column);
std::vector<PlannedValueColumn> PlannedValueColumns(const duckdb_api::ScanPlan &plan);

// Validates a complete Runtime batch before changing the output chunk, then
// writes strict typed values. Invalid values become typed DuckDB NULL entries;
// required nulls, kind/arity drift, empty successful batches, and widened
// batches fail closed. The caller owns cancellation checks and publication.
void WriteTypedBatch(DataChunk &output, const duckdb_api::TypedBatch &batch,
                     const std::vector<PlannedValueColumn> &expected_columns, std::uint64_t max_batch_rows);
void WriteTypedBatch(DataChunk &output, const duckdb_api::TypedBatch &batch,
                     const std::vector<PlannedValueColumn> &expected_columns, std::uint64_t max_batch_rows,
                     duckdb_api::ExecutionControl &control);

} // namespace duckdb_api_query_internal
} // namespace duckdb
