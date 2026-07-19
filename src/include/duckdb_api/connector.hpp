#pragma once

#include "duckdb_api/connector_catalog.hpp"

namespace duckdb_api {

// Constructs the exact RFC 0007 native 0.5.0 catalog deterministically without I/O,
// environment access, package parsing, runtime construction, DuckDB types,
// secret names, credential values, or received Link state. Generic
// representation and validation live behind connector_catalog.hpp so native
// product metadata has one reason to change: the fixed repository-owned
// catalog and its immutable bounded pagination declarations.
CompiledConnector BuildNativeGithubConnector();

} // namespace duckdb_api
