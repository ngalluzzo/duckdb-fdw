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
// immutable ScanPlan passes executable-capability validation. Body bytes are
// owned by the request, contain no credentials, and are immutable for one
// synchronous attempt. Content type is separate from the fixed non-sensitive
// header collection so transport implementations cannot silently infer it.
// No installed API accepts a caller-selected request.
struct HttpRequest {
	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string target;
	std::vector<HttpHeader> headers;
	std::string body;
	std::string content_type;
};

// Hard per-request limits. max_request_body_bytes is a secondary transport
// guard over an already measured and scan-debited outbound body; a value of
// zero authorizes only a bodyless request. The deadline covers transport and
// decode together. Implementations must stop before any byte ceiling can be
// exceeded.
struct HttpLimits {
	uint64_t max_request_body_bytes;
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

// Private protocol-neutral transport boundary. Execute performs one synchronous
// already-admitted request attempt, polls the call-scoped control, and returns
// no raw dependency diagnostic. It validates body/method coherence before a
// concrete transport sees the request. Existing GET-only transports remain
// source-compatible through Get; POST is denied unless a transport explicitly
// overrides Post. Implementations must not retain request, limits, or control
// beyond the call and own cleanup on every return or exception.
class HttpTransport {
public:
	virtual ~HttpTransport() noexcept {
	}

	HttpResponse Execute(const HttpRequest &request, const HttpLimits &limits, ExecutionControl &control) const {
		if (control.IsCancellationRequested()) {
			throw ExecutionCancelled();
		}
		if (request.method == "GET") {
			if (!request.body.empty() || !request.content_type.empty()) {
				throw ExecutionError(ErrorStage::POLICY, "request_body", "HTTP GET request cannot carry a body");
			}
			RequireDedicatedContentType(request);
			return Get(request, limits, control);
		}
		if (request.method == "POST") {
			if (request.body.empty() || request.content_type != "application/json") {
				throw ExecutionError(ErrorStage::POLICY, "request_body",
				                     "HTTP POST request requires a JSON body and content type");
			}
			RequireDedicatedContentType(request);
			if (static_cast<uint64_t>(request.body.size()) > limits.max_request_body_bytes) {
				throw ExecutionError(ErrorStage::RESOURCE, "request_body_bytes",
				                     "HTTP request exceeded its body budget");
			}
			return Post(request, limits, control);
		}
		throw ExecutionError(ErrorStage::POLICY, "method", "HTTP request method is not supported");
	}

protected:
	virtual HttpResponse Get(const HttpRequest &request, const HttpLimits &limits, ExecutionControl &control) const = 0;

	// POST remains opt-in. The safe default lets existing GET transports compile
	// unchanged and prevents adding body fields from granting execution.
	virtual HttpResponse Post(const HttpRequest &, const HttpLimits &, ExecutionControl &) const {
		throw ExecutionError(ErrorStage::POLICY, "method", "HTTP POST is not supported by this transport");
	}

private:
	static void RequireDedicatedContentType(const HttpRequest &request) {
		for (std::size_t index = 0; index < request.headers.size(); index++) {
			if (IsContentTypeHeader(request.headers[index].name)) {
				throw ExecutionError(ErrorStage::POLICY, "content_type",
				                     "HTTP content type must use the dedicated request field");
			}
		}
	}

	static bool IsContentTypeHeader(const std::string &name) noexcept {
		static const char expected[] = "content-type";
		if (name.size() != sizeof(expected) - 1) {
			return false;
		}
		for (std::size_t index = 0; index < sizeof(expected) - 1; index++) {
			const char value = name[index];
			const char folded = value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
			if (folded != expected[index]) {
				return false;
			}
		}
		return true;
	}
};

} // namespace internal
} // namespace duckdb_api
