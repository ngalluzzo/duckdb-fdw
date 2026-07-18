#pragma once

#include "duckdb_api/internal/http_transport.hpp"

#include <sys/socket.h>

namespace duckdb_api {
namespace internal {

typedef bool (*CurlSocketPolicy)(const sockaddr *address, socklen_t address_length, const void *context);

// Inputs fixed by a composition wrapper, never by SQL, settings, environment,
// or a ScanPlan. The installed wrapper supplies only the public HTTPS profile;
// a separately linked test-support wrapper supplies the loopback profile.
struct CurlTransferProfile {
	const char *url;
	const char *protocols;
	CurlSocketPolicy socket_policy;
	const void *socket_policy_context;
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
	// Private-link-only trust and deterministic name-resolution inputs. The
	// installed curl object is compiled without this test surface.
	const char *trusted_ca_file;
	const char *resolve_entry;
#endif
};

// Shared curl algorithm used by the installed fixed wrapper and the private
// loopback wrapper. It owns all easy-handle options, byte accounting,
// cancellation, deadline enforcement, status collection, and redaction.
HttpResponse PerformCurlTransfer(const CurlTransferProfile &profile, const HttpRequest &request,
                                 const HttpLimits &limits, ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
