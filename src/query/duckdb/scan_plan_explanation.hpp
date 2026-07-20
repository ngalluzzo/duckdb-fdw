#pragma once

#include "duckdb/common/insertion_order_preserving_map.hpp"

namespace duckdb_api {
class ScanPlan;
struct ScanRequest;
} // namespace duckdb_api

namespace duckdb {
namespace duckdb_api_query_internal {

// Renders Query's typed, non-authoritative explanation of the immutable
// request/plan handoff. No field is parsed to recover planning or execution
// authority, and the result contains no credential value or remote response.
InsertionOrderPreservingMap<string> ExplainSelectedScan(const duckdb_api::ScanRequest &request,
                                                        const duckdb_api::ScanPlan &plan);

} // namespace duckdb_api_query_internal
} // namespace duckdb
