#include "package_fixture_execution.hpp"

#include "runtime/execution/package_fixture_variant_test_support.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;
using duckdb_api_test::RuntimeFixtureBoundaryVariant;
using duckdb_api_test::RuntimeFixtureGraphqlBodyResourceField;
using duckdb_api_test::RuntimeFixtureVariantEvidencePath;
using duckdb_api_test::RuntimeFixtureVariantOutcome;
using duckdb_api_test::RuntimePackageFixtureExecutionService;
using duckdb_api_test::variant_test::GraphqlTranscript;
using duckdb_api_test::variant_test::ManualControl;
using duckdb_api_test::variant_test::NonGithubGraphqlTranscript;

void TestGraphqlBodyResourceFields(const duckdb_api::ScanPlan &plan,
                                   const duckdb_api_test::RuntimeFixtureTranscript &transcript) {
	const RuntimeFixtureGraphqlBodyResourceField fields[] = {
	    RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_REQUEST,
	    RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_SCAN};
	RuntimePackageFixtureExecutionService service;
	for (const auto field : fields) {
		const bool per_scan = field == RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_SCAN;
		const auto expected_path = per_scan ? RuntimeFixtureVariantEvidencePath::EXECUTOR_PLUS_SCAN_ACCOUNTING
		                                    : RuntimeFixtureVariantEvidencePath::EXECUTOR_PLUS_PAGE_ACCOUNTING;
		ManualControl boundary_control;
		const auto boundary = service.ExecuteGraphqlBodyResourceVariant(
		    plan, transcript, field, RuntimeFixtureBoundaryVariant::BOUNDARY, boundary_control);
		Require(boundary.outcome == RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED && boundary.execution.succeeded &&
		            !boundary.execution.has_runtime_error && boundary.execution.transport_observed &&
		            boundary.execution.request_count != 0 && boundary.execution.stream_close_invoked &&
		            boundary.evidence_path == expected_path && boundary.executor_observed_units > 0 &&
		            boundary.executor_observed_units < boundary.admitted_limit &&
		            boundary.accounting_observed_units == boundary.admitted_limit,
		        "GraphQL request-body boundary lost canonical executor or scoped accounting evidence");

		ManualControl rejected_control;
		const auto rejected = service.ExecuteGraphqlBodyResourceVariant(
		    plan, transcript, field, RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED, rejected_control);
		Require(rejected.outcome == RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED && rejected.execution.succeeded &&
		            !rejected.execution.has_runtime_error && rejected.execution.transport_observed &&
		            rejected.execution.request_count != 0 && rejected.execution.stream_close_invoked &&
		            rejected.evidence_path == expected_path && rejected.executor_observed_units > 0 &&
		            rejected.executor_observed_units < rejected.admitted_limit &&
		            rejected.accounting_observed_units == rejected.admitted_limit + 1,
		        "GraphQL request-body one-over lost canonical executor or scoped accounting evidence");
		Require(boundary.execution.requests[0].body == rejected.execution.requests[0].body &&
		            boundary.executor_observed_units == rejected.executor_observed_units,
		        "GraphQL accounting proof was substituted for observed production request bytes");
	}
}

void TestCanonicalGithubBodyAuthority() {
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("runtime_variant_secret");
	TestGraphqlBodyResourceFields(plan, GraphqlTranscript());
}

void TestControlledNonGithubBodyAuthority(const std::string &repository_root) {
	const auto plan = duckdb_api_test::BuildNonGithubPackageGraphqlPlan(repository_root);
	Require(plan.Pagination().PageBudgets().serialized_request_body_bytes == 16384 &&
	            plan.Pagination().ScanBudgets().serialized_request_body_bytes == 49152 &&
	            plan.Pagination().ScanBudgets().serialized_request_body_bytes ==
	                plan.Pagination().PageBudgets().serialized_request_body_bytes *
	                    plan.Pagination().GraphqlCursor().max_pages_per_scan,
	        "controlled non-GitHub plan did not carry reachable immutable GraphQL body authority");
	TestGraphqlBodyResourceFields(plan, NonGithubGraphqlTranscript());
}

void TestUnknownSelectorsFailBeforeTranscriptOrControlWork() {
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("runtime_variant_secret");
	const duckdb_api_test::RuntimeFixtureTranscript invalid {
	    duckdb_api_test::RuntimeFixtureAuthorizationState::BEARER_PRESENT, {}};
	RuntimePackageFixtureExecutionService service;
	ManualControl control;
	control.Cancel();
	bool field_rejected = false;
	try {
		(void)service.ExecuteGraphqlBodyResourceVariant(plan, invalid,
		                                                static_cast<RuntimeFixtureGraphqlBodyResourceField>(127),
		                                                RuntimeFixtureBoundaryVariant::BOUNDARY, control);
	} catch (const std::invalid_argument &error) {
		field_rejected = std::string(error.what()) == "unknown closed Runtime GraphQL body resource field";
	}
	Require(field_rejected, "unknown GraphQL body resource field reached transcript or cancellation work");

	bool boundary_rejected = false;
	try {
		(void)service.ExecuteGraphqlBodyResourceVariant(
		    plan, invalid, RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_REQUEST,
		    static_cast<RuntimeFixtureBoundaryVariant>(127), control);
	} catch (const std::invalid_argument &error) {
		boundary_rejected = std::string(error.what()) == "unknown closed Runtime GraphQL body boundary variant";
	}
	Require(boundary_rejected, "unknown GraphQL body boundary reached transcript or cancellation work");
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_fixture_graphql_body_variant_tests ABSOLUTE_REPOSITORY_ROOT");
		TestCanonicalGithubBodyAuthority();
		TestControlledNonGithubBodyAuthority(argv[1]);
		TestUnknownSelectorsFailBeforeTranscriptOrControlWork();
		std::cout << "package fixture Runtime GraphQL body variant tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package fixture Runtime GraphQL body variant tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
