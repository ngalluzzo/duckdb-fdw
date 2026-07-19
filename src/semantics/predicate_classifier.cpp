#include "predicate_classifier.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace duckdb_api {
namespace predicate_classifier {

namespace {

const char VISIBILITY_COLUMN[] = "visibility";
const char VISIBILITY_EXTRACTOR[] = "$.visibility";
const char VISIBILITY_LOGICAL_TYPE[] = "VARCHAR";
const char AUTHENTICATED_REPOSITORIES_OPERATION[] = "github_authenticated_repositories";
const char PRIVATE_VALUE[] = "private";

PlannedPredicate ResidualPredicate(const ScanRequest &request) {
	switch (request.retained_predicate_scope) {
	case RetainedPredicateScope::UNRESTRICTED:
		return PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	case RetainedPredicateScope::REQUESTED_PREDICATE:
		if (request.requested_predicate.Kind() == RequestedPredicateKind::VISIBILITY_EQUALS_PRIVATE) {
			return PlannedPredicate::VISIBILITY_EQUALS_PRIVATE;
		}
		throw std::logic_error("requested residual scope has no closed predicate");
	case RetainedPredicateScope::COMPLETE_DUCKDB_FILTER:
		return PlannedPredicate::COMPLETE_DUCKDB_FILTER;
	}
	throw std::logic_error("predicate classifier received an unknown retained-predicate scope");
}

PredicatePlanDecision UnrestrictedDecision(const ScanRequest &request, std::string reason) {
	return {PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        RemotePredicateAccuracy::UNSUPPORTED,
	        ResidualPredicate(request),
	        RelationalOwner::DUCKDB,
	        PlannedConditionalInput::NONE,
	        std::move(reason)};
}

PredicatePlanDecision VisibilityFallback(const ScanRequest &request, std::string reason) {
	return {PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        RemotePredicateAccuracy::UNSUPPORTED,
	        ResidualPredicate(request),
	        RelationalOwner::DUCKDB,
	        PlannedConditionalInput::NONE,
	        std::move(reason)};
}

bool HasRequiredVisibilityColumn(const CompiledRelation &relation) {
	std::size_t matches = 0;
	for (const auto &column : relation.Columns()) {
		if (column.name != VISIBILITY_COLUMN) {
			continue;
		}
		matches++;
		if (column.logical_type != VISIBILITY_LOGICAL_TYPE || column.nullable ||
		    column.extractor != VISIBILITY_EXTRACTOR) {
			return false;
		}
	}
	return matches == 1;
}

bool IsAcceptedVisibilityMapping(const CompiledRelation &relation, const CompiledPredicateMapping &mapping) {
	return mapping.ColumnName() == VISIBILITY_COLUMN && mapping.Operator() == CompiledPredicateOperator::EQUALS &&
	       mapping.Literal() == CompiledPredicateLiteral::VARCHAR_PRIVATE &&
	       relation.Operation().name == AUTHENTICATED_REPOSITORIES_OPERATION &&
	       mapping.OperationName() == relation.Operation().name &&
	       mapping.InputPlacement() == CompiledPredicateInputPlacement::REST_QUERY_PARAMETER &&
	       mapping.RemoteInputName() == VISIBILITY_COLUMN && mapping.EncodedRemoteValue() == PRIVATE_VALUE &&
	       mapping.Accuracy() == CompiledPredicateAccuracy::SUPERSET &&
	       mapping.Evidence() == CompiledPredicateEvidence::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY;
}

} // namespace

PredicatePlanDecision Classify(const CompiledRelation &relation, const ScanRequest &request) {
	const auto requested_kind = request.requested_predicate.Kind();
	if (requested_kind == RequestedPredicateKind::UNRESTRICTED) {
		if (request.retained_predicate_scope == RetainedPredicateScope::COMPLETE_DUCKDB_FILTER) {
			return UnrestrictedDecision(
			    request,
			    "DuckDB exposed a retained structured filter, but its shape is outside the accepted mapping; the "
			    "complete base-domain traversal is used and DuckDB remains authoritative");
		}
		return UnrestrictedDecision(
		    request, "no selective predicate was requested; the complete base-domain traversal remains authoritative");
	}
	if (requested_kind != RequestedPredicateKind::VISIBILITY_EQUALS_PRIVATE) {
		throw std::logic_error("planner received an unknown requested-predicate state");
	}

	if (!request.capabilities.selective_predicate) {
		return VisibilityFallback(
		    request,
		    "structured selective-predicate capability is unavailable; the complete base-domain traversal is used");
	}
	if (!request.capabilities.retains_predicate) {
		return VisibilityFallback(
		    request, "DuckDB residual-retention capability is unavailable; the complete base-domain traversal is used");
	}
	if (!HasRequiredVisibilityColumn(relation)) {
		return VisibilityFallback(
		    request,
		    "the required visibility response column is unavailable; the complete base-domain traversal is used");
	}
	if (relation.PredicateMappings().size() != 1 ||
	    !IsAcceptedVisibilityMapping(relation, relation.PredicateMappings().front())) {
		return VisibilityFallback(
		    request,
		    "the accepted visibility predicate mapping is unavailable; the complete base-domain traversal is used");
	}

	const auto reason = request.retained_predicate_scope == RetainedPredicateScope::COMPLETE_DUCKDB_FILTER
	                        ? "the required visibility field and reviewed same-field mapping establish D=>R; DuckDB "
	                          "retains the complete structured filter as sole residual owner"
	                        : "the required visibility field and reviewed same-field mapping establish D=>R; DuckDB "
	                          "retains the complete visibility predicate as sole residual owner";
	return {PlannedPredicate::VISIBILITY_EQUALS_PRIVATE,
	        RemotePredicateAccuracy::SUPERSET,
	        ResidualPredicate(request),
	        RelationalOwner::DUCKDB,
	        PlannedConditionalInput::VISIBILITY_PRIVATE,
	        reason};
}

} // namespace predicate_classifier
} // namespace duckdb_api
