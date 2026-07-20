#pragma once

#include "duckdb_api/connector_catalog.hpp"

namespace duckdb_api {

// Constructs the exact RFC 0005 through RFC 0010 native 0.6.0 catalog
// deterministically without I/O, environment access, package parsing, runtime
// construction, DuckDB types, secret names, credential values, or received
// Link state. The sole installed mapping keeps its reviewed GitHub superset
// proof/domain/occurrence/encoding profile; the distinct exact profile remains
// in the non-installable Connector fixture service. Generic representation and
// validation live behind connector_catalog.hpp so native product metadata has
// one reason to change: the fixed repository-owned catalog and its immutable
// bounded declarations.
CompiledConnector BuildNativeGithubConnector();

} // namespace duckdb_api
