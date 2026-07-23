#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "duckdb_api/scan_request.hpp"
#include "query/duckdb/scan_plan_explanation.hpp"
#include "query/support/duckdb_adapter_test_support.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::QueryError;
using duckdb_api_test::QueryRuntimeScenario;
using duckdb_api_test::RegisterNativeAdapter;
using duckdb_api_test::Require;

const char GRAPHQL_RELATION[] = "viewer_repository_metrics";
const char GRAPHQL_SCAN[] =
    "duckdb_api_scan(connector := 'github', relation := 'viewer_repository_metrics', secret := 'graphql_test')";

void CreateTemporarySecret(duckdb::Connection &connection, const std::string &token) {
	auto result = connection.Query("CREATE TEMPORARY SECRET graphql_test "
	                               "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                               token + "')");
	if (result->HasError()) {
		throw std::runtime_error("temporary GraphQL test secret creation failed: " + result->GetError());
	}
}

std::string Explain(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query("EXPLAIN " + sql);
	if (result->HasError()) {
		throw std::runtime_error("GraphQL EXPLAIN failed: " + result->GetError());
	}
	std::string explanation;
	for (duckdb::idx_t row = 0; row < result->RowCount(); row++) {
		for (duckdb::idx_t column = 0; column < result->ColumnCount(); column++) {
			explanation += result->GetValue(column, row).ToString();
			explanation.push_back('\n');
		}
	}
	return explanation;
}

void TestPublicProviderPlanAndProtocolNeutralRequest() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto request = duckdb_api::BuildConservativeScanRequest(
	    connector, GRAPHQL_RELATION, duckdb_api::LogicalSecretReference::Named("graphql_test"));
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	const std::vector<std::string> expected_names = {"id",      "full_name", "owner_login", "stars", "primary_language",
	                                                 "private", "archived",  "updated_at"};
	const std::vector<std::string> expected_types = {"VARCHAR", "VARCHAR", "VARCHAR", "BIGINT",
	                                                 "VARCHAR", "BOOLEAN", "BOOLEAN", "VARCHAR"};

	Require(request.projected_columns == expected_names && request.explicit_inputs.empty() &&
	            request.requested_predicate == duckdb_api::RequestedPredicate::Unrestricted() &&
	            request.orderings.empty() && !request.has_limit && !request.has_offset,
	        "Query did not publish the conservative protocol-neutral request");
	Require(plan.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL &&
	            plan.Domain() == duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
	            plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR &&
	            plan.OutputColumns().size() == expected_names.size(),
	        "public provider services did not return the expected typed GraphQL plan");
	for (std::size_t index = 0; index < expected_names.size(); index++) {
		Require(plan.OutputColumns()[index].name == expected_names[index] &&
		            plan.OutputColumns()[index].logical_type == expected_types[index] &&
		            plan.OutputColumns()[index].nullable == (index == 4),
		        "public plan changed GraphQL schema order, type, or nullability");
	}
	auto explanation = duckdb::duckdb_api_query_internal::ExplainSelectedScan(request, plan);
	Require(explanation["Protocol"] == "graphql" &&
	            explanation["Operation Identity"] == "github_viewer_repository_metrics" &&
	            explanation["Operation Kind"] == "query" &&
	            explanation["Endpoint"] == "https://api.github.com:443/graphql" &&
	            explanation["Partial Data"] == "fail_on_any_error" &&
	            explanation["Nullable Columns"] == "primary_language" &&
	            explanation["Pagination Strategy"] == "graphql_cursor" &&
	            explanation["Page Dependency"] == "sequential" && explanation["Page Consistency"] == "mutable" &&
	            explanation["Page Size"] == "100" && explanation["Maximum Pages"] == "32" &&
	            explanation["Page Row Bound"] == "100" && explanation["Scan Row Bound"] == "3200" &&
	            explanation["Page Body Bytes"] == "8192" && explanation["Scan Body Bytes"] == "262144" &&
	            explanation["Stable Row Order"] == "none" && explanation["Snapshot Guarantee"] == "none" &&
	            explanation["Declared Replay Safety"] == "safe" && explanation["Retry"] == "disabled" &&
	            explanation["Rate-Limit Waiting"] == "disabled" && explanation["Cache"] == "disabled",
	        "typed GraphQL explanation facts changed or inferred unsupported authority");
	for (const auto &forbidden :
	     {"api.github.com", "viewer {", "repositories(", "$pageSize", "$cursor", "Authorization", "Bearer "}) {
		Require(request.Snapshot().find(forbidden) == std::string::npos,
		        "Query ScanRequest acquired protocol or credential authority");
	}
}

