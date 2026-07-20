#pragma once

#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <string>
#include <vector>

namespace duckdb_api {
namespace predicate_classifier {

// Complete semantic decision consumed by ScanPlan construction. The typed
// conditional input is the only predicate-derived Runtime authority. Category
// and reason are structured explanation; prose is safe rendering only.
struct PredicatePlanDecision {
	PlannedPredicate remote_predicate;
	RemotePredicateAccuracy remote_accuracy;
	PlannedPredicate residual_predicate;
	RelationalOwner residual_owner;
	PlannedConditionalInput conditional_input;
	PredicateDecisionCategory category;
	PredicateDecisionReason reason_code;
	std::string reason;
};

// One operation-scoped input derived from a validated predicate mapping. This
// is selection evidence only: evaluating an operation candidate does not grant
// Runtime authority or place its translation in ScanPlan. Only Classify() for
// the selected operation can produce PlannedConditionalInput.
struct CandidateInputBinding {
	std::string name;
	std::string encoded_value;
};

struct CandidateInputBindings {
	std::vector<CandidateInputBinding> values;
	bool conflicting;
};

CandidateInputBindings ResolveCandidateInputBindings(const CompiledRelation &relation,
                                                     const CompiledOperation &operation, const ScanRequest &request);

PredicatePlanDecision Classify(const CompiledRelation &relation, const CompiledOperation &operation,
                               const ScanRequest &request);

} // namespace predicate_classifier
} // namespace duckdb_api
