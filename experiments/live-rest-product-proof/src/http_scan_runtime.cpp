#include "live_rest/runtime.hpp"

#include "live_rest/internal/json_decoder.hpp"

#include <atomic>
#include <utility>

namespace live_rest {
namespace {

const char *const REQUEST_TARGET = "/search/users?q=duckdb+in%3Alogin&per_page=3";

void CheckCancelled(const CancellationView &cancellation) {
	if (cancellation.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
}

std::vector<HttpHeader> FixedHeaders() {
	return {{"Accept", "application/vnd.github+json"},
	        {"User-Agent", "duckdb-fdw-live-rest-product-proof"},
	        {"X-GitHub-Api-Version", "2022-11-28"}};
}

bool HasSuffix(const std::string &value, const std::string &suffix) {
	return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void ValidatePlan(const LiveScanPlan &plan) {
	// Rebuilding the canonical offline plan closes the mutable-value boundary
	// before network authority is acquired. No caller-mutated destination,
	// capability flag, schema, or budget can reach HttpTransport.
	const std::string target(REQUEST_TARGET);
	if (!HasSuffix(plan.url, target) || plan.url.size() == target.size()) {
		throw RuntimeError(RuntimeStage::PLAN, "url", "live REST plan is outside the fixed execution profile");
	}
	try {
		const auto canonical = BuildLiveScanPlan(plan.url.substr(0, plan.url.size() - target.size()));
		if (canonical.Snapshot() != plan.Snapshot()) {
			throw RuntimeError(RuntimeStage::PLAN, "", "live REST plan is outside the fixed execution profile");
		}
	} catch (const RuntimeError &) {
		throw;
	} catch (...) {
		throw RuntimeError(RuntimeStage::PLAN, "", "live REST plan is outside the fixed execution profile");
	}
}

// This view composes DuckDB's call lifetime with an independently callable
// stream cancellation bit. HttpTransport may poll it synchronously but cannot
// retain it; Cancel is the only operation supported concurrently with Next.
class CombinedCancellation final : public CancellationView {
public:
	CombinedCancellation(const CancellationView &outer_p, const std::atomic<bool> &stream_cancelled_p)
	    : outer(outer_p), stream_cancelled(stream_cancelled_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		return stream_cancelled.load(std::memory_order_acquire) || outer.IsCancellationRequested();
	}

private:
	const CancellationView &outer;
	const std::atomic<bool> &stream_cancelled;
};

// One stream owns one immutable plan and at most one HTTP exchange. A failed
// exchange is marked attempted before I/O, preventing accidental replay. The
// decoded set is bounded to three rows and is released by idempotent Close.
class LiveBatchStream final : public BatchStream {
public:
	LiveBatchStream(LiveScanPlan plan_p, std::shared_ptr<const HttpTransport> transport_p)
	    : plan(std::move(plan_p)), transport(std::move(transport_p)), cancelled(false), closed(false), attempted(false),
	      offset(0) {
	}

	~LiveBatchStream() noexcept override {
		Close();
	}

	bool Next(const CancellationView &cancellation, std::vector<LiveRow> &rows) override {
		rows.clear();
		if (closed) {
			return false;
		}

		CombinedCancellation combined(cancellation, cancelled);
		CheckCancelled(combined);
		if (!attempted) {
			attempted = true;
			const HttpLimits limits {plan.max_response_bytes, plan.wall_milliseconds};
			HttpResponse response;
			try {
				response = transport->Get(plan.url, FixedHeaders(), limits, combined);
			} catch (const ExecutionCancelled &) {
				throw;
			} catch (const RuntimeError &) {
				throw;
			} catch (...) {
				// Transport exception text may contain response bytes, URLs, or
				// dependency diagnostics, so it never crosses the team API.
				throw RuntimeError(RuntimeStage::TRANSPORT, "", "live REST HTTP request failed");
			}

			CheckCancelled(combined);
			if (static_cast<uint64_t>(response.body.size()) > plan.max_response_bytes) {
				throw RuntimeError(RuntimeStage::RESOURCE, "", "live REST response exceeds the configured byte limit");
			}
			if (response.status < 200 || response.status >= 300) {
				throw RuntimeError(RuntimeStage::HTTP_STATUS, "", "live REST endpoint returned a non-success status");
			}
			decoded = internal::DecodeResponseRows(response.body, plan, combined);
		}

		if (offset >= decoded.size()) {
			return false;
		}
		const auto remaining = decoded.size() - offset;
		const auto batch_size = static_cast<std::size_t>(plan.batch_rows);
		const auto count = remaining < batch_size ? remaining : batch_size;
		rows.reserve(count);
		for (std::size_t index = 0; index < count; index++) {
			rows.push_back(std::move(decoded[offset++]));
		}
		return true;
	}

	void Cancel() noexcept override {
		cancelled.store(true, std::memory_order_release);
	}

	void Close() noexcept override {
		if (closed) {
			return;
		}
		closed = true;
		cancelled.store(true, std::memory_order_release);
		decoded.clear();
		offset = 0;
	}

private:
	const LiveScanPlan plan;
	const std::shared_ptr<const HttpTransport> transport;
	std::atomic<bool> cancelled;
	bool closed;
	bool attempted;
	std::vector<LiveRow> decoded;
	std::size_t offset;
};

class ImmutableScanExecutor final : public ScanExecutor {
public:
	explicit ImmutableScanExecutor(std::shared_ptr<const HttpTransport> transport_p)
	    : transport(std::move(transport_p)) {
	}

	std::unique_ptr<BatchStream> Open(const LiveScanPlan &plan,
	                                  const CancellationView &cancellation) const override {
		CheckCancelled(cancellation);
		ValidatePlan(plan);
		return std::unique_ptr<BatchStream>(new LiveBatchStream(plan, transport));
	}

private:
	const std::shared_ptr<const HttpTransport> transport;
};

} // namespace

std::shared_ptr<const ScanExecutor> BuildScanExecutor(std::unique_ptr<HttpTransport> transport) {
	if (!transport) {
		throw std::invalid_argument("live REST scan executor requires an HTTP transport");
	}
	std::shared_ptr<const HttpTransport> shared_transport(std::move(transport));
	return std::shared_ptr<const ScanExecutor>(new ImmutableScanExecutor(std::move(shared_transport)));
}

} // namespace live_rest
