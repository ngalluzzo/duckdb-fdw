#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "query/support/live_scan_request.hpp"
#include "support/require.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace duckdb_api_test {
namespace graphql_semantics {

duckdb_api::ScanPlan BuildProductionPlan() {
	const auto connector = BuildCanonicalGraphqlConnectorCatalogFixture();
	const auto request = BuildAuthenticatedScanRequest(connector, GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION,
	                                                   "graphql_semantics_secret");
	return duckdb_api::BuildConservativeScanPlan(connector, request);
}

void TestOperationPlan() {
	const auto plan = BuildProductionPlan();
	Require(plan.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL,
	        "canonical GraphQL relation did not produce the GraphQL planned alternative");
	const auto &operation = plan.Operation().Graphql();
	Require(operation.operation_name == "github_viewer_repository_metrics" &&
	            operation.kind == duckdb_api::PlannedGraphqlOperationKind::QUERY &&
	            operation.cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            operation.replay_safety == duckdb_api::PlannedReplaySafety::SAFE &&
	            operation.document_identity ==
	                duckdb_api::PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 &&
	            operation.digest_algorithm == duckdb_api::PlannedGraphqlDigestAlgorithm::SHA256 &&
	            operation.document_digest == "9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85" &&
	            !operation.document.empty(),
	        "planner did not preserve the admitted query identity, exact bytes, digest, and derived replay fact");
	Require(operation.origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            operation.origin.host == "api.github.com" && operation.origin.port == 443 &&
	            operation.path == "/graphql" && operation.variables.size() == 2 && operation.result_columns.size() == 8,
	        "planned GraphQL endpoint, variable, or typed result profile drifted");
	bool wrong_variant_failed = false;
	try {
		(void)plan.Operation().Rest();
	} catch (const std::logic_error &) {
		wrong_variant_failed = true;
	}
	Require(wrong_variant_failed, "GraphQL plan exposed an inactive REST payload");

	const auto snapshot = plan.Snapshot();
	Require(snapshot.find(operation.document) == std::string::npos &&
	            snapshot.find("graphql_semantics_secret") == std::string::npos &&
	            snapshot.find("$cursor") == std::string::npos && snapshot.find("query:") != std::string::npos,
	        "safe explanation exposed document, variable, or secret bytes, or omitted query-only classification");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test

int main() {
	try {
		duckdb_api_test::graphql_semantics::TestOperationPlan();
		duckdb_api_test::graphql_semantics::TestBaseDomain();
		duckdb_api_test::graphql_semantics::TestCursorResources();
		duckdb_api_test::graphql_semantics::TestNullability();
		duckdb_api_test::graphql_semantics::TestFixtureBoundary();
		std::cout << "GraphQL relational Semantics tests passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
