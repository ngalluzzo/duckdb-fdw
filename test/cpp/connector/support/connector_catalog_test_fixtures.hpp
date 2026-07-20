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
extern const char PREDICATE_EXACT_RELATION[];
extern const char PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION[];
extern const char PREDICATE_AMBIGUOUS_MAPPINGS_RELATION[];
extern const char OPERATION_UNIQUE_WINNER_RELATION[];
extern const char OPERATION_FALLBACK_RELATION[];
extern const char GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION[];

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

// Returns a one-relation, non-installable controlled catalog whose visibility
// mapping has a distinct exact proof identity. The root-array operation and
// mapping pass the same production Connector validation as native metadata;
// duplicate-sensitive rows are supplied by consumer law/product oracles. The
// relation keeps `visibility` as required VARCHAR output and can encode exactly
// one positive `visibility=private` input, with no compound Boolean encoding.
duckdb_api::CompiledConnector BuildExactPredicateCatalogFixture();

// Returns a controlled relation with two structurally valid fallback base
// operations carrying equal eligibility facts. Connector validates and exposes
// their stable declaration order but does not select, rank, or break the tie;
// Relational Semantics owns the deterministic operation-selection failure.
duckdb_api::CompiledConnector BuildEqualRankedOperationsCatalogFixture();

// The unique-winner service exposes two non-fallback exact operations with
// equally specific operation-scoped `visibility` inputs and distinct
// priorities, plus one fallback. The fallback service exposes one required-
// input candidate plus one fallback and is consumed without that binding.
// Connector supplies validated facts and never performs request-dependent
// selection.
duckdb_api::CompiledConnector BuildUniqueWinnerOperationsCatalogFixture();
duckdb_api::CompiledConnector BuildFallbackOperationsCatalogFixture();

// Returns the controlled exact relation with two individually validated safe
// encodings for the same predicate on its sole operation. The mappings bind
// distinct conditional input names; Connector does not choose between them,
// allowing Relational Semantics to produce its explicit Ambiguous fallback.
duckdb_api::CompiledConnector BuildAmbiguousPredicateMappingsCatalogFixture();

// Invalid controlled construction probes. Each factory must throw
// std::invalid_argument before a catalog escapes: one selector both requires
// and forbids the same input, and one relation declares two fallbacks.
duckdb_api::CompiledConnector BuildContradictorySelectorCatalogFixture();
duckdb_api::CompiledConnector BuildMultipleFallbackOperationsCatalogFixture();

// Non-installable canonical GraphQL provider fixture. It exposes one stable
// relation identifier and an immutable factory result through the public
// Connector facade only. Construction reuses production canonical document
// facts and validators, performs no I/O, and carries no secret name, credential,
// cursor value, request body, response row, or runtime state. Consumers must
// not import catalog_test_access.hpp or Connector implementation files.
duckdb_api::CompiledConnector BuildCanonicalGraphqlConnectorCatalogFixture();

} // namespace duckdb_api_test
