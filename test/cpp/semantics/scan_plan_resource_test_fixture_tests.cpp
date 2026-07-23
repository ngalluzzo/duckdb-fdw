#include "semantics/support/scan_plan_test_fixture_test_support.hpp"

#include "support/require.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace duckdb_api_test {
namespace scan_plan_fixture_contract {
namespace {

std::size_t BudgetDifferenceCount(const duckdb_api::ResourceBudgets &left, const duckdb_api::ResourceBudgets &right) {
	std::size_t differences = 0;
	differences += left.request_attempts != right.request_attempts;
	differences += left.response_bytes != right.response_bytes;
	differences += left.header_bytes != right.header_bytes;
	differences += left.decompressed_bytes != right.decompressed_bytes;
	differences += left.decoded_records != right.decoded_records;
	differences += left.extracted_string_bytes != right.extracted_string_bytes;
	differences += left.json_nesting != right.json_nesting;
	differences += left.decoded_memory_bytes != right.decoded_memory_bytes;
	differences += left.batch_rows != right.batch_rows;
	differences += left.wall_milliseconds != right.wall_milliseconds;
	differences += left.concurrency != right.concurrency;
	return differences;
}

} // namespace

void TestResourceCounterexamples(const std::string &canary) {
	struct Case {
		ResourcePlanCounterexample counterexample;
		std::uint64_t duckdb_api::ResourceBudgets::*field;
		std::uint64_t expected;
	};
	const std::vector<Case> cases = {
	    {ResourcePlanCounterexample::REQUEST_ATTEMPTS_ZERO, &duckdb_api::ResourceBudgets::request_attempts, 0},
	    {ResourcePlanCounterexample::REQUEST_ATTEMPTS_WIDENED, &duckdb_api::ResourceBudgets::request_attempts,
	     duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP + 1},
	    {ResourcePlanCounterexample::RESPONSE_BYTES_ZERO, &duckdb_api::ResourceBudgets::response_bytes, 0},
	    {ResourcePlanCounterexample::RESPONSE_BYTES_WIDENED, &duckdb_api::ResourceBudgets::response_bytes,
	     duckdb_api::HOST_MAX_RESPONSE_BYTES + 1},
	    {ResourcePlanCounterexample::HEADER_BYTES_ZERO, &duckdb_api::ResourceBudgets::header_bytes, 0},
	    {ResourcePlanCounterexample::HEADER_BYTES_WIDENED, &duckdb_api::ResourceBudgets::header_bytes,
	     duckdb_api::HOST_MAX_HEADER_BYTES + 1},
	    {ResourcePlanCounterexample::DECOMPRESSED_BYTES_ZERO, &duckdb_api::ResourceBudgets::decompressed_bytes, 0},
	    {ResourcePlanCounterexample::DECOMPRESSED_BYTES_WIDENED, &duckdb_api::ResourceBudgets::decompressed_bytes,
	     duckdb_api::HOST_MAX_DECOMPRESSED_BYTES + 1},
	    {ResourcePlanCounterexample::DECODED_RECORDS_ZERO, &duckdb_api::ResourceBudgets::decoded_records, 0},
	    {ResourcePlanCounterexample::DECODED_RECORDS_WIDENED, &duckdb_api::ResourceBudgets::decoded_records,
	     duckdb_api::HOST_MAX_DECODED_RECORDS + 1},
	    {ResourcePlanCounterexample::EXTRACTED_STRING_BYTES_ZERO, &duckdb_api::ResourceBudgets::extracted_string_bytes,
	     0},
	    {ResourcePlanCounterexample::EXTRACTED_STRING_BYTES_WIDENED,
	     &duckdb_api::ResourceBudgets::extracted_string_bytes, duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES + 1},
	    {ResourcePlanCounterexample::JSON_NESTING_ZERO, &duckdb_api::ResourceBudgets::json_nesting, 0},
	    {ResourcePlanCounterexample::JSON_NESTING_WIDENED, &duckdb_api::ResourceBudgets::json_nesting,
	     duckdb_api::HOST_MAX_JSON_NESTING + 1},
	    {ResourcePlanCounterexample::DECODED_MEMORY_BYTES_ZERO, &duckdb_api::ResourceBudgets::decoded_memory_bytes, 0},
	    {ResourcePlanCounterexample::DECODED_MEMORY_BYTES_WIDENED, &duckdb_api::ResourceBudgets::decoded_memory_bytes,
	     duckdb_api::HOST_MAX_DECODED_MEMORY_BYTES + 1},
	    {ResourcePlanCounterexample::BATCH_ROWS_ZERO, &duckdb_api::ResourceBudgets::batch_rows, 0},
	    {ResourcePlanCounterexample::BATCH_ROWS_WIDENED, &duckdb_api::ResourceBudgets::batch_rows,
	     duckdb_api::OUTPUT_BATCH_ROWS + 1},
	    {ResourcePlanCounterexample::WALL_MILLISECONDS_ZERO, &duckdb_api::ResourceBudgets::wall_milliseconds, 0},
	    {ResourcePlanCounterexample::WALL_MILLISECONDS_WIDENED, &duckdb_api::ResourceBudgets::wall_milliseconds,
	     duckdb_api::MAX_EXECUTION_MILLISECONDS + 1},
	    {ResourcePlanCounterexample::CONCURRENCY_ZERO, &duckdb_api::ResourceBudgets::concurrency, 0},
	    {ResourcePlanCounterexample::CONCURRENCY_WIDENED, &duckdb_api::ResourceBudgets::concurrency,
	     duckdb_api::HOST_MAX_CONCURRENCY + 1}};
	const auto baseline = BuildValidAuthenticatedPlanFixture("fixture_secret_name");
	for (const auto &test_case : cases) {
		const auto plan = BuildResourcePlanCounterexample("fixture_secret_name", test_case.counterexample);
		Require(plan.Budgets().*(test_case.field) == test_case.expected,
		        "resource counterexample changed the wrong named field value");
		Require(BudgetDifferenceCount(plan.Budgets(), baseline.Budgets()) == 1,
		        "resource counterexample changed more than one budget fact");
		Require(!plan.Budgets().IsWithinLiveRestBounds(),
		        "resource counterexample remained inside the accepted host envelope");
		RequireCanaryAbsent(plan, canary);
	}

	scan_plan_contract::RequireThrows<std::invalid_argument>(
	    []() {
		    (void)BuildResourcePlanCounterexample("fixture_secret_name", static_cast<ResourcePlanCounterexample>(255));
	    },
	    "resource fixture accepted an unknown counterexample");
}

} // namespace scan_plan_fixture_contract
} // namespace duckdb_api_test
