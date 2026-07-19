#pragma once

#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <cstdint>
#include <memory>

namespace duckdb_api {
namespace internal {

class CurlProcessLifetime;

// Closed result of the installed transport's final request-policy check. This
// is a Runtime-private inspection boundary used by the transport and focused
// policy tests; it performs no allocation, DNS, credential lookup, or I/O.
enum class InstalledHttpRequestKind : uint8_t {
	UNSUPPORTED,
	ANONYMOUS_SEARCH,
	AUTHENTICATED_USER,
	AUTHENTICATED_REPOSITORIES
};

InstalledHttpRequestKind ClassifyInstalledHttpRequest(const HttpRequest &request) noexcept;

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