void TestOfflineBindPrepareAndSafeExplanation() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::GRAPHQL_SUCCESS);
	duckdb::Connection connection(database);
	const char *expected_names[] = {"id",      "full_name", "owner_login", "stars", "primary_language",
	                                "private", "archived",  "updated_at"};
	const char *expected_types[] = {"VARCHAR", "VARCHAR", "VARCHAR", "BIGINT",
	                                "VARCHAR", "BOOLEAN", "BOOLEAN", "VARCHAR"};
	auto describe = connection.Query("DESCRIBE SELECT * FROM " + std::string(GRAPHQL_SCAN));
	if (describe->HasError()) {
		throw std::runtime_error("GraphQL DESCRIBE failed offline: " + describe->GetError());
	}
	Require(describe->RowCount() == 8, "GraphQL DESCRIBE returned the wrong width");
	for (duckdb::idx_t index = 0; index < 8; index++) {
		Require(describe->GetValue(0, index).ToString() == expected_names[index] &&
		            describe->GetValue(1, index).ToString() == expected_types[index],
		        "GraphQL DESCRIBE changed ordered schema");
	}
	auto prepare =
	    connection.Query("PREPARE graphql_metrics AS SELECT * FROM " + std::string(GRAPHQL_SCAN) + " ORDER BY id");
	if (prepare->HasError()) {
		throw std::runtime_error("GraphQL PREPARE failed offline: " + prepare->GetError());
	}
	const auto explanation = Explain(connection, "SELECT * FROM " + std::string(GRAPHQL_SCAN));
	for (const auto &marker : {"Protocol",
	                           "graphql",
	                           "Operation Identity",
	                           "Operation Kind",
	                           "query",
	                           "Endpoint",
	                           "/graphql",
	                           "Partial Data",
	                           "fail_on_any_error",
	                           "Nullable Columns",
	                           "primary_language",
	                           "Pagination Strategy",
	                           "graphql_cursor",
	                           "Page Dependency",
	                           "sequential",
	                           "Page Consistency",
	                           "mutable",
	                           "Page Size",
	                           "100",
	                           "Maximum Pages",
	                           "32",
	                           "Page Row Bound",
	                           "Scan Row Bound",
	                           "3200",
	                           "Page Body Bytes",
	                           "8192",
	                           "Scan Body Bytes",
	                           "262144",
	                           "Stable Row Order",
	                           "none",
	                           "Snapshot Guarantee",
	                           "Remote Accuracy",
	                           "unsupported",
	                           "Projection Owner",
	                           "duckdb",
	                           "Ordering Owner",
	                           "Limit Owner",
	                           "Offset Owner"}) {
		Require(explanation.find(marker) != std::string::npos,
		        "GraphQL explanation omitted a typed safe fact: " + std::string(marker) + "\n" + explanation);
	}
	for (const auto &forbidden :
	     {"github_viewer_repository_metrics_v1", "query DuckdbApiViewerRepositoryMetrics", "$pageSize", "$cursor",
	      "viewer {", "nodes {", "endCursor", "graphql_test", "Authorization", "Bearer "}) {
		Require(explanation.find(forbidden) == std::string::npos,
		        "GraphQL explanation exposed document, cursor, or credential state");
	}
	Require(probe->legacy_open_calls.load(std::memory_order_relaxed) == 0 &&
	            probe->authorization_open_calls.load(std::memory_order_relaxed) == 0 &&
	            probe->streams_opened.load(std::memory_order_relaxed) == 0,
	        "DESCRIBE, PREPARE, or EXPLAIN entered Runtime");
}

