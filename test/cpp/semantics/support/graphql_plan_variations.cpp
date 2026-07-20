#include "semantics/support/graphql_plan_test_variations.hpp"

#include "semantics/support/scan_plan_test_access.hpp"

#include <stdexcept>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::GraphqlVariation(duckdb_api::ScanPlan plan, GraphqlPlanVariation variation) {
	switch (variation) {
	case GraphqlPlanVariation::OTHER_CONNECTOR_NAME:
		plan.connector_name = "other_connector";
		break;
	case GraphqlPlanVariation::OTHER_CONNECTOR_VERSION:
		plan.connector_version = "other-version";
		break;
	case GraphqlPlanVariation::OTHER_RELATION_NAME:
		plan.relation_name = "other_relation";
		break;
	case GraphqlPlanVariation::OTHER_SECRET_REFERENCE:
		plan.secret_reference = duckdb_api::PlannedSecretReference("other_secret");
		break;
	case GraphqlPlanVariation::OTHER_CLASSIFICATION_REASON:
		plan.classification_reason = "other safe explanation";
		break;
	default:
		throw std::invalid_argument("unknown internal GraphQL plan variation");
	}
	return plan;
}

duckdb_api::ScanPlan BuildGraphqlPlanVariation(const std::string &exact_logical_secret_name,
                                               GraphqlPlanVariation variation) {
	return ScanPlanTestAccess::GraphqlVariation(BuildValidGraphqlScanPlanFixture(exact_logical_secret_name), variation);
}

} // namespace duckdb_api_test
