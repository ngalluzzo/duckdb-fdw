#include "support/duckdb_adapter_auth_test_support.hpp"

#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "support/connector_catalog_test_fixtures.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <string>

namespace duckdb_api_test {
namespace {

void RequireNoRuntimeEntry(const QueryLifecycleProbe &probe, const std::string &context) {
	Require(probe.legacy_open_calls.load(std::memory_order_relaxed) == 0 &&
	            probe.authorization_open_calls.load(std::memory_order_relaxed) == 0 &&
	            probe.streams_opened.load(std::memory_order_relaxed) == 0,
	        context + " entered runtime during bind");
}

void RequireBindFailure(duckdb::Connection &connection, const std::string &sql, const std::string &expected,
                        const std::shared_ptr<QueryLifecycleProbe> &probe) {
	const auto error = QueryError(connection, sql);
	Require(error.find(expected) != std::string::npos, "bind diagnostic mismatch: " + error);
	RequireNoRuntimeEntry(*probe, sql);
}

void RequireSchema(duckdb::Connection &connection, const std::string &sql, const char *const *names,
                   const char *const *types, duckdb::idx_t count) {
	auto result = connection.Query("DESCRIBE " + sql);
	if (result->HasError()) {
		throw std::runtime_error("schema bind failed: " + result->GetError());
	}
	Require(result->RowCount() == count, "schema bind returned the wrong column count");
	for (duckdb::idx_t index = 0; index < count; index++) {
		Require(result->GetValue(0, index).ToString() == names[index], "schema bind returned the wrong column name");
		Require(result->GetValue(1, index).ToString() == types[index], "schema bind returned the wrong column type");
	}
}

void TestRegisteredParameterInventory() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::SUCCESS);
	duckdb::Connection connection(database);
	auto result = connection.Query("SELECT parameters, parameter_types FROM duckdb_functions() "
	                               "WHERE function_name = 'duckdb_api_scan'");
	if (result->HasError()) {
		throw std::runtime_error("duckdb_api_scan parameter inventory failed: " + result->GetError());
	}
	Require(result->RowCount() == 1, "duckdb_api_scan did not have one registered overload");
	const auto parameters = result->GetValue(0, 0).ToString();
	const auto parameter_types = result->GetValue(1, 0).ToString();
	// DuckDB publishes named parameters in its pinned map order. Assert the
	// complete registered surface so an accidental positional or extra argument
	// is visible without pretending registration order is SQL call order.
	Require(parameters == "[secret, relation, connector]",
	        "duckdb_api_scan parameter names are not the exact public surface: " + parameters);
	Require(parameter_types == "[VARCHAR, VARCHAR, VARCHAR]",
	        "duckdb_api_scan parameter types are not the exact public surface: " + parameter_types);
	RequireNoRuntimeEntry(*probe, "duckdb_functions inventory");
}

void TestSecretPresenceRulesAndExactSelection() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::SUCCESS);
	duckdb::Connection connection(database);

	auto anonymous = connection.Query(
	    "DESCRIBE SELECT * FROM duckdb_api_scan(connector := 'github', relation := 'duckdb_login_search_page')");
	if (anonymous->HasError()) {
		throw std::runtime_error("anonymous relation without secret did not bind: " + anonymous->GetError());
	}
	RequireNoRuntimeEntry(*probe, "anonymous DESCRIBE");

	RequireBindFailure(connection,
	                   "SELECT * FROM duckdb_api_scan(connector := 'github', relation := "
	                   "'duckdb_login_search_page', secret := 'unused')",
	                   "named argument secret is not accepted", probe);
	RequireBindFailure(connection,
	                   "SELECT * FROM duckdb_api_scan(connector := 'github', relation := 'authenticated_user')",
	                   "required named argument secret is missing", probe);
	RequireBindFailure(connection,
	                   "SELECT * FROM duckdb_api_scan(connector := 'github', relation := 'authenticated_user', "
	                   "secret := NULL)",
	                   "named argument secret must not be NULL or empty", probe);
	RequireBindFailure(connection,
	                   "SELECT * FROM duckdb_api_scan(connector := 'github', relation := 'authenticated_user', "
	                   "secret := '')",
	                   "named argument secret must not be NULL or empty", probe);
	RequireBindFailure(connection,
	                   "SELECT * FROM duckdb_api_scan(connector := 'GitHub', relation := 'authenticated_user', "
	                   "secret := 'missing')",
	                   "unknown connector identifier", probe);
	RequireBindFailure(connection,
	                   "SELECT * FROM duckdb_api_scan(connector := 'github', relation := 'Authenticated_User', "
	                   "secret := 'missing')",
	                   "unknown relation identifier", probe);
}

