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

void RegisterDuckdbApi(ExtensionLoader &loader, duckdb_api::CompiledConnector connector,
                       std::shared_ptr<const duckdb_api::ScanExecutor> executor);

} // namespace duckdb
