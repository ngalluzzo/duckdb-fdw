#pragma once

#include "duckdb.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/query_generation.hpp"

#include <memory>

namespace duckdb {

class DuckdbApiExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

// Registers the retired generic dispatcher (`duckdb_api_scan`) against an
// isolated ExtensionLoader. Accepted RFC 0012 removed this dispatcher from
// the installed product in `0.9.0`; `LoadProduct` no longer calls it. It
// remains solely as test-only composition, letting focused adapter, auth,
// and lifecycle tests exercise the shared bind/execution plumbing without
// standing up a full connector package. Secret type/provider registration
// completes before the scan function becomes visible. DuckDB 1.5.4 offers no
// transaction or unregister across those host registries, so a later failure
// can leave an orphan type/provider; it must still leave no scan function
// whose prerequisite registration failed.
void RegisterDuckdbApi(ExtensionLoader &loader, duckdb_api::CompiledConnector connector,
                       std::shared_ptr<const duckdb_api::ScanExecutor> executor);

// Registers the Query-owned local-package management, introspection, catalog
// coordinator, and DatabaseInstance lifecycle surface. Lead composition
// supplies the local Connector/Runtime staging port; ordinary relation bind
// and execution never call it or read package source.
void RegisterDuckdbApiPackageSurface(ExtensionLoader &loader,
                                     std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging);

} // namespace duckdb
