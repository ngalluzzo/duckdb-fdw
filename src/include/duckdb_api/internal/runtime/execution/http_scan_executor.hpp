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

// Closed construction-time authority for a privately composed executor. The
// installed factory above supplies only its fixed public HTTPS profile; test
// support may link this private boundary to bind an exact controlled plan.
// No SQL, setting, environment value, or per-scan input constructs a profile.
struct HttpExecutionProfile {
	PlannedUrlScheme scheme;
	std::string host;
	uint16_t port;
	bool private_addresses_enabled;
	bool link_local_addresses_enabled;
	bool loopback_addresses_enabled;
	uint64_t max_wall_milliseconds;
	// Per-response record authority. The installed 0.6 profile admits up to
	// 100 repository records; private fixtures may narrow this value.
	uint64_t max_decoded_records;
};

std::shared_ptr<const ScanExecutor> BuildHttpScanExecutorForProfile(std::unique_ptr<HttpTransport> transport,
                                                                    const HttpExecutionProfile &profile);

} // namespace internal
} // namespace duckdb_api
