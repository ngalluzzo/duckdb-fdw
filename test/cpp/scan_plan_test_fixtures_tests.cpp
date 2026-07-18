#include "support/scan_plan_test_fixture_test_support.hpp"

#include "support/scan_plan_contract_test_support.hpp"
#include "support/scan_plan_test_fixtures.hpp"

#include <cstdlib>
#include <iostream>
#include <type_traits>

static_assert(std::is_same<decltype(duckdb_api_test::BuildValidAnonymousPlanFixture()), duckdb_api::ScanPlan>::value,
              "safe fixture factory must return the immutable public plan type");
static_assert(!std::is_default_constructible<duckdb_api::ScanPlan>::value,
              "test fixtures must not make ScanPlan publicly constructible");
static_assert(!std::is_copy_assignable<duckdb_api::ScanPlan>::value,
              "test fixtures must not weaken immutable plan assignment");
static_assert(!std::is_default_constructible<duckdb_api::PlannedAuthenticationObligation>::value,
              "test fixtures must not make authorization obligations publicly constructible");

int main() {
	try {
		using namespace duckdb_api_test::scan_plan_fixture_contract;
		const auto canary = duckdb_api_test::scan_plan_contract::RuntimeCredentialCanary();
		duckdb_api_test::scan_plan_contract::ScopedEnvironment hostile;
		hostile.Set("DUCKDB_API_TOKEN", canary);
		hostile.Set("GITHUB_TOKEN", canary);
		hostile.Set("HTTP_PROXY", canary);
		hostile.Set("HTTPS_PROXY", canary);
		hostile.Set("HOME", canary);

		TestValidFactoriesUsePublicPlanningPath();
		TestOperationCounterexamples(canary);
		TestAuthenticationCounterexamples(canary);
		TestResponseCounterexamples(canary);
		TestNetworkCounterexamples(canary);
		TestFeatureCounterexamples(canary);
		TestResourceCounterexamples(canary);
		TestSafeConsumerHeaderBoundary();
		std::cout << "scan plan test fixture tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan plan test fixture tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
