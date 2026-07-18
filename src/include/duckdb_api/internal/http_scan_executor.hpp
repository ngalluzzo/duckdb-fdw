#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/http_transport.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

// Private construction boundary used by production composition and focused
// deterministic runtime tests. Transport ownership becomes immutable and is
// shared only with independently owned streams opened by the executor.
std::shared_ptr<const ScanExecutor> BuildHttpScanExecutor(std::unique_ptr<HttpTransport> transport);

} // namespace internal
} // namespace duckdb_api
