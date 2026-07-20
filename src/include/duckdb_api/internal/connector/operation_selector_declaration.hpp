#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <vector>

namespace duckdb_api {
namespace internal {

// Connector Experience validates that every immutable selector input is
// representable by the same operation's compiled input declarations. Request-
// dependent eligibility, specificity, priority, and selection remain owned by
// Relational Semantics.
void ValidateOperationSelectorReferences(const CompiledOperation &operation,
                                         const std::vector<CompiledRelationInput> &relation_inputs,
                                         const std::vector<CompiledPredicateMapping> &mappings);

} // namespace internal
} // namespace duckdb_api
