#pragma once

#include "live_rest/plan.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace live_rest {

// Runtime errors expose only bounded, response-independent diagnostics. The
// adapter may use Stage() and Field() for classification; what() never includes
// a URL, header value, response body, or transport exception text.
enum class RuntimeStage : uint8_t {
	PLAN,
	TRANSPORT,
	HTTP_STATUS,
	DECODE,
	SCHEMA,
	RESOURCE
};

class RuntimeError : public std::runtime_error {
public:
	RuntimeError(RuntimeStage stage, std::string field, std::string safe_message);

	RuntimeStage Stage() const noexcept;
	const std::string &Field() const noexcept;

private:
	RuntimeStage stage;
	std::string field;
};

class ExecutionCancelled : public std::runtime_error {
public:
	ExecutionCancelled();
};

// A cancellation view is call-scoped: transports must poll it while blocked
// but must never retain it after Get returns. This keeps DuckDB-owned lifetime
// and synchronization outside the runtime service.
class CancellationView {
public:
	virtual ~CancellationView() noexcept;
	virtual bool IsCancellationRequested() const noexcept = 0;
};

struct HttpHeader {
	std::string name;
	std::string value;
};

struct HttpLimits {
	uint64_t max_response_bytes;
	uint64_t wall_milliseconds;
};

struct HttpResponse {
	uint32_t status;
	std::string body;
};

// HttpTransport is the only network-capable team API. Get performs exactly one
// synchronous unauthenticated request. Implementations must enforce both hard
// limits, observe cancellation during I/O, and disable redirects, retries,
// authentication, pagination, ambient proxy configuration, and response-body
// logging. The runtime validates the immutable plan before invoking it.
class HttpTransport {
public:
	virtual ~HttpTransport() noexcept;
	virtual HttpResponse Get(const std::string &url, const std::vector<HttpHeader> &fixed_headers,
	                         const HttpLimits &limits, const CancellationView &cancellation) const = 0;
};

struct LiveRow {
	int64_t id;
	std::string login;
	bool site_admin;
};

// BatchStream is a serialized pull interface with bounded backpressure. Next
// replaces rows with at most plan.batch_rows values. Cancel is safe to call
// concurrently with Get; Next and Close are serialized by the consumer. Both
// Cancel and Close are idempotent, and Close releases decoded rows.
class BatchStream {
public:
	virtual ~BatchStream() noexcept;
	virtual bool Next(const CancellationView &cancellation, std::vector<LiveRow> &rows) = 0;
	virtual void Cancel() noexcept = 0;
	virtual void Close() noexcept = 0;
};

// ScanExecutor is immutable and reusable. Open creates isolated per-scan state
// without network I/O; the stream performs its sole GET on the first Next.
class ScanExecutor {
public:
	virtual ~ScanExecutor() noexcept;
	virtual std::unique_ptr<BatchStream> Open(const LiveScanPlan &plan,
	                                          const CancellationView &cancellation) const = 0;
};

std::shared_ptr<const ScanExecutor> BuildScanExecutor(std::unique_ptr<HttpTransport> transport);

} // namespace live_rest
