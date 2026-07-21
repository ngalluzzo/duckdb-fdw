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
		ManualControl boundary_control;
		const auto boundary = service.ExecuteGraphqlBodyResourceVariant(
		    plan, transcript, field, RuntimeFixtureBoundaryVariant::BOUNDARY, boundary_control);
		Require(boundary.outcome == RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED && boundary.execution.succeeded &&
		            boundary.observed_units == boundary.admitted_limit,
		        "GraphQL request-body boundary did not debit its exact admitted limit");

		ManualControl rejected_control;
		const auto rejected = service.ExecuteGraphqlBodyResourceVariant(
		    plan, transcript, field, RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED, rejected_control);
		Require(rejected.outcome == RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED && !rejected.execution.succeeded &&
		            rejected.execution.has_runtime_error &&
		            rejected.execution.runtime_error_stage == duckdb_api::ErrorStage::RESOURCE &&
		            rejected.execution.runtime_error_field == "request_body_bytes" &&
		            rejected.observed_units == rejected.admitted_limit + 1,
		        "GraphQL request-body one-over lost its exact resource rejection");
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

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_fixture_graphql_body_variant_tests ABSOLUTE_REPOSITORY_ROOT");
		TestCanonicalGithubBodyAuthority();
		TestControlledNonGithubBodyAuthority(argv[1]);
		std::cout << "package fixture Runtime GraphQL body variant tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package fixture Runtime GraphQL body variant tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
