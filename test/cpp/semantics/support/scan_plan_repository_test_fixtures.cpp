#include "semantics/support/scan_plan_test_access.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::Repository(duckdb_api::ScanPlan plan,
                                                    RepositoryPlanCounterexample counterexample) {
	switch (counterexample) {
	case RepositoryPlanCounterexample::MISSING_VISIBILITY_COLUMN:
		plan.output_columns.pop_back();
		break;
	case RepositoryPlanCounterexample::VISIBILITY_NOT_TRAILING:
		std::swap(plan.output_columns[4], plan.output_columns[5]);
		break;
	case RepositoryPlanCounterexample::VISIBILITY_NULLABLE:
		plan.output_columns.back().nullable = true;
		break;
	case RepositoryPlanCounterexample::VISIBILITY_WRONG_TYPE:
		plan.output_columns.back().logical_type = "BOOLEAN";
		break;
	case RepositoryPlanCounterexample::VISIBILITY_WRONG_EXTRACTOR:
		plan.output_columns.back().extractor = "$.private";
		break;
	case RepositoryPlanCounterexample::SELECTIVE_REMOTE_TRUE:
		plan.remote_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
		break;
	case RepositoryPlanCounterexample::SELECTIVE_ACCURACY_UNSUPPORTED:
		plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::UNSUPPORTED;
		break;
	case RepositoryPlanCounterexample::SELECTIVE_RESIDUAL_TRUE:
		plan.residual_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
		break;
	case RepositoryPlanCounterexample::SELECTIVE_RESIDUAL_OWNER_UNKNOWN:
		plan.residual_owner = static_cast<duckdb_api::RelationalOwner>(255);
		break;
	case RepositoryPlanCounterexample::SELECTIVE_FILTER_OWNER_UNKNOWN:
		plan.ownership.filter = static_cast<duckdb_api::RelationalOwner>(255);
		break;
	case RepositoryPlanCounterexample::SELECTIVE_PROJECTION_OWNER_UNKNOWN:
		plan.ownership.projection = static_cast<duckdb_api::RelationalOwner>(255);
		break;
	case RepositoryPlanCounterexample::SELECTIVE_REMOTE_ORDERING_UNKNOWN:
		plan.remote_ordering = static_cast<duckdb_api::RelationalDelegation>(255);
		break;
	case RepositoryPlanCounterexample::UNKNOWN_CONDITIONAL_INPUT:
		plan.conditional_input = static_cast<duckdb_api::PlannedConditionalInput>(255);
		break;
	case RepositoryPlanCounterexample::BASELINE_REMOTE_VISIBILITY:
		plan.remote_predicate = duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE;
		break;
	case RepositoryPlanCounterexample::UNKNOWN_PREDICATE_CATEGORY:
		plan.predicate_category = static_cast<duckdb_api::PredicateDecisionCategory>(255);
		break;
	case RepositoryPlanCounterexample::UNKNOWN_PREDICATE_REASON:
		plan.predicate_reason = static_cast<duckdb_api::PredicateDecisionReason>(255);
		break;
	case RepositoryPlanCounterexample::EXACT_CATEGORY_SUPERSET_ACCURACY:
		plan.predicate_category = duckdb_api::PredicateDecisionCategory::EXACT;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::SELECTED_EXACT_MAPPING;
		break;
	case RepositoryPlanCounterexample::SUPERSET_CATEGORY_EXACT_ACCURACY:
		plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::EXACT;
		break;
	case RepositoryPlanCounterexample::AMBIGUOUS_RESIDUAL_TRUE:
		plan.residual_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
		break;
	case RepositoryPlanCounterexample::MAPPING_UNAVAILABLE_RESIDUAL_TRUE:
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE;
		plan.residual_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
		break;
	default:
		throw std::invalid_argument("unknown closed repository plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan BuildRepositoryPlanCounterexample(const std::string &exact_logical_secret_name,
                                                       RepositoryPlanCounterexample counterexample) {
	const bool baseline = counterexample == RepositoryPlanCounterexample::BASELINE_REMOTE_VISIBILITY;
	const bool ambiguous = counterexample == RepositoryPlanCounterexample::AMBIGUOUS_RESIDUAL_TRUE;
	const bool mapping_unavailable = counterexample == RepositoryPlanCounterexample::MAPPING_UNAVAILABLE_RESIDUAL_TRUE;
	auto plan = baseline              ? BuildValidAuthenticatedRepositoriesPlanFixture(exact_logical_secret_name)
	            : ambiguous           ? BuildAmbiguousPredicateFallbackPlanFixture(exact_logical_secret_name)
	            : mapping_unavailable ? BuildCompleteResidualFallbackPlanFixture(exact_logical_secret_name)
	                                  : BuildVisibilityPrivatePlanFixture(exact_logical_secret_name);
	return ScanPlanTestAccess::Repository(std::move(plan), counterexample);
}

} // namespace duckdb_api_test
