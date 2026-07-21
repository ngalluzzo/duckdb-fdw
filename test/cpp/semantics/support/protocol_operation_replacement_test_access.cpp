#include "semantics/support/scan_plan_test_access.hpp"

#include <memory>
#include <utility>

namespace duckdb_api_test {

// Shared friend-only mutation boundary for closed Semantics fixtures. Keeping
// protocol replacement separate lets narrow package-plan providers construct
// their counterexamples without linking the broad fixture implementation.
void ScanPlanTestAccess::ReplaceRest(duckdb_api::ScanPlan &plan, duckdb_api::PlannedRestOperation operation) {
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromRest(std::move(operation)));
}

void ScanPlanTestAccess::ReplaceGraphql(duckdb_api::ScanPlan &plan, duckdb_api::PlannedGraphqlOperation operation) {
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromGraphql(std::move(operation)));
}

} // namespace duckdb_api_test
