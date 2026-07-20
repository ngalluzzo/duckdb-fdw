#pragma once

#include "duckdb_api/query_generation.hpp"

#include <memory>

namespace duckdb {

class ExtensionLoader;

namespace duckdb_api_query_internal {

class CatalogGenerationCoordinator;

// Installs the initial immutable package snapshot and its database-lifetime
// sentry. The returned coordinator is Query-internal observability for focused
// lifecycle tests; product composition uses only the public void facade.
std::shared_ptr<CatalogGenerationCoordinator>
RegisterPackageSurfaceInternal(ExtensionLoader &loader,
                               std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging);

} // namespace duckdb_api_query_internal
} // namespace duckdb
