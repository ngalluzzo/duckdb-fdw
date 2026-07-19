#pragma once

#include "duckdb_api/connector.hpp"
#include "query/support/query_runtime_scenarios.hpp"

#include <memory>
#include <string>

namespace duckdb {
class Connection;
class DuckDB;
} // namespace duckdb

namespace duckdb_api_test {

std::string QueryError(duckdb::Connection &connection, const std::string &sql);
std::shared_ptr<QueryLifecycleProbe>
RegisterQueryAdapter(duckdb::DuckDB &database, duckdb_api::CompiledConnector connector, QueryRuntimeScenario scenario);
std::shared_ptr<QueryLifecycleProbe> RegisterNativeAdapter(duckdb::DuckDB &database, QueryRuntimeScenario scenario);

} // namespace duckdb_api_test
