#pragma once

#include "duckdb/common/insertion_order_preserving_map.hpp"

namespace duckdb_api {
class ScanPlan;
struct ScanRequest;
enum class PlannedPredicate;
} // namespace duckdb_api

namespace duckdb {
namespace duckdb_api_query_internal {

// Maps the predicate states Query currently understands to stable explanation
// labels. Unknown values fail closed so a provider can extend its enum without
// Query silently misrepresenting the new state.
const char *PredicateNameForExplanation(duckdb_api::PlannedPredicate predicate);

// Renders Query's typed, non-authoritative explanation of the immutable
// request/plan handoff. No field is parsed to recover planning or execution
// authority. It renders provider-classified facts without reproducing their
// classification rules and contains no document bytes/identity, variable or
// cursor identifier/value, credential value, or remote response.
InsertionOrderPreservingMap<string> ExplainSelectedScan(const duckdb_api::ScanRequest &request,
                                                        const duckdb_api::ScanPlan &plan);

} // namespace duckdb_api_query_internal
} // namespace duckdb
