#pragma once

#include "duckdb_api/execution.hpp"

#include <memory>
#include <string>

namespace duckdb_api {

struct HttpRuntimeIdentity {
	std::string libcurl_version;
	std::string ssl_backend;
	bool thread_safe;
};

struct HttpRuntimeService {
	std::shared_ptr<const ScanExecutor> executor;
	HttpRuntimeIdentity identity;
};

// Query Experience calls this once before registering any table function. It
// verifies the exact supported libcurl cell and performs checked process-global
// initialization. The returned executor has no authority override. A process-
// resident owner keeps one balanced cleanup after all executors and streams;
// dynamic extension unload/reload is unsupported by the 0.3.0 profile.
HttpRuntimeService InitializeHttpRuntime();

} // namespace duckdb_api
