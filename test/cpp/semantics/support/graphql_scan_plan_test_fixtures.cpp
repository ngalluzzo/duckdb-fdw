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

duckdb_api::ScanPlan ScanPlanTestAccess::Graphql(duckdb_api::ScanPlan plan, GraphqlPlanCounterexample counterexample) {
	switch (counterexample) {
	case GraphqlPlanCounterexample::OTHER_DOCUMENT_IDENTITY: {
		auto operation = plan.Operation().Graphql();
		operation.document_identity = static_cast<duckdb_api::PlannedGraphqlDocumentIdentity>(127);
		ReplaceGraphql(plan, std::move(operation));
		break;
	}
	case GraphqlPlanCounterexample::OTHER_DOCUMENT_DIGEST: {
		auto operation = plan.Operation().Graphql();
		operation.document_digest = "other-digest";
		ReplaceGraphql(plan, std::move(operation));
		break;
	}
	case GraphqlPlanCounterexample::OTHER_DOMAIN:
		plan.domain = duckdb_api::BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS;
		break;
	case GraphqlPlanCounterexample::UNKNOWN_REPLAY_SAFETY: {
		auto operation = plan.Operation().Graphql();
		operation.replay_safety = static_cast<duckdb_api::PlannedReplaySafety>(127);
		ReplaceGraphql(plan, std::move(operation));
		break;
	}
	case GraphqlPlanCounterexample::OTHER_CURSOR_PAGE_SIZE:
		plan.pagination.graphql_cursor.page_size = 99;
		break;
	case GraphqlPlanCounterexample::ZERO_PAGE_BODY_BUDGET:
		plan.pagination.page_budgets.serialized_request_body_bytes = 0;
		break;
	case GraphqlPlanCounterexample::RUNTIME_ORDERING_DELEGATED:
		plan.runtime_ordering = static_cast<duckdb_api::RelationalDelegation>(127);
		break;
	case GraphqlPlanCounterexample::PRIMARY_LANGUAGE_REQUIRED:
		plan.output_columns[4].nullable = false;
		break;
	default:
		throw std::invalid_argument("unknown closed GraphQL plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixture(const std::string &exact_logical_secret_name) {
	return BuildValidGraphqlScanPlanFixtureImpl(exact_logical_secret_name);
}

duckdb_api::ScanPlan BuildGraphqlPlanCounterexample(const std::string &exact_logical_secret_name,
                                                    GraphqlPlanCounterexample counterexample) {
	return ScanPlanTestAccess::Graphql(BuildValidGraphqlScanPlanFixtureImpl(exact_logical_secret_name), counterexample);
}

} // namespace duckdb_api_test
