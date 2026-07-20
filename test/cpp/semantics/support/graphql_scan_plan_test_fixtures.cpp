#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "semantics/support/scan_plan_test_access.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api_test {

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixtureImpl(const std::string &exact_logical_secret_name);

void ScanPlanTestAccess::ReplaceGraphql(duckdb_api::ScanPlan &plan, duckdb_api::PlannedGraphqlOperation operation) {
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromGraphql(std::move(operation)));
}

duckdb_api::ScanPlan ScanPlanTestAccess::Graphql(duckdb_api::ScanPlan plan,
                                                 GraphqlRuntimeAdmissionCounterexample counterexample) {
	if (!MutateGraphqlProtocol(plan, counterexample) && !MutateGraphqlOperationOrSchema(plan, counterexample) &&
	    !MutateGraphqlRelationalOrAuthority(plan, counterexample) && !MutateGraphqlCursor(plan, counterexample) &&
	    !MutateGraphqlResources(plan, counterexample)) {
		throw std::invalid_argument("unknown closed GraphQL plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixture(const std::string &exact_logical_secret_name) {
	return BuildValidGraphqlScanPlanFixtureImpl(exact_logical_secret_name);
}

duckdb_api::ScanPlan BuildGraphqlRuntimeAdmissionCounterexample(const std::string &exact_logical_secret_name,
                                                                GraphqlRuntimeAdmissionCounterexample counterexample) {
	return ScanPlanTestAccess::Graphql(BuildValidGraphqlScanPlanFixtureImpl(exact_logical_secret_name), counterexample);
}

} // namespace duckdb_api_test
