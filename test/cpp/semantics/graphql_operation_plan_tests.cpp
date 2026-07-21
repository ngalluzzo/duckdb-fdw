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

namespace {

std::string RejectedMessage(InvalidGraphqlCatalogCandidate candidate) {
	const auto connector = BuildInvalidGraphqlConnectorCatalogCandidate(candidate);
	const auto request = BuildAuthenticatedScanRequest(connector, GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION,
	                                                   "graphql_semantics_secret");
	try {
		(void)duckdb_api::BuildConservativeScanPlan(connector, request);
	} catch (const duckdb_api::PlanningError &error) {
		Require(error.Code() == duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
		        "invalid Connector GraphQL candidate used the wrong planning error category");
		return error.what();
	}
	throw std::runtime_error("invalid Connector GraphQL candidate produced a partial plan");
}

void TestInvalidProviderCandidates() {
	using Candidate = InvalidGraphqlCatalogCandidate;
	const Candidate candidates[] = {
	    Candidate::UNKNOWN_DOCUMENT_IDENTITY, Candidate::CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST,
	    Candidate::DOCUMENT_DIGEST_MISMATCH,  Candidate::VARIABLE_PROFILE_DRIFT,
	    Candidate::RESPONSE_NODES_PATH_DRIFT, Candidate::RESPONSE_ERRORS_PATH_DRIFT,
	    Candidate::PARTIAL_DATA_POLICY_DRIFT, Candidate::CURSOR_PROFILE_DRIFT,
	    Candidate::BODY_BUDGET_DRIFT,         Candidate::SCHEMA_TYPE_DRIFT,
	    Candidate::SCHEMA_NULLABILITY_DRIFT};
	for (const auto candidate : candidates) {
		const auto first = RejectedMessage(candidate);
		const auto second = RejectedMessage(candidate);
		Require(first == second && !first.empty(),
		        "invalid Connector GraphQL candidate did not fail deterministically");
	}
}

} // namespace

duckdb_api::ScanPlan BuildProductionPlan() {
	return BuildProductionPlan(GraphqlLocalResidualProfile::UNRESTRICTED);
}

duckdb_api::ScanPlan BuildProductionPlan(GraphqlLocalResidualProfile profile) {
	const auto connector = BuildCanonicalGraphqlConnectorCatalogFixture();
	auto request = BuildAuthenticatedScanRequest(connector, GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION,
	                                             "graphql_semantics_secret");
	const auto archived_equals_false = []() {
		return duckdb_api::RequestedPredicate::Comparison(6, duckdb_api::RequestedPredicateValueKind::BOOLEAN,
		                                                  duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
		                                                  duckdb_api::RequestedPredicateValue::Boolean(false));
	};
	switch (profile) {
	case GraphqlLocalResidualProfile::UNRESTRICTED:
		break;
	case GraphqlLocalResidualProfile::MAPPING_UNAVAILABLE:
		request.requested_predicate = archived_equals_false();
		request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
		request.capabilities.selective_predicate = true;
		request.capabilities.retains_predicate = true;
		break;
	case GraphqlLocalResidualProfile::STRUCTURE_UNSUPPORTED:
		request.requested_predicate = duckdb_api::RequestedPredicate::Unsupported(0);
		request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER;
		request.capabilities.selective_predicate = true;
		request.capabilities.retains_predicate = true;
		break;
	case GraphqlLocalResidualProfile::CAPABILITY_UNAVAILABLE:
		request.requested_predicate = archived_equals_false();
		request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
		request.capabilities.selective_predicate = false;
		request.capabilities.retains_predicate = true;
		break;
	case GraphqlLocalResidualProfile::COUNT:
		throw std::invalid_argument("unknown GraphQL local-residual production profile");
	}
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
	Require(plan.SourceSnapshot().find("pageSize") != std::string::npos &&
	            plan.SourceSnapshot().find("cursor") != std::string::npos,
	        "GraphQL explanation canary no longer probes raw Connector variable declarations");
	Require(snapshot.find(operation.document) == std::string::npos &&
	            snapshot.find("graphql_semantics_secret") == std::string::npos &&
	            snapshot.find("pageSize") == std::string::npos &&
	            snapshot.find("cursor_variable") == std::string::npos &&
	            snapshot.find("cursor:") == std::string::npos && snapshot.find("query:") != std::string::npos &&
	            snapshot.find("canonical_graphql_profile:github_viewer_repository_metrics_v1") != std::string::npos,
	        "safe explanation exposed document, variable, or secret bytes, or omitted query-only classification");
	TestInvalidProviderCandidates();
}

} // namespace graphql_semantics
} // namespace duckdb_api_test

int main(int argc, char **argv) {
	try {
		duckdb_api_test::Require(argc == 2, "usage: graphql_semantics_tests ABSOLUTE_REPOSITORY_ROOT");
		duckdb_api_test::graphql_semantics::TestOperationPlan();
		duckdb_api_test::graphql_semantics::TestBaseDomain();
		duckdb_api_test::graphql_semantics::TestCursorResources();
		duckdb_api_test::graphql_semantics::TestNullability();
		duckdb_api_test::graphql_semantics::TestFixtureBoundary();
		duckdb_api_test::graphql_semantics::TestPackageGraphqlPlanning(argv[1]);
		std::cout << "GraphQL relational Semantics tests passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
