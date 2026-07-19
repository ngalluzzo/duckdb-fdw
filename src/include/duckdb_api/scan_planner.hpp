#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

namespace duckdb_api {

// Relational Semantics' construction facade. It deterministically selects and
// plans exactly the relation named by request, consumes only immutable
// Connector and Query team APIs, and performs no DuckDB callback, secret
// lookup, network/filesystem/environment access, runtime construction, or
// other I/O. Runtime consumers depend only on scan_plan.hpp.
ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

} // namespace duckdb_api
