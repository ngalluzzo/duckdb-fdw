#include "support/scan_plan_test_access.hpp"

#include <stdexcept>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::Feature(duckdb_api::ScanPlan plan, FeaturePlanCounterexample counterexample) {
	switch (counterexample) {
	case FeaturePlanCounterexample::PROVIDERS_ENABLED:
		plan.providers = duckdb_api::FeatureState::ENABLED;
		break;
	case FeaturePlanCounterexample::RETRY_ENABLED:
		plan.retry = duckdb_api::FeatureState::ENABLED;
		break;
	case FeaturePlanCounterexample::CACHE_ENABLED:
		plan.cache = duckdb_api::FeatureState::ENABLED;
		break;
	default:
		throw std::invalid_argument("unknown closed feature plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan BuildFeaturePlanCounterexample(const std::string &exact_logical_secret_name,
                                                    FeaturePlanCounterexample counterexample) {
	return ScanPlanTestAccess::Feature(BuildValidAuthenticatedPlanFixture(exact_logical_secret_name), counterexample);
}

} // namespace duckdb_api_test
