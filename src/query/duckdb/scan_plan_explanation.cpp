#include "scan_plan_explanation.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <string>

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

const char *PredicateName(duckdb_api::PlannedPredicate predicate) {
	switch (predicate) {
	case duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN:
		return "unrestricted";
	case duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE:
		return "visibility_equals_private";
	case duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER:
		return "complete_duckdb_filter";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate state");
}

const char *AccuracyName(duckdb_api::RemotePredicateAccuracy accuracy) {
	switch (accuracy) {
	case duckdb_api::RemotePredicateAccuracy::UNSUPPORTED:
		return "unsupported";
	case duckdb_api::RemotePredicateAccuracy::SUPERSET:
		return "superset";
	case duckdb_api::RemotePredicateAccuracy::EXACT:
		return "exact";
	}
	throw InternalException("duckdb_api scan plan contains an unknown remote accuracy");
}

const char *OwnerName(duckdb_api::RelationalOwner owner) {
	switch (owner) {
	case duckdb_api::RelationalOwner::DUCKDB:
		return "duckdb";
	}
	throw InternalException("duckdb_api scan plan contains an unknown relational owner");
}

const char *DelegationName(duckdb_api::RelationalDelegation delegation) {
	switch (delegation) {
	case duckdb_api::RelationalDelegation::NONE:
		return "none";
	}
	throw InternalException("duckdb_api scan plan contains an unknown relational delegation");
}

const char *CategoryName(duckdb_api::PredicateDecisionCategory category) {
	switch (category) {
	case duckdb_api::PredicateDecisionCategory::EXACT:
		return "exact";
	case duckdb_api::PredicateDecisionCategory::SUPERSET:
		return "superset";
	case duckdb_api::PredicateDecisionCategory::UNSUPPORTED:
		return "unsupported";
	case duckdb_api::PredicateDecisionCategory::AMBIGUOUS:
		return "ambiguous";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate category");
}

const char *ReasonName(duckdb_api::PredicateDecisionReason reason) {
	switch (reason) {
	case duckdb_api::PredicateDecisionReason::NO_REMOTE_CANDIDATE:
		return "no_remote_candidate";
	case duckdb_api::PredicateDecisionReason::SELECTED_EXACT_MAPPING:
		return "selected_exact_mapping";
	case duckdb_api::PredicateDecisionReason::SELECTED_SUPERSET_MAPPING:
		return "selected_superset_mapping";
	case duckdb_api::PredicateDecisionReason::STRUCTURE_UNSUPPORTED:
		return "structure_unsupported";
	case duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE:
		return "capability_unavailable";
	case duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE:
		return "mapping_unavailable";
	case duckdb_api::PredicateDecisionReason::DISJUNCTION_ENCODING_UNAVAILABLE:
		return "disjunction_encoding_unavailable";
	case duckdb_api::PredicateDecisionReason::COMPLEMENT_ENCODING_UNAVAILABLE:
		return "complement_encoding_unavailable";
	case duckdb_api::PredicateDecisionReason::AMBIGUOUS_CONDITIONAL_INPUT:
		return "ambiguous_conditional_input";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate reason");
}

const char *ScopeName(duckdb_api::RetainedPredicateScope scope) {
	switch (scope) {
	case duckdb_api::RetainedPredicateScope::UNRESTRICTED:
		return "unrestricted";
	case duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE:
		return "requested_predicate";
	case duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER:
		return "complete_duckdb_filter";
	}
	throw InternalException("duckdb_api scan request contains an unknown retained-filter scope");
}

std::string ProjectionClosure(const duckdb_api::ScanPlan &plan) {
	std::string result;
	for (std::size_t index = 0; index < plan.OutputColumns().size(); index++) {
		if (index > 0) {
			result += ',';
		}
		result += plan.OutputColumns()[index].name;
	}
	return result;
}

const char *CapabilityName(bool available) {
	return available ? "available" : "unavailable";
}

} // namespace

InsertionOrderPreservingMap<string> ExplainSelectedScan(const duckdb_api::ScanRequest &request,
                                                        const duckdb_api::ScanPlan &plan) {
	InsertionOrderPreservingMap<string> result;
	result["Relation"] = plan.RelationName();
	result["Candidate"] = request.requested_predicate.Snapshot();
	result["Remote Predicate"] = PredicateName(plan.RemotePredicate());
	result["Remote Accuracy"] = AccuracyName(plan.RemoteAccuracy());
	result["Offered Filter Scope"] = ScopeName(request.retained_predicate_scope);
	result["Filter Action"] = "retained";
	result["Residual Predicate"] = PredicateName(plan.ResidualPredicate());
	result["Residual Owner"] = OwnerName(plan.ResidualOwner());
	result["Filter Owner"] = OwnerName(plan.Ownership().filter);
	result["Projection Closure"] = ProjectionClosure(plan);
	result["Projection Owner"] = OwnerName(plan.Ownership().projection);
	result["Ordering Owner"] = OwnerName(plan.Ownership().ordering);
	result["Limit Owner"] = OwnerName(plan.Ownership().limit);
	result["Offset Owner"] = OwnerName(plan.Ownership().offset);
	result["Remote Ordering"] = DelegationName(plan.RemoteOrdering());
	result["Runtime Ordering"] = DelegationName(plan.RuntimeOrdering());
	result["Remote Limit"] = DelegationName(plan.RemoteLimit());
	result["Runtime Limit"] = DelegationName(plan.RuntimeLimit());
	result["Remote Offset"] = DelegationName(plan.RemoteOffset());
	result["Runtime Offset"] = DelegationName(plan.RuntimeOffset());
	result["Projection Metadata"] = CapabilityName(request.capabilities.projection);
	result["Generic Filter Execution"] = CapabilityName(request.capabilities.filter);
	result["Candidate Inspection"] = CapabilityName(request.capabilities.selective_predicate);
	result["DuckDB Residual Retention"] = request.capabilities.retains_predicate ? "verified" : "unavailable";
	result["Ordering Metadata"] = CapabilityName(request.capabilities.ordering);
	result["Limit Metadata"] = CapabilityName(request.capabilities.limit);
	result["Offset Metadata"] = CapabilityName(request.capabilities.offset);
	result["Classification Category"] = CategoryName(plan.PredicateCategory());
	result["Classification Reason"] = ReasonName(plan.PredicateReason());
	result["Classification Detail"] = plan.ClassificationReason();
	return result;
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
