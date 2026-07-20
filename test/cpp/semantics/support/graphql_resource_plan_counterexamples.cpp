#include "semantics/support/scan_plan_test_access.hpp"

namespace duckdb_api_test {

bool ScanPlanTestAccess::MutateGraphqlResources(duckdb_api::ScanPlan &plan, GraphqlPlanCounterexample counterexample) {
	auto &page = plan.pagination.page_budgets;
	auto &scan = plan.pagination.scan_budgets;
	switch (counterexample) {
	case GraphqlPlanCounterexample::OTHER_PAGE_REQUEST_ATTEMPTS:
		page.request_attempts++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_RESPONSE_BYTES:
		page.response_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_HEADER_BYTES:
		page.header_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_DECOMPRESSED_BYTES:
		page.decompressed_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_DECODED_RECORDS:
		page.decoded_records++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_EXTRACTED_STRING_BYTES:
		page.extracted_string_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_JSON_NESTING:
		page.json_nesting++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_DECODED_MEMORY_BYTES:
		page.decoded_memory_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_BATCH_ROWS:
		page.batch_rows++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_WALL_MILLISECONDS:
		page.wall_milliseconds++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_CONCURRENCY:
		page.concurrency++;
		return true;
	case GraphqlPlanCounterexample::OTHER_PAGE_SERIALIZED_BODY_BYTES:
		page.serialized_request_body_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_REQUEST_ATTEMPTS:
		scan.request_attempts++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_PAGES:
		scan.pages++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_RESPONSE_BYTES:
		scan.response_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_HEADER_BYTES:
		scan.header_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_DECOMPRESSED_BYTES:
		scan.decompressed_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_DECODED_RECORDS:
		scan.decoded_records++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_EXTRACTED_STRING_BYTES:
		scan.extracted_string_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_JSON_NESTING:
		scan.json_nesting++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_DECODED_MEMORY_BYTES:
		scan.decoded_memory_bytes++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_BATCH_ROWS:
		scan.batch_rows++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_WALL_MILLISECONDS:
		scan.wall_milliseconds++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_CONCURRENCY:
		scan.concurrency++;
		return true;
	case GraphqlPlanCounterexample::OTHER_SCAN_SERIALIZED_BODY_BYTES:
		scan.serialized_request_body_bytes++;
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_api_test
