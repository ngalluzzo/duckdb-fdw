#include "semantics/support/scan_plan_test_access.hpp"

#include <memory>

namespace duckdb_api_test {
namespace {

duckdb_api::PlannedRestOperation WrongRestPayload() {
	return {"wrong_rest_payload",
	        duckdb_api::PlannedHttpMethod::GET,
	        duckdb_api::PlannedCardinality::ZERO_TO_MANY,
	        duckdb_api::PlannedReplaySafety::SAFE,
	        {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	        "/graphql",
	        {},
	        {},
	        duckdb_api::PlannedResponseSource::ROOT_OBJECT,
	        "$"};
}

} // namespace

bool ScanPlanTestAccess::MutateGraphqlProtocol(duckdb_api::ScanPlan &plan,
                                               GraphqlRuntimeAdmissionCounterexample counterexample) {
	const auto graphql = std::make_shared<const duckdb_api::PlannedGraphqlOperation>(plan.Operation().Graphql());
	switch (counterexample) {
	case GraphqlRuntimeAdmissionCounterexample::UNKNOWN_PROTOCOL:
		plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
		    duckdb_api::PlannedProtocolOperation(static_cast<duckdb_api::PlannedProtocol>(127), nullptr, graphql));
		return true;
	case GraphqlRuntimeAdmissionCounterexample::GRAPHQL_CONTRADICTORY_PROTOCOL_PAYLOADS:
		plan.operation =
		    std::make_shared<const duckdb_api::PlannedProtocolOperation>(duckdb_api::PlannedProtocolOperation(
		        duckdb_api::PlannedProtocol::GRAPHQL,
		        std::make_shared<const duckdb_api::PlannedRestOperation>(WrongRestPayload()), graphql));
		return true;
	case GraphqlRuntimeAdmissionCounterexample::GRAPHQL_MISSING_ACTIVE_PROTOCOL_PAYLOAD:
		plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
		    duckdb_api::PlannedProtocolOperation(duckdb_api::PlannedProtocol::GRAPHQL, nullptr, nullptr));
		return true;
	case GraphqlRuntimeAdmissionCounterexample::GRAPHQL_WRONG_PROTOCOL_PAYLOAD:
		plan.operation =
		    std::make_shared<const duckdb_api::PlannedProtocolOperation>(duckdb_api::PlannedProtocolOperation(
		        duckdb_api::PlannedProtocol::GRAPHQL,
		        std::make_shared<const duckdb_api::PlannedRestOperation>(WrongRestPayload()), nullptr));
		return true;
	case GraphqlRuntimeAdmissionCounterexample::REST_CONTRADICTORY_PROTOCOL_PAYLOADS:
		plan.operation =
		    std::make_shared<const duckdb_api::PlannedProtocolOperation>(duckdb_api::PlannedProtocolOperation(
		        duckdb_api::PlannedProtocol::REST,
		        std::make_shared<const duckdb_api::PlannedRestOperation>(WrongRestPayload()), graphql));
		return true;
	case GraphqlRuntimeAdmissionCounterexample::REST_MISSING_ACTIVE_PROTOCOL_PAYLOAD:
		plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
		    duckdb_api::PlannedProtocolOperation(duckdb_api::PlannedProtocol::REST, nullptr, nullptr));
		return true;
	case GraphqlRuntimeAdmissionCounterexample::REST_WRONG_PROTOCOL_PAYLOAD:
		plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
		    duckdb_api::PlannedProtocolOperation(duckdb_api::PlannedProtocol::REST, nullptr, graphql));
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_api_test
