#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <cstddef>

namespace duckdb_api_test {

// Counts differing public GraphQL plan leaves. Connector-owned source prose is
// deliberately excluded: the fixture service owns the typed Semantics handoff
// and cannot depend on Connector construction merely to reproduce provenance.
std::size_t CountGraphqlPlanDifferences(const duckdb_api::ScanPlan &left, const duckdb_api::ScanPlan &right);

} // namespace duckdb_api_test
