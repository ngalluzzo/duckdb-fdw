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

// Constructs the production fixed-authority transport. It admits only the
// installed GitHub profiles. Repository page targets are already canonical,
// typed-state reconstructions; the transport revalidates them before joining
// them to its fixed authority. The process-lifetime token proves initialization
// completed before any easy handle can be built.
std::unique_ptr<HttpTransport> BuildCurlHttpTransport(const CurlProcessLifetime *lifetime);

} // namespace internal
} // namespace duckdb_api
