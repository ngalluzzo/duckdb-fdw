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
	// Retained normalized response metadata is separately bounded by the
	// decoder's remaining page-memory authority. Zero disables Link retention
	// for operations that have no pagination semantics.
	uint64_t max_metadata_bytes;
	std::chrono::steady_clock::time_point deadline;
};

// Narrow protocol metadata returned by one attempt. Transport preserves only
// physical Link field-values from the terminal response header section, in
// receipt order. It does not parse relations or URLs and never exposes a
// general header map, received destination, or dependency response object.
struct HttpResponseMetadata {
	std::vector<std::string> link_field_values;
	uint64_t retained_bytes;
};

struct HttpResponse {
	uint32_t status;
	uint64_t header_bytes;
	uint64_t response_bytes;
	std::string body;
	HttpResponseMetadata metadata;
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
