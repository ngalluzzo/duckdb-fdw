#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/query_generation.hpp"

#include <memory>

namespace duckdb_api {

// Complete installed product assembled from provider team APIs. The DuckDB
// entry point consumes this immutable value without learning connector
// construction or Remote Runtime implementation details.
struct ProductComposition {
	CompiledConnector connector;
	std::shared_ptr<const ScanExecutor> executor;
	std::shared_ptr<const QueryPackageStagingService> package_staging;
};

// Builds the sole installed native composition. Connector supplies the
// immutable four-relation catalog and Runtime supplies the bounded
// anonymous-or-bearer executor service; Query assembles them without mutating
// either provider, inspecting a protocol alternative, or retaining credential
// state. Runtime initialization is checked before DuckDB registers the
// function, and this path has no authority or test-scenario override. The
// package staging service consumes the same executor while retaining its own
// database-scoped generation registry. Database teardown closes Query
// publication first, then that registry, then this shared executor; the
// executor's reference-counted stream state outlives only for bounded release.
ProductComposition BuildProductComposition();

} // namespace duckdb_api
