#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/execution.hpp"

#include <memory>

namespace duckdb_api {

// Complete installed product assembled from provider team APIs. The DuckDB
// entry point consumes this immutable value without learning connector
// construction or Remote Runtime implementation details.
struct ProductComposition {
	CompiledConnector connector;
	std::shared_ptr<const ScanExecutor> executor;
};

// Builds the sole installed RFC 0005/0006 composition. Connector supplies the
// immutable two-relation catalog and Runtime supplies the anonymous-or-bearer
// executor service; Query assembles them without mutating either provider or
// retaining credential state. Runtime initialization is checked before DuckDB
// registers the function, and this path has no authority or test-scenario
// override.
ProductComposition BuildProductComposition();

} // namespace duckdb_api
