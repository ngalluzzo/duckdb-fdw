#pragma once

#include "input_resolution.hpp"
#include "predicate_classifier.hpp"

namespace duckdb_api {
namespace operation_selection {

// Checks one compiled namespace tag against its own binding set. Relation
// values and operation-local predicate conditionals may share an identifier;
// the tag, never a string prefix or lookup order, decides which set applies.
// A required reference is satisfied only by one concrete value.
bool RequiredInputIsSatisfied(CompiledRequiredInputKind kind, const std::string &id,
                              const input_resolution::ResolvedRelationInputs &relation_inputs,
                              const predicate_classifier::CandidateInputBindings &conditional_inputs);

// Selects exactly one operation after relation inputs have been resolved.
// Every candidate derives independent conditional bindings; conflicting
// bindings reject only that candidate. V1 selectors use tagged required refs
// and rank by their count. The temporary native fixture bridge preserves its
// legacy selector rules without introducing a new untagged convention.
const CompiledOperation &SelectOperation(const CompiledRelation &relation, const ScanRequest &request,
                                         const input_resolution::ResolvedRelationInputs &relation_inputs);

} // namespace operation_selection
} // namespace duckdb_api
