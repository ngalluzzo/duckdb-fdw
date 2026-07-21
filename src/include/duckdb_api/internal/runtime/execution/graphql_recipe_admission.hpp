#pragma once

#include "duckdb_api/planned_graphql_generator_recipe.hpp"

#include <cstdint>
#include <string>

namespace duckdb_api {
namespace internal {

// Independently validates and renders Semantics' immutable package recipe
// using the accepted v1 grammar. Success returns the exact bounded document;
// false grants no partial authority. This service consumes no Connector type,
// source bytes, package identity, or renderer implementation.
bool TryRenderPackageGraphqlRecipe(const PlannedGraphqlGeneratorRecipe &recipe, uint64_t max_rendered_bytes,
                                   std::string &document);

} // namespace internal
} // namespace duckdb_api
