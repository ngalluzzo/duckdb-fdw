#include "support/scan_plan_test_fixtures.hpp"

#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "support/live_scan_request.hpp"
#include "support/scan_plan_test_access.hpp"

#include <stdexcept>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::Pagination(duckdb_api::ScanPlan plan,
                                                    PaginationPlanCounterexample counterexample) {
	switch (counterexample) {
	case PaginationPlanCounterexample::STRATEGY_DISABLED:
		plan.pagination.strategy = duckdb_api::PlannedPaginationStrategy::DISABLED;
		break;
	case PaginationPlanCounterexample::UNKNOWN_DEPENDENCY:
		plan.pagination.dependency = static_cast<duckdb_api::PlannedPageDependency>(255);
		break;
	case PaginationPlanCounterexample::UNKNOWN_CONSISTENCY:
		plan.pagination.consistency = static_cast<duckdb_api::PlannedPageConsistency>(255);
		break;
	case PaginationPlanCounterexample::UNKNOWN_LINK_RELATION:
		plan.pagination.link_relation = static_cast<duckdb_api::PlannedLinkRelation>(255);
		break;
	case PaginationPlanCounterexample::UNKNOWN_TARGET_SCOPE:
		plan.pagination.target_scope = static_cast<duckdb_api::PlannedContinuationTargetScope>(255);
		break;
	case PaginationPlanCounterexample::SUPPORTS_TOTAL:
		plan.pagination.supports_total = true;
		break;
	case PaginationPlanCounterexample::SUPPORTS_RESUME:
		plan.pagination.supports_resume = true;
		break;
	case PaginationPlanCounterexample::EMPTY_TARGET_PATH:
		plan.pagination.target.path.clear();
		break;
	case PaginationPlanCounterexample::PAGE_REQUEST_ATTEMPTS_WIDENED:
		plan.pagination.page_budgets.request_attempts = 2;
		break;
	case PaginationPlanCounterexample::SCAN_REQUEST_ATTEMPTS_MISMATCH:
		plan.pagination.scan_budgets.request_attempts = plan.pagination.scan_budgets.pages - 1;
		break;
	case PaginationPlanCounterexample::SCAN_RESPONSE_BYTES_BELOW_PAGE:
		plan.pagination.scan_budgets.response_bytes = plan.pagination.page_budgets.response_bytes - 1;
		break;
	case PaginationPlanCounterexample::SCAN_DECODED_RECORDS_BELOW_PAGE:
		plan.pagination.scan_budgets.decoded_records = plan.pagination.page_budgets.decoded_records - 1;
		break;
	default:
		throw std::invalid_argument("unknown closed pagination plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan BuildValidPaginatedPlanFixture(const std::string &exact_logical_secret_name) {
	const auto connector = BuildPaginationConnectorCatalogFixture();
	const auto *relation = connector.FindRelation(PAGINATION_LINK_RELATION);
	if (relation == nullptr) {
		throw std::logic_error("pagination fixture omitted its exact Link relation");
	}
	return duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation->Name(), exact_logical_secret_name));
}

duckdb_api::ScanPlan BuildValidAuthenticatedRepositoriesPlanFixture(const std::string &exact_logical_secret_name) {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("authenticated_repositories");
	if (relation == nullptr) {
		throw std::logic_error("authenticated repositories fixture omitted its exact relation");
	}
	return duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation->Name(), exact_logical_secret_name));
}

duckdb_api::ScanPlan BuildPaginationPlanCounterexample(const std::string &exact_logical_secret_name,
                                                       PaginationPlanCounterexample counterexample) {
	return ScanPlanTestAccess::Pagination(BuildValidAuthenticatedRepositoriesPlanFixture(exact_logical_secret_name),
	                                      counterexample);
}

} // namespace duckdb_api_test
