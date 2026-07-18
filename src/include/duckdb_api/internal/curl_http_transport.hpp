#pragma once

#include "duckdb_api/internal/http_transport.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

class CurlProcessLifetime;

// Constructs the production transport after checked process-global libcurl
// initialization. The lifetime token keeps cleanup after every easy handle and
// stream even during process teardown.
std::unique_ptr<HttpTransport> BuildCurlHttpTransport(std::shared_ptr<const CurlProcessLifetime> lifetime);

} // namespace internal
} // namespace duckdb_api
