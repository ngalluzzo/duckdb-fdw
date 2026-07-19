#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

// Closed Remote Runtime service for RFC 0007's authenticated repository
// profile. Plan admission occurs before this construction boundary. Open owns
// one moved authorization capability and returns the ordinary BatchStream team
// API; no pagination type crosses into Query Experience.
std::unique_ptr<BatchStream> OpenAuthenticatedRepositoriesScan(const ScanPlan &plan, ScanAuthorization authorization,
                                                               std::shared_ptr<const HttpTransport> transport,
                                                               uint64_t max_wall_milliseconds,
                                                               ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
