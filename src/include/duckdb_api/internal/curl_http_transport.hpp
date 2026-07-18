#pragma once

#include "duckdb_api/internal/http_transport.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

class CurlProcessLifetime;

// Performs one checked process-global initialization, then safely inspects the
// initialized runtime identity. A rejected identity is balanced immediately.
// An accepted owner and global state are intentionally process-resident and
// are never cleaned by service, extension, or atexit teardown.
const CurlProcessLifetime *AcquireCurlProcessLifetime();

// Constructs the production fixed-authority transport. It admits only the two
// compiled-in GitHub request profiles and never derives a URL or credential
// placement from caller text. The process-lifetime token proves initialization
// completed before any easy handle can be built.
std::unique_ptr<HttpTransport> BuildCurlHttpTransport(const CurlProcessLifetime *lifetime);

} // namespace internal
} // namespace duckdb_api
