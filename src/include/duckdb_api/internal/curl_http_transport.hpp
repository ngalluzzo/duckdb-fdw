#pragma once

#include "duckdb_api/internal/http_transport.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

class CurlProcessLifetime;

// Performs one checked process-global initialization, then safely inspects the
// initialized runtime identity. A rejected identity is balanced immediately;
// an accepted owner is intentionally process-resident and is never destroyed
// by service or extension teardown.
const CurlProcessLifetime *AcquireCurlProcessLifetime();

// Constructs the production fixed-authority transport. The process-lifetime
// token proves initialization completed before any easy handle can be built.
std::unique_ptr<HttpTransport> BuildCurlHttpTransport(const CurlProcessLifetime *lifetime);

} // namespace internal
} // namespace duckdb_api
