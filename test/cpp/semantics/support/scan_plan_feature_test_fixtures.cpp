#include "semantics/support/scan_plan_test_access.hpp"

#include <stdexcept>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::RetryEnabled(duckdb_api::ScanPlan plan) {
	if (plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED ||
	    plan.pagination.scan_budgets.pages == 0 || plan.pagination.scan_budgets.pages > 32) {
		throw std::invalid_argument("retry-enabled fixture requires a bounded paginated safe read");
	}
	plan.retry = duckdb_api::FeatureState::ENABLED;
	plan.replay_class = duckdb_api::PlannedOperationReplayClass::REPLAYABLE_READ;
	plan.retry_policy = {3, plan.pagination.scan_budgets.pages * 3, 10, 250};
	plan.budgets.request_attempts = 3;
	plan.pagination.page_budgets.request_attempts = 3;
	plan.pagination.scan_budgets.request_attempts = plan.retry_policy.max_attempts_per_scan;
	return plan;
}

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
