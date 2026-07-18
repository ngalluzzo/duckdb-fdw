#include "support/scan_plan_test_access.hpp"

#include <stdexcept>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::Operation(duckdb_api::ScanPlan plan,
                                                   OperationPlanCounterexample counterexample) {
	switch (counterexample) {
	case OperationPlanCounterexample::OTHER_CONNECTOR_IDENTITY:
		plan.connector_name = "other-connector";
		break;
	case OperationPlanCounterexample::OTHER_CONNECTOR_VERSION:
		plan.connector_version = "999.0.0";
		break;
	case OperationPlanCounterexample::OTHER_RELATION_IDENTITY:
		plan.relation_name = "other_relation";
		break;
	case OperationPlanCounterexample::EMPTY_IDENTITY:
		plan.operation.operation_name.clear();
		break;
	case OperationPlanCounterexample::OTHER_OPERATION_IDENTITY:
		plan.operation.operation_name = "other_operation";
		break;
	case OperationPlanCounterexample::UNKNOWN_METHOD:
		plan.operation.method = static_cast<duckdb_api::PlannedHttpMethod>(127);
		break;
	case OperationPlanCounterexample::EMPTY_PATH:
		plan.operation.path.clear();
		break;
	case OperationPlanCounterexample::OTHER_PATH:
		plan.operation.path = "/other";
		break;
	case OperationPlanCounterexample::INVALID_QUERY:
		plan.operation.query_parameters.push_back({"invalid?query", "fixed-test-value"});
		break;
	case OperationPlanCounterexample::EMPTY_FIXED_HEADER_VALUE:
		plan.operation.headers.push_back({"X-Invalid-Fixture", ""});
		break;
	case OperationPlanCounterexample::CASE_VARIANT_AUTHORIZATION_HEADER:
		plan.operation.headers.push_back({"authorization", "test-only-redacted"});
		break;
	case OperationPlanCounterexample::DUPLICATE_AUTHORIZATION_HEADERS:
		plan.operation.headers.push_back({"Authorization", "test-only-redacted"});
		plan.operation.headers.push_back({"Authorization", "test-only-redacted"});
		break;
	case OperationPlanCounterexample::HTTP_ORIGIN_SCHEME:
		plan.operation.origin.scheme = duckdb_api::PlannedUrlScheme::HTTP;
		break;
	case OperationPlanCounterexample::OTHER_ORIGIN_HOST:
		plan.operation.origin.host = "other.example";
		break;
	case OperationPlanCounterexample::OTHER_ORIGIN_PORT:
		plan.operation.origin.port = 444;
		break;
	default:
		throw std::invalid_argument("unknown closed operation plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan BuildOperationPlanCounterexample(const std::string &exact_logical_secret_name,
                                                      OperationPlanCounterexample counterexample) {
	return ScanPlanTestAccess::Operation(BuildValidAuthenticatedPlanFixture(exact_logical_secret_name), counterexample);
}

} // namespace duckdb_api_test
