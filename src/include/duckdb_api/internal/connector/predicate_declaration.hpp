#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <iosfwd>
#include <vector>

namespace duckdb_api {
namespace internal {

// Validates a relation's mapping collection against its declared schema,
// operation, fixed query fields, and pagination bindings. This is Connector
// compilation logic; consumers receive only the validated public const values.
void ValidatePredicateMappings(const std::string &relation_name, const std::vector<CompiledColumn> &columns,
                               const CompiledOperation &operation, const CompiledAuthenticationPolicy &authentication,
                               const std::vector<CompiledPredicateMapping> &mappings);

// Emits safe deterministic declaration facts only. The output is explanation,
// never a parser input or execution authority.
void AppendPredicateMappings(std::ostream &result, const std::vector<CompiledPredicateMapping> &mappings);

} // namespace internal
} // namespace duckdb_api