void TestAuthenticatedBindStaysOffline() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::SUCCESS);
	duckdb::Connection connection(database);
	const std::string relation = "duckdb_api_scan(connector := 'github', relation := 'authenticated_user', "
	                             "secret := 'not_created')";

	auto describe = connection.Query("DESCRIBE SELECT * FROM " + relation);
	if (describe->HasError()) {
		throw std::runtime_error("authenticated DESCRIBE resolved a secret: " + describe->GetError());
	}
	auto explain = connection.Query("EXPLAIN SELECT * FROM " + relation);
	if (explain->HasError()) {
		throw std::runtime_error("authenticated EXPLAIN resolved a secret: " + explain->GetError());
	}
	auto prepare = connection.Query("PREPARE authenticated_bind AS SELECT * FROM " + relation);
	if (prepare->HasError()) {
		throw std::runtime_error("authenticated PREPARE resolved a secret: " + prepare->GetError());
	}
	RequireNoRuntimeEntry(*probe, "authenticated bind-only operations");
}

void TestBothRelationSchemasComeFromPlans() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterFixtureAdapter(database, QueryRuntimeScenario::SUCCESS);
	duckdb::Connection connection(database);
	const char *anonymous_names[] = {"public_id", "public_label"};
	const char *anonymous_types[] = {"BIGINT", "VARCHAR"};
	const char *authenticated_names[] = {"profile_login", "profile_verified", "profile_generation"};
	const char *authenticated_types[] = {"VARCHAR", "BOOLEAN", "BIGINT"};

	RequireSchema(connection,
	              "SELECT * FROM duckdb_api_scan(connector := 'fixture_distinct_catalog', relation := '" +
	                  std::string(DISTINCT_SCHEMA_ANONYMOUS_RELATION) + "')",
	              anonymous_names, anonymous_types, 2);
	RequireSchema(connection,
	              "SELECT * FROM duckdb_api_scan(connector := 'fixture_distinct_catalog', relation := '" +
	                  std::string(DISTINCT_SCHEMA_AUTHENTICATED_RELATION) + "', secret := 'not_created')",
	              authenticated_names, authenticated_types, 3);
	RequireNoRuntimeEntry(*probe, "fixture relation schema binds");
}

void TestAnonymousExecutionUsesClosedAuthorizationEntry() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::SUCCESS);
	duckdb::Connection connection(database);
	auto result = connection.Query(ACCEPTED_LIVE_SQL);
	if (result->HasError()) {
		throw std::runtime_error("anonymous execution failed: " + result->GetError());
	}
	Require(result->RowCount() == 3, "anonymous execution returned the wrong row count");
	Require(probe->legacy_open_calls.load(std::memory_order_relaxed) == 0 &&
	            probe->authorization_open_calls.load(std::memory_order_relaxed) == 1 &&
	            probe->anonymous_authorizations.load(std::memory_order_relaxed) == 1 &&
	            probe->github_bearer_authorizations.load(std::memory_order_relaxed) == 0,
	        "anonymous execution bypassed or misclassified the closed authorization entry point");
}

} // namespace

void RunDuckdbAdapterAuthBindTests() {
	TestRegisteredParameterInventory();
	TestSecretPresenceRulesAndExactSelection();
	TestAuthenticatedBindStaysOffline();
	TestBothRelationSchemasComeFromPlans();
	TestAnonymousExecutionUsesClosedAuthorizationEntry();
}

} // namespace duckdb_api_test
