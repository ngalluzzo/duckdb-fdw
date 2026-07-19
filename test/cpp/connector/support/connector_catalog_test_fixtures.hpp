#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <cstdint>

namespace duckdb_api_test {

// Stable test-service identifiers for consumers that must prove relation
// selection without depending on the native GitHub names. These values are
// private test compatibility, not package syntax or a public native ABI.
extern const char DISTINCT_SCHEMA_ANONYMOUS_RELATION[];
extern const char DISTINCT_SCHEMA_AUTHENTICATED_RELATION[];
extern const char PAGINATION_DECOY_RELATION[];
extern const char PAGINATION_LINK_RELATION[];

// Returns a deterministic immutable catalog whose two relations deliberately
// differ in name, schema width, column names/types, response shape, and
// credential requirement. Connector Experience owns all construction details;
// Query and Semantics tests consume only this factory and CompiledConnector's
// public const API. The fixture carries logical policy but no secret name or
// credential value and performs no I/O.
duckdb_api::CompiledConnector BuildDistinctSchemaConnectorCatalogFixture();

// Returns two required-credential many-row relations with the same page-like
// request shape. Only one carries an explicit sequential Link declaration.
// Consumers must select the exact service identifier and read CompiledPagination
// rather than infer from query fields, credential requirements, or native names.
duckdb_api::CompiledConnector BuildPaginationConnectorCatalogFixture();

// Returns a one-relation catalog whose pagination and resource envelopes are
// controlled independently. Relational Semantics consumes this Connector-owned
// factory for conservative-planning counterexamples without constructing the
// private catalog model directly.
duckdb_api::CompiledConnector
BuildPaginationPlannerCandidate(std::uint64_t max_pages, std::uint64_t response_bytes_per_page,
                                std::uint64_t response_bytes_per_scan, std::uint64_t records_per_page,
                                std::uint64_t records_per_scan, std::uint64_t extracted_string_bytes);

// Returns the repository-shaped root-array counterexample with pagination
// explicitly disabled. Its GitHub-shaped identity remains fixture-only and
// carries no credential value or execution authority.
duckdb_api::CompiledConnector BuildDisabledRootArrayRepositoryCandidate();

// Valid mapping decoys for consumer tests. Each factory exposes only public
// immutable catalog access and deliberately publishes no predicate mapping:
// the first preserves every native catalog fact except mapping availability,
// the second varies the schema, and the third varies the operation. Consumers
// must not infer capability from relation, column, extractor, operation, or
// request names.
duckdb_api::CompiledConnector BuildPredicateMappingAbsentCatalogFixture();
duckdb_api::CompiledConnector BuildPredicateSchemaVariationCatalogFixture();
duckdb_api::CompiledConnector BuildPredicateOperationVariationCatalogFixture();

} // namespace duckdb_api_test