void TestPreparedTypedRowsNullsAndDuckdbComposition() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::GRAPHQL_SUCCESS);
	duckdb::Connection connection(database);
	auto prepare =
	    connection.Query("PREPARE graphql_metrics AS SELECT * FROM " + std::string(GRAPHQL_SCAN) + " ORDER BY id");
	if (prepare->HasError()) {
		throw std::runtime_error("GraphQL PREPARE failed before secret creation: " + prepare->GetError());
	}
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 0,
	        "GraphQL PREPARE resolved a secret or opened a stream");
	CreateTemporarySecret(connection, "query-test-token-not-for-network");

	for (int execution = 0; execution < 2; execution++) {
		auto result = connection.Query("EXECUTE graphql_metrics");
		if (result->HasError()) {
			throw std::runtime_error("prepared GraphQL execution failed: " + result->GetError());
		}
		Require(result->RowCount() == 2 && result->ColumnCount() == 8,
		        "prepared GraphQL execution returned the wrong shape");
		Require(result->GetValue(0, 0).ToString() == "NODE-A" && result->GetValue(3, 0).GetValue<int64_t>() == 0 &&
		            result->GetValue(4, 0).IsNull() && !result->GetValue(5, 0).GetValue<bool>() &&
		            !result->GetValue(6, 0).GetValue<bool>(),
		        "nullable/zero/false GraphQL values changed at the DuckDB boundary");
		Require(result->GetValue(0, 1).ToString() == "NODE-B" && result->GetValue(4, 1).ToString() == "C++" &&
		            result->GetValue(5, 1).GetValue<bool>() && result->GetValue(6, 1).GetValue<bool>(),
		        "valid non-null GraphQL values changed at the DuckDB boundary");
	}

	auto composed = connection.Query(
	    "SELECT metrics.full_name, metrics.stars, metrics.primary_language IS NULL AS language_missing "
	    "FROM " +
	    std::string(GRAPHQL_SCAN) +
	    " AS metrics JOIN (VALUES ('fixture/zero')) AS selected(full_name) USING (full_name) "
	    "WHERE metrics.archived = FALSE ORDER BY metrics.stars DESC LIMIT 1");
	if (composed->HasError()) {
		throw std::runtime_error("DuckDB GraphQL composition failed: " + composed->GetError());
	}
	Require(composed->RowCount() == 1 && composed->GetValue(0, 0).ToString() == "fixture/zero" &&
	            composed->GetValue(1, 0).GetValue<int64_t>() == 0 && composed->GetValue(2, 0).GetValue<bool>(),
	        "DuckDB did not retain GraphQL filter/join/order/limit ownership");
	Require(probe->legacy_open_calls.load(std::memory_order_relaxed) == 0 &&
	            probe->authorization_open_calls.load(std::memory_order_relaxed) == 3 &&
	            probe->github_bearer_authorizations.load(std::memory_order_relaxed) == 3 &&
	            probe->streams_opened.load(std::memory_order_relaxed) == 3 &&
	            probe->streams_closed.load(std::memory_order_relaxed) == 3,
	        "prepared/repeated/composed GraphQL scans did not own isolated streams");
}

void TestRemoteProtocolErrorUsesSingleRedactedBoundary() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::REMOTE_PROTOCOL_ERROR);
	duckdb::Connection connection(database);
	const std::string canary = "remote-message-and-response-canary";
	CreateTemporarySecret(connection, canary);
	const auto error = QueryError(connection, "SELECT * FROM " + std::string(GRAPHQL_SCAN));
	Require(error.find("[duckdb_api][remote_protocol] connector=github relation=viewer_repository_metrics ") !=
	                std::string::npos &&
	            error.find("field=errors: remote protocol response reported application errors") != std::string::npos,
	        "GraphQL application error did not use the structured remote-protocol category: " + error);
	Require(error.find(canary) == std::string::npos && error.find("top-secret") == std::string::npos,
	        "GraphQL application error exposed credential or provider detail");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 1 &&
	            probe->cancellations.load(std::memory_order_relaxed) == 1 &&
	            probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "GraphQL application error did not cancel and close exactly one stream");
}

void TestRestExplainReplayPolicy() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto request = duckdb_api::BuildConservativeScanRequest(connector, "duckdb_login_search_page",
	                                                              duckdb_api::LogicalSecretReference());
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	auto explanation = duckdb::duckdb_api_query_internal::ExplainSelectedScan(request, plan);
	Require(explanation["Protocol"] == "rest" && explanation["Declared Replay Safety"] == "safe" &&
	            explanation["Retry"] == "disabled" && explanation["Rate-Limit Waiting"] == "disabled" &&
	            explanation["Cache"] == "disabled",
	        "REST EXPLAIN did not surface the effective resilience policy (declared replay safety read from the REST "
	        "plan)");
}

} // namespace

int main() {
	try {
		TestPublicProviderPlanAndProtocolNeutralRequest();
		TestOfflineBindPrepareAndSafeExplanation();
		TestPreparedTypedRowsNullsAndDuckdbComposition();
		TestRemoteProtocolErrorUsesSingleRedactedBoundary();
		TestRestExplainReplayPolicy();
		std::cout << "GraphQL Query adapter contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "GraphQL Query adapter contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
