#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/execution.hpp"

#include <memory>

namespace duckdb_api {

// Fully assembled internal example handed to the DuckDB adapter. Composition
// freezes connector provenance and hides concrete fixture-provider types.
struct ExampleComposition {
	CompiledConnector connector;
	std::shared_ptr<const ScanExecutor> executor;
};

ExampleComposition BuildEmbeddedExampleComposition();

} // namespace duckdb_api
