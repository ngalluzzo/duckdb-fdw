#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/query_generation.hpp"

#include <memory>

namespace duckdb_api {

// Composes Connector compilation/custody, generation-bound Semantics
// planning, Runtime generation admission, and a caller-supplied Runtime
// executor behind Query's single staging port. The executor remains a bounded
// provider API, so installed and deterministic test transports use the same
// package lifecycle without exposing transport implementation to Query. Close
// rejects new Runtime generation publication before it closes the shared
// executor; retained streams remain release-safe under ScanExecutor's lifecycle
// contract.
std::shared_ptr<const QueryPackageStagingService>
BuildPackageGenerationComposition(std::shared_ptr<const ScanExecutor> executor);

} // namespace duckdb_api
