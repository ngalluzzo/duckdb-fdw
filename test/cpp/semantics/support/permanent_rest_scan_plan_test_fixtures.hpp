#pragma once

#include "duckdb_api/scan_plan.hpp"

namespace duckdb_api_test {

// Deterministic positive package-REST plan built through the production
// Connector -> Query request -> Semantics planner path. The factory performs
// no I/O and returns an anonymous immutable plan with fixed, relation-input,
// page-size, and page-number bindings plus nested structural response paths.
// Runtime consumers use this bounded service instead of constructing plan
// internals or linking Connector-private fixture access.
duckdb_api::ScanPlan BuildValidPermanentRestScanPlanFixture();

} // namespace duckdb_api_test
