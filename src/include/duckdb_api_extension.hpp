#pragma once

#include "duckdb.hpp"
#include "duckdb_api/contracts.hpp"

namespace duckdb {

class DuckdbApiExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

void RegisterDuckdbApi(ExtensionLoader &loader, shared_ptr<duckdb_api::FixtureFactory> fixture_factory);

} // namespace duckdb
