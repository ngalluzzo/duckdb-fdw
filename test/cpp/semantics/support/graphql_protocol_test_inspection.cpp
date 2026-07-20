#include "semantics/support/graphql_protocol_test_inspection.hpp"

#include "semantics/support/scan_plan_test_access.hpp"

namespace duckdb_api_test {

GraphqlProtocolEnvelopeShape ScanPlanTestAccess::GraphqlProtocolShape(const duckdb_api::ScanPlan &plan) {
	const auto &operation = plan.Operation();
	return {operation.protocol, operation.rest != nullptr, operation.graphql != nullptr};
}

GraphqlProtocolEnvelopeShape InspectGraphqlProtocolEnvelope(const duckdb_api::ScanPlan &plan) {
	return ScanPlanTestAccess::GraphqlProtocolShape(plan);
}

} // namespace duckdb_api_test
