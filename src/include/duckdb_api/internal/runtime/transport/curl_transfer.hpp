#pragma once

#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <sys/socket.h>

#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
#include <curl/curl.h>
#endif

namespace duckdb_api {
namespace internal {

typedef bool (*CurlSocketPolicy)(const sockaddr *address, socklen_t address_length, const void *context);

#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
typedef void (*CurlOptionObserver)(CURLoption option, const char *normalized_value, void *context);
#endif

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
	CurlOptionObserver option_observer;
	void *option_observer_context;
#endif
};

#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
// Present only in private curl-test objects. Enablement asserts that this
// marker is absent from every installed and loadable artifact.
const char *PrivateCurlOptionObserverCanary() noexcept;
#endif

// Shared curl algorithm used by the installed fixed wrapper and the private
// loopback wrapper. It owns all easy-handle options, byte accounting,
// cancellation, deadline enforcement, status collection, and redaction.
HttpResponse PerformCurlTransfer(const CurlTransferProfile &profile, const HttpRequest &request,
                                 const HttpLimits &limits, ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
