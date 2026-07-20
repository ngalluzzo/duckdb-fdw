#include "semantics/support/scan_plan_test_access.hpp"

namespace duckdb_api_test {

bool ScanPlanTestAccess::MutateGraphqlCursor(duckdb_api::ScanPlan &plan, GraphqlPlanCounterexample counterexample) {
	auto &cursor = plan.pagination.graphql_cursor;
	switch (counterexample) {
	case GraphqlPlanCounterexample::OTHER_PAGINATION_STRATEGY:
		plan.pagination.strategy = duckdb_api::PlannedPaginationStrategy::LINK_HEADER;
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_CURSOR_DIRECTION:
		cursor.direction = static_cast<duckdb_api::PlannedGraphqlCursorDirection>(127);
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_CURSOR_DEPENDENCY:
		cursor.dependency = static_cast<duckdb_api::PlannedGraphqlCursorDependency>(127);
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_CURSOR_CONSISTENCY:
		cursor.consistency = static_cast<duckdb_api::PlannedGraphqlCursorConsistency>(127);
		return true;
	case GraphqlPlanCounterexample::CURSOR_SUPPORTS_TOTAL:
		cursor.supports_total = true;
		return true;
	case GraphqlPlanCounterexample::CURSOR_SUPPORTS_RESUME:
		cursor.supports_resume = true;
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_CONCURRENCY:
		cursor.max_concurrent_pages++;
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_PAGE_SIZE_VARIABLE:
		cursor.page_size_variable = "other";
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_PAGE_SIZE:
		cursor.page_size++;
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_VARIABLE:
		cursor.cursor_variable = "other";
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_HAS_NEXT_PAGE_PATH:
		cursor.has_next_page.segments.back() = "other";
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_END_CURSOR_PATH:
		cursor.end_cursor.segments.back() = "other";
		return true;
	case GraphqlPlanCounterexample::OTHER_CURSOR_MAX_PAGES:
		cursor.max_pages_per_scan++;
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_api_test
