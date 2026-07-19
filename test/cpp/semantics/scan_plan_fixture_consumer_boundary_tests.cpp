#include "semantics/support/scan_plan_test_fixture_test_support.hpp"

#include "duckdb_api/scan_planner.hpp"
#include "query/support/live_scan_request.hpp"
#include "support/require.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"

#include <fstream>
#include <string>
#include <vector>

#ifndef DUCKDB_API_SOURCE_ROOT
#define DUCKDB_API_SOURCE_ROOT "."
#endif

namespace duckdb_api_test {
std::string ConsumeSafeScanPlanFixtureHeader(const std::string &exact_logical_secret_name);

namespace scan_plan_fixture_contract {
namespace {

std::string ReadText(const std::string &path) {
	std::ifstream input(path.c_str(), std::ios::binary);
	Require(input.good(), "could not read fixture boundary source: " + path);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string SnapshotWithoutReason(const duckdb_api::ScanPlan &plan) {
	const auto snapshot = plan.Snapshot();
	const auto reason = snapshot.find(";reason=");
	Require(reason != std::string::npos, "plan snapshot omitted its reason boundary");
	return snapshot.substr(0, reason);
}

} // namespace

void TestValidFactoriesUsePublicPlanningPath() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &anonymous = scan_plan_contract::FindRelation(connector, "duckdb_login_search_page");
	const auto &authenticated = scan_plan_contract::FindRelation(connector, "authenticated_user");
	const auto &repositories = scan_plan_contract::FindRelation(connector, "authenticated_repositories");
	const auto expected_anonymous =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	const auto expected_authenticated = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, authenticated.Name(), "fixture_secret_name"));
	auto selective_request = BuildAuthenticatedScanRequest(connector, repositories.Name(), "fixture_secret_name");
	selective_request.requested_predicate = duckdb_api::RequestedPredicate::VisibilityEqualsPrivate();
	selective_request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	selective_request.capabilities.selective_predicate = true;
	selective_request.capabilities.retains_predicate = true;
	const auto expected_selective = duckdb_api::BuildConservativeScanPlan(connector, selective_request);
	const auto provided_anonymous = BuildValidAnonymousPlanFixture();
	const auto provided_authenticated = BuildValidAuthenticatedPlanFixture("fixture_secret_name");
	const auto provided_selective = BuildVisibilityPrivatePlanFixture("fixture_secret_name");

	Require(SnapshotWithoutReason(provided_anonymous) == SnapshotWithoutReason(expected_anonymous),
	        "closed anonymous fixture drifted from the planner-produced executable plan");
	Require(SnapshotWithoutReason(provided_authenticated) == SnapshotWithoutReason(expected_authenticated),
	        "closed authenticated fixture drifted from the planner-produced executable plan");
	Require(SnapshotWithoutReason(provided_selective) == SnapshotWithoutReason(expected_selective) &&
	            provided_selective.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "closed selective fixture drifted from the planner-produced executable plan");
	Require(provided_authenticated.SecretReference().IsPresent() &&
	            provided_authenticated.SecretReference().Name() == "fixture_secret_name",
	        "authenticated fixture did not preserve the exact logical secret name");
	Require(ConsumeSafeScanPlanFixtureHeader("fixture_secret_name") ==
	            authenticated.Name() + ":" + repositories.Name() + ":" + authenticated.Name(),
	        "safe-header-only consumer probe did not compile and link the fixture service");
}

void TestSafeConsumerHeaderBoundary() {
	const std::string root = DUCKDB_API_SOURCE_ROOT;
	const auto header = ReadText(root + "/test/cpp/semantics/support/scan_plan_test_fixtures.hpp");
	const auto consumer = ReadText(root + "/test/cpp/semantics/support/scan_plan_fixture_consumer_probe.cpp");
	const auto plan_header = ReadText(root + "/src/include/duckdb_api/scan_plan.hpp");
	const auto runtime_targets = ReadText(root + "/src/runtime/targets.cmake");
	const auto runtime_test_targets = ReadText(root + "/test/cpp/runtime/targets.cmake");
	const auto semantics_targets = ReadText(root + "/test/cpp/semantics/targets.cmake");
	const std::vector<std::string> forbidden = {"scan_plan_test_access",
	                                            "connector_catalog_test_access",
	                                            "duckdb/main",
	                                            "duckdb_secret",
	                                            "SecretManager",
	                                            "ClientContext",
	                                            "authorized_secret",
	                                            "http_scan_executor",
	                                            "curl"};
	for (const auto &value : forbidden) {
		Require(header.find(value) == std::string::npos,
		        "safe fixture consumer header leaked a forbidden dependency: " + value);
		Require(consumer.find(value) == std::string::npos,
		        "safe fixture consumer probe leaked a forbidden dependency: " + value);
	}
	Require(header.find("friend ") == std::string::npos &&
	            header.find("#include \"duckdb_api/scan_plan.hpp\"") != std::string::npos,
	        "safe fixture header exposed construction authority or omitted the public plan API");
	for (const auto &value :
	     {"duckdb_api/connector", "duckdb_api/scan_request", "LogicalSecretReference", "BuildConservativeScanPlan"}) {
		Require(plan_header.find(value) == std::string::npos,
		        "ScanPlan consumer header leaked a planner input dependency: " + std::string(value));
	}
	for (const auto &value :
	     {"duckdb_api_connector", "duckdb_api_query_request", "duckdb_api_relational_planning_service"}) {
		Require(runtime_targets.find(value) == std::string::npos,
		        "Runtime production target linked a planner input or construction service: " + std::string(value));
		Require(runtime_test_targets.find(value) == std::string::npos,
		        "Runtime test target linked a planner input or construction service: " + std::string(value));
	}
	const auto fixture_target = semantics_targets.find("duckdb_api_semantics_fixture_service STATIC");
	const auto fixture_target_end = semantics_targets.find("add_executable", fixture_target);
	Require(fixture_target != std::string::npos && fixture_target_end != std::string::npos,
	        "Semantics fixture provider target was not inspectable");
	const auto fixture_block = semantics_targets.substr(fixture_target, fixture_target_end - fixture_target);
	for (const auto &value :
	     {"duckdb_api_connector", "duckdb_api_query_request", "duckdb_api_relational_planning_service"}) {
		Require(fixture_block.find(value) == std::string::npos,
		        "Semantics plan-only fixture provider imported a construction service: " + std::string(value));
	}
	Require(fixture_block.find("duckdb_api_scan_plan_service") != std::string::npos,
	        "Semantics plan-only fixture provider omitted the immutable ScanPlan service");
	const auto first_include = consumer.find("#include");
	Require(first_include != std::string::npos &&
	            consumer.find("#include", first_include + std::string("#include").size()) == std::string::npos &&
	            consumer.find("semantics/support/scan_plan_test_fixtures.hpp") != std::string::npos,
	        "consumer probe included more than the safe fixture header");
}

} // namespace scan_plan_fixture_contract
} // namespace duckdb_api_test
