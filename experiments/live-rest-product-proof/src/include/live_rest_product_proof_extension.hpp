#pragma once

#include "duckdb.hpp"
#include "live_rest/runtime.hpp"

#include <memory>

namespace duckdb {

class LiveRestProductProofExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

// Constructs the bounded trial transport. It owns one libcurl easy handle per
// request and retains no DuckDB context or database state.
std::unique_ptr<live_rest::HttpTransport> BuildCurlHttpTransport();

} // namespace duckdb
