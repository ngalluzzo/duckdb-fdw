#pragma once

#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <string>

namespace duckdb_api {
namespace predicate_classifier {

// Complete closed decision consumed by ScanPlan construction. The conditional
// input is the only predicate-derived Runtime authority; the reason is safe
// explanation only and must never be parsed by a consumer.
struct PredicatePlanDecision {
	PlannedPredicate remote_predicate;
	RemotePredicateAccuracy remote_accuracy;
	PlannedPredicate residual_predicate;
	RelationalOwner residual_owner;
	PlannedConditionalInput conditional_input;
	std::string reason;
};

PredicatePlanDecision Classify(const CompiledRelation &relation, const ScanRequest &request);

} // namespace predicate_classifier
} // namespace duckdb_api
