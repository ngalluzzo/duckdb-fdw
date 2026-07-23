#include "query/support/duckdb_adapter_test_support.hpp"
#include "query/support/isolated_credential_root.hpp"

#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api_extension.hpp"
#include "support/require.hpp"

#include <utility>

namespace duckdb_api_test {

std::string QueryError(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "query unexpectedly succeeded: " + sql);
	return result->GetError();
}

std::shared_ptr<QueryLifecycleProbe>
RegisterQueryAdapter(duckdb::DuckDB &database, duckdb_api::CompiledConnector connector, QueryRuntimeScenario scenario) {
	auto probe = std::shared_ptr<QueryLifecycleProbe>(new QueryLifecycleProbe());
	ConfigureIsolatedCredentialRoot(database);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_adapter_test");
	duckdb::RegisterDuckdbApi(loader, std::move(connector), BuildQueryScenarioExecutor(scenario, probe));
	return probe;
}

std::shared_ptr<QueryLifecycleProbe> RegisterNativeAdapter(duckdb::DuckDB &database, QueryRuntimeScenario scenario) {
	return RegisterQueryAdapter(database, duckdb_api::BuildNativeGithubConnector(), scenario);
}

} // namespace duckdb_api_test
