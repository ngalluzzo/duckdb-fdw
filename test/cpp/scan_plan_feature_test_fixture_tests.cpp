#include "support/scan_plan_test_fixture_test_support.hpp"

#include "support/require.hpp"
#include "support/scan_plan_contract_test_support.hpp"
#include "support/scan_plan_test_fixtures.hpp"

#include <stdexcept>
#include <vector>

namespace duckdb_api_test {
namespace scan_plan_fixture_contract {

void TestFeatureCounterexamples(const std::string &canary) {
	const std::vector<FeaturePlanCounterexample> variants = {
	    FeaturePlanCounterexample::PAGINATION_ENABLED, FeaturePlanCounterexample::PROVIDERS_ENABLED,
	    FeaturePlanCounterexample::RETRY_ENABLED, FeaturePlanCounterexample::CACHE_ENABLED};
	for (const auto variant : variants) {
		const auto plan = BuildFeaturePlanCounterexample("fixture_secret_name", variant);
		switch (variant) {
		case FeaturePlanCounterexample::PAGINATION_ENABLED:
			Require(plan.Pagination() == duckdb_api::FeatureState::ENABLED,
			        "pagination counterexample remained disabled");
			break;
		case FeaturePlanCounterexample::PROVIDERS_ENABLED:
			Require(plan.Providers() == duckdb_api::FeatureState::ENABLED,
			        "providers counterexample remained disabled");
			break;
		case FeaturePlanCounterexample::RETRY_ENABLED:
			Require(plan.Retry() == duckdb_api::FeatureState::ENABLED, "retry counterexample remained disabled");
			break;
		case FeaturePlanCounterexample::CACHE_ENABLED:
			Require(plan.Cache() == duckdb_api::FeatureState::ENABLED, "cache counterexample remained disabled");
			break;
		}
		RequireCanaryAbsent(plan, canary);
	}

	scan_plan_contract::RequireThrows<std::invalid_argument>(
	    []() {
		    (void)BuildFeaturePlanCounterexample("fixture_secret_name", static_cast<FeaturePlanCounterexample>(255));
	    },
	    "feature fixture accepted an unknown counterexample");
}

} // namespace scan_plan_fixture_contract
} // namespace duckdb_api_test
