#pragma once

#include "duckdb_api/connector_catalog.hpp"

namespace duckdb_api {

// Constructs the exact RFC 0006 catalog deterministically without I/O,
// environment access, package parsing, runtime construction, DuckDB types,
// secret names, or credential values. Generic representation and validation
// live in connector_catalog.hpp so native product metadata has one reason to
// change: the fixed repository-owned catalog.
CompiledConnector BuildNativeGithubConnector();

} // namespace duckdb_api
