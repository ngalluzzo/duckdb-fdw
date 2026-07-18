#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/http_transport.hpp"

#include <cstdint>
#include <string>

namespace duckdb_api_test {

enum class PrivateCurlSocketPolicy { ALLOW_LOOPBACK_PORT, DENY_ALL };

struct PrivateCurlProbeOptions {
	std::string url;
	std::string protocols;
	std::string request_scheme;
	std::string request_host;
	uint16_t request_port;
	std::string trusted_ca_file;
	std::string resolve_entry;
	PrivateCurlSocketPolicy socket_policy;
	uint64_t wall_milliseconds;
	uint64_t *completed_socket_policy_checks;
};

struct PrivateCurlProbeResult {
	duckdb_api::internal::HttpResponse response;
	uint64_t socket_policy_checks;
};

// Calls the shared curl algorithm through a private-link-only profile. This
// support object is compiled with DUCKDB_API_PRIVATE_CURL_TESTS and must never
// be included in an installed or loadable target.
PrivateCurlProbeResult PerformPrivateCurlProbe(const PrivateCurlProbeOptions &options,
                                               duckdb_api::ExecutionControl &control);

} // namespace duckdb_api_test
