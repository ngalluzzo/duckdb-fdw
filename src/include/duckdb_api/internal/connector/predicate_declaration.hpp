#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <iosfwd>
#include <vector>

namespace duckdb_api {
namespace internal {

struct CompiledPackagePredicateIdentities {
	std::string proof;
	std::string base_domain;
};

// Derives unforgeable normalized identities from the exact package generation
// and operation structure. The compiler calls this before mapping construction;
// PackageGeneration recomputes it after all immutable values are assembled.
CompiledPackagePredicateIdentities DerivePackagePredicateIdentities(const std::string &package_digest,
                                                                    const std::string &relation_name,
                                                                    const CompiledOperation &operation);

// Validates a relation's mapping collection against its declared schema,
// operations, fixed query fields, and pagination bindings. This is Connector
// compilation logic; consumers receive only the validated public const values.
void ValidatePredicateMappings(const std::string &relation_name, const std::vector<CompiledColumn> &columns,
                               const std::vector<CompiledOperation> &operations,
                               const CompiledAuthenticationPolicy &authentication,
                               const std::vector<CompiledPredicateMapping> &mappings);

// Emits safe deterministic declaration facts only. The output is explanation,
// never a parser input or execution authority.
void AppendPredicateMappings(std::ostream &result, const std::vector<CompiledPredicateMapping> &mappings);

} // namespace internal
} // namespace duckdb_api
