#pragma once

#include "duckdb_api/connector_catalog.hpp"

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

} // namespace duckdb_api_test
