#pragma once

#include "duckdb_api/scan_planner.hpp"
#include "input_resolution.hpp"
#include "predicate_classifier.hpp"

namespace duckdb_api {

// Sole production constructor for immutable ScanPlan values. Keeping request
// materialization behind this friend preserves PlannedRestQueryBinding's
// private construction while letting each protocol adapter remain a focused
// module with one reason to change.
class ScanPlanBuilder {
public:
	static ScanPlan Build(const CompiledConnector &connector, const ScanRequest &request);

private:
	static PlannedRestOperation
	BuildRestOperation(CompiledConnectorOrigin connector_origin, const CompiledRelation &relation,
	                   const CompiledOperation &operation,
	                   const input_resolution::ResolvedRelationInputs &relation_inputs,
	                   const predicate_classifier::PredicatePlanDecision &predicate_decision);
};

} // namespace duckdb_api
