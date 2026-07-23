#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace duckdb_api {
namespace internal {

// Private construction boundary used by production composition and focused
// deterministic runtime tests. Transport ownership becomes immutable and is
// shared only with independently owned streams opened by the executor.
std::shared_ptr<const ScanExecutor> BuildHttpScanExecutor(std::unique_ptr<HttpTransport> transport);

// Closed construction-time host ceilings for a privately composed executor.
// An empty host paired with port zero admits any exact safe DNS destination
// carried by a fully validated plan; a non-empty host and nonzero port narrow
// that authority to one origin for focused fixtures. The installed factory is
// destination-neutral and always denies private, link-local, and loopback
// addresses. No SQL, setting, environment value, or per-scan input constructs
// or widens these ceilings.
struct HttpExecutionProfile {
	PlannedUrlScheme scheme;
	std::string host;
	uint16_t port;
	bool private_addresses_enabled;
	bool link_local_addresses_enabled;
	bool loopback_addresses_enabled;
	uint64_t max_wall_milliseconds;
	// Per-response record authority. The installed compatibility profile admits
	// up to the v1 per-page ceiling; private fixtures may narrow this value.
	uint64_t max_decoded_records;
	uint64_t max_retry_attempts_per_step;
	uint64_t max_retry_attempts_per_scan;
	uint64_t max_retry_delay_milliseconds;
	uint64_t max_retry_waiting_milliseconds_per_scan;
};

// Shared origin/network intersection used by protocol-specific admission. It
// validates the exact typed HTTPS destination and deny-only address policy;
// provenance and reconstructed URL strings are deliberately absent.
bool IsOriginAllowedByExecutionProfile(const PlannedHttpOrigin &origin, const HttpExecutionProfile &profile) noexcept;
bool HasExactNetworkCapability(const NetworkCapability &network, const PlannedHttpOrigin &origin,
                               const HttpExecutionProfile &profile) noexcept;

std::shared_ptr<const ScanExecutor> BuildHttpScanExecutorForProfile(std::unique_ptr<HttpTransport> transport,
                                                                    const HttpExecutionProfile &profile);

} // namespace internal
} // namespace duckdb_api
