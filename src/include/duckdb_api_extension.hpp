#pragma once

#include "duckdb.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/execution.hpp"

#include <memory>

namespace duckdb {

class DuckdbApiExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

// Publish the complete Query Experience surface for one DatabaseInstance.
// Secret type/provider registration completes before duckdb_api_scan becomes
// visible. DuckDB 1.5.4 offers no transaction or unregister across those host
// registries, so a later failure can leave an orphan type/provider; it must
// still leave no scan function whose prerequisite registration failed.
void RegisterDuckdbApi(ExtensionLoader &loader, duckdb_api::CompiledConnector connector,
                       std::shared_ptr<const duckdb_api::ScanExecutor> executor);

} // namespace duckdb
