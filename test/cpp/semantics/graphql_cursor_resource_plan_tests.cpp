#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "support/require.hpp"

namespace duckdb_api_test {
namespace graphql_semantics {

void TestCursorResources() {
	const auto plan = BuildProductionPlan();
	const auto &pagination = plan.Pagination();
	Require(pagination.Strategy() == duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR,
	        "GraphQL plan did not select typed cursor pagination");
	const auto &cursor = pagination.GraphqlCursor();
	Require(cursor.direction == duckdb_api::PlannedGraphqlCursorDirection::FORWARD &&
	            cursor.dependency == duckdb_api::PlannedGraphqlCursorDependency::SEQUENTIAL &&
	            cursor.consistency == duckdb_api::PlannedGraphqlCursorConsistency::MUTABLE && !cursor.supports_total &&
	            !cursor.supports_resume && cursor.max_concurrent_pages == 1 &&
	            cursor.page_size_variable == "pageSize" && cursor.page_size == 100 &&
	            cursor.cursor_variable == "cursor" && cursor.max_pages_per_scan == 32,
	        "GraphQL cursor plan widened or contradicted the accepted transition");
	const auto &page = pagination.PageBudgets();
	const auto &scan = pagination.ScanBudgets();
	Require(page.request_attempts == 1 && page.decoded_records == 100 &&
	            page.serialized_request_body_bytes == 8 * 1024 && scan.request_attempts == 32 && scan.pages == 32 &&
	            scan.decoded_records == 3200 && scan.serialized_request_body_bytes == 256 * 1024 &&
	            page.concurrency == 1 && scan.concurrency == 1 && page.IsWithinPaginatedPageBounds() &&
	            scan.IsWithinPaginatedScanBounds(),
	        "GraphQL page/scan row, attempt, concurrency, or serialized-body envelope drifted");
	Require(plan.Retry() == duckdb_api::FeatureState::DISABLED && plan.Cache() == duckdb_api::FeatureState::DISABLED &&
	            plan.Providers() == duckdb_api::FeatureState::DISABLED,
	        "GraphQL plan enabled retry, cache, or provider authority");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
