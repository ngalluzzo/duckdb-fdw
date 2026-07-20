#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <vector>

namespace duckdb_api {
namespace internal {

// Connector Experience validates the v1 fallback/when shape and that every
// immutable selector input is representable by the same operation's exact
// compiled declaration namespace. Request-dependent eligibility, specificity,
// and selection remain owned by Relational Semantics.
void ValidateOperationSelectorReferences(const CompiledOperation &operation,
                                         const std::vector<CompiledRelationInput> &relation_inputs,
                                         const std::vector<CompiledPredicateMapping> &mappings);

// Exact structural equality used by package reload compatibility. Keeping the
// tag comparison beside selector declaration prevents a compatibility caller
// from accidentally collapsing relation and conditional namespaces.
bool SameOperationSelectorStructure(const CompiledOperationSelector &left, const CompiledOperationSelector &right);

} // namespace internal
} // namespace duckdb_api
