#pragma once

#include "duckdb_api/execution.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

struct HttpHeader {
	std::string name;
	std::string value;
};

// Structural request authority. The executor builds this value only after the
// immutable ScanPlan passes executable-capability validation. No installed API
// accepts a caller-selected request.
struct HttpRequest {
	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string target;
	std::vector<HttpHeader> headers;
};

// Hard per-request limits. The deadline covers transport and decode together;
// implementations must stop before either wire or decompressed accounting can
// exceed its ceiling.
struct HttpLimits {
	uint64_t max_header_bytes;
	uint64_t max_response_bytes;
	uint64_t max_decompressed_bytes;
	std::chrono::steady_clock::time_point deadline;
};

struct HttpResponse {
	uint32_t status;
	uint64_t header_bytes;
	uint64_t response_bytes;
	std::string body;
};

// Private protocol-neutral transport boundary. Get performs one synchronous
// request attempt, polls the call-scoped control, and returns no raw dependency
// diagnostic. The production implementation is fixed to RFC 0005's HTTPS
// authority; deterministic tests provide a non-network implementation.
class HttpTransport {
public:
	virtual ~HttpTransport() noexcept {
	}
	virtual HttpResponse Get(const HttpRequest &request, const HttpLimits &limits, ExecutionControl &control) const = 0;
};

} // namespace internal
} // namespace duckdb_api
