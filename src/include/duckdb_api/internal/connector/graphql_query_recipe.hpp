#pragma once

#include "duckdb_api/compiled_protocol_operation.hpp"

#include <string>

namespace duckdb_api {
namespace internal {

// Connector-owned canonical renderer for the immutable compiled recipe. It is
// deterministic, thread-safe, and performs no I/O. Invalid or over-budget
// recipes fail before an operation can enter a catalog generation.
std::string RenderCompiledGraphqlQueryRecipe(const CompiledGraphqlQueryRecipe &recipe);
void ValidateCompiledGraphqlQueryRecipe(const CompiledGraphqlQueryRecipe &recipe);

} // namespace internal
} // namespace duckdb_api
