#pragma once

#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <thread>

namespace duckdb_api {
namespace internal {

struct RetriedHttpResponse {
	HttpResponse response;
	PageResourceAllowance allowance;
};

// Deterministic stream-local jitter primitive. A production caller supplies a
// stream seed; tests can inject a fixed seed. The result remains in the RFC
// 0024 inclusive 75%-125% interval and never exceeds the admitted one-delay
// ceiling.
inline uint64_t ComputeRetryDelayMilliseconds(uint64_t failed_attempt, uint64_t max_delay, uint64_t seed) {
	if (failed_attempt == 0 || max_delay == 0) {
		return 0;
	}
	uint64_t nominal = 10;
	for (uint64_t index = 1; index < failed_attempt && nominal < max_delay; index++) {
		nominal = nominal > max_delay / 2 ? max_delay : nominal * 2;
	}
	nominal = std::min(nominal, max_delay);
	const uint64_t percent = 75 + ((seed ^ (failed_attempt * 0x9e3779b97f4a7c15ULL)) % 51);
	const uint64_t jittered =
	    nominal > std::numeric_limits<uint64_t>::max() / percent ? nominal : (nominal * percent) / 100;
	const uint64_t lower =
	    nominal > (std::numeric_limits<uint64_t>::max() - 99) / 75 ? nominal : (nominal * 75 + 99) / 100;
	const uint64_t upper = nominal > std::numeric_limits<uint64_t>::max() / 125 ? nominal : (nominal * 125) / 100;
	return std::min(max_delay, std::max<uint64_t>(1, std::max(lower, std::min(upper, jittered))));
}

inline bool IsRetryableTransportFailure(const HttpAttemptFailure &failure) noexcept {
	const auto &facts = failure.Facts();
	if (facts.response_status != 0 || facts.header_bytes != 0 || facts.response_bytes != 0 ||
	    facts.decompressed_response_bytes != 0) {
		return false;
	}
	switch (failure.Kind()) {
	case HttpTransportFailureKind::COULD_NOT_RESOLVE_HOST:
	case HttpTransportFailureKind::COULD_NOT_CONNECT:
	case HttpTransportFailureKind::SEND_FAILED:
	case HttpTransportFailureKind::EMPTY_RESPONSE:
	case HttpTransportFailureKind::RECEIVE_FAILED:
		return true;
	case HttpTransportFailureKind::OTHER:
		return false;
	}
	return false;
}

inline bool IsRetryableGatewayResponse(const HttpResponse &response) noexcept {
	return (response.status == 502 || response.status == 503 || response.status == 504) &&
	       !response.metadata.retry_after_present;
}

inline TransportResourceUsage AttemptUsage(const HttpAttemptFacts &facts) noexcept {
	return {facts.header_bytes, facts.response_bytes, facts.decompressed_response_bytes};
}

inline TransportResourceUsage AttemptUsage(const HttpResponse &response) noexcept {
	return {response.header_bytes, response.response_bytes, response.decompressed_response_bytes};
}

// The sole Runtime retry loop. It repeats only a freshly rebuilt admitted
// request for the current unaccepted step, while one ScanResourceAccounting
// instance retains every attempt, byte, wait, page, and deadline debit. The
// request factory closes over one immutable admitted profile and one scan-owned
// authorization snapshot; transport re-applies destination policy on every
// fresh synchronous attempt.
inline RetriedHttpResponse ExecuteHttpStepWithRetry(const RetryPlan &policy, ScanResourceAccounting &accounting,
                                                    const std::shared_ptr<const HttpTransport> &transport,
                                                    uint64_t max_metadata_bytes, uint64_t jitter_seed,
                                                    const std::function<HttpRequest()> &request_factory,
                                                    ExecutionControl &control) {
	auto allowance = accounting.BeginPage(std::chrono::steady_clock::now());
	while (true) {
		if (control.IsCancellationRequested()) {
			throw ExecutionCancelled();
		}
		auto request = request_factory();
		const auto request_body_bytes = static_cast<uint64_t>(request.body.size());
		if (!request.body.empty()) {
			accounting.CommitRequestBody(static_cast<uint64_t>(request.body.size()));
		}
		const HttpLimits limits {
		    allowance.serialized_request_body_bytes, allowance.header_bytes, allowance.wire_response_bytes,
		    allowance.decompressed_response_bytes,   max_metadata_bytes,     allowance.deadline};
		try {
			auto response = transport->Execute(request, limits, control);
			if (response.header_bytes > limits.max_header_bytes ||
			    response.response_bytes > limits.max_response_bytes ||
			    response.decompressed_response_bytes > limits.max_decompressed_bytes ||
			    static_cast<uint64_t>(response.body.size()) > response.decompressed_response_bytes ||
			    response.metadata.retained_bytes > limits.max_metadata_bytes) {
				throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP response exceeded an execution budget");
			}
			if (!IsRetryableGatewayResponse(response)) {
				accounting.CommitTransport(AttemptUsage(response));
				return {std::move(response), allowance};
			}
			accounting.CommitAttemptFailure(AttemptUsage(response));
			if (!policy.Enabled() || !accounting.CanBeginRetryAttempt()) {
				auto properties = HttpStatusFailureProperties(response.status, false);
				if (policy.Enabled()) {
					properties.terminating_budget = BudgetDimension::ATTEMPTS;
				}
				throw ExecutionError(ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status",
				                     properties);
			}
			accounting.RequireRetryAttemptResources(request_body_bytes);
		} catch (const HttpAttemptFailure &failure) {
			accounting.CommitAttemptFailure(AttemptUsage(failure.Facts()));
			const bool retryable = IsRetryableTransportFailure(failure);
			if (!policy.Enabled() || !retryable || !accounting.CanBeginRetryAttempt()) {
				auto properties = FailurePropertiesFromError(failure.Error());
				properties.phase = FailurePhase::TRANSPORT;
				if (failure.Facts().response_status != 0) {
					const auto status = failure.Facts().response_status;
					properties.remote_status_class =
					    status >= 200 && status < 300
					        ? RemoteStatusClass::SUCCESS
					        : (status == 429 ? RemoteStatusClass::RATE_LIMITED
					                         : (status >= 500 ? RemoteStatusClass::SERVER_ERROR
					                                          : RemoteStatusClass::CLIENT_ERROR));
				}
				if (policy.Enabled() && retryable) {
					properties.terminating_budget = BudgetDimension::ATTEMPTS;
				}
				if (!retryable) {
					properties.replay_classification = ReplayClassification::NEVER_REPLAYABLE;
				}
				throw ExecutionError(failure.Error().Stage(), failure.Error().Field(), failure.Error().SafeMessage(),
				                     properties);
			}
			accounting.RequireRetryAttemptResources(request_body_bytes);
		}

		const auto used_wait = accounting.Counters().cumulative_waiting_milliseconds;
		if (used_wait >= policy.max_cumulative_waiting_milliseconds_per_scan) {
			throw ScanResourceError("cumulative_waiting_milliseconds", "scan exhausted its cumulative-waiting budget");
		}
		auto delay =
		    ComputeRetryDelayMilliseconds(accounting.CurrentAttempt(), policy.max_delay_milliseconds, jitter_seed);
		delay = std::min(delay, policy.max_cumulative_waiting_milliseconds_per_scan - used_wait);
		if (delay == 0) {
			throw ScanResourceError("cumulative_waiting_milliseconds", "scan exhausted its cumulative-waiting budget");
		}
		uint64_t waited = 0;
		while (waited < delay) {
			if (control.IsCancellationRequested()) {
				throw ExecutionCancelled();
			}
			const auto now = std::chrono::steady_clock::now();
			if (now >= accounting.Deadline()) {
				throw ScanResourceError("wall_milliseconds", "scan exceeded its wall-time budget");
			}
			const auto slice = std::min<uint64_t>(5, delay - waited);
			accounting.CommitWait(slice);
			std::this_thread::sleep_for(std::chrono::milliseconds(slice));
			waited += slice;
		}
		if (control.IsCancellationRequested()) {
			throw ExecutionCancelled();
		}
		allowance = accounting.BeginRetryAttempt(std::chrono::steady_clock::now());
	}
}

} // namespace internal
} // namespace duckdb_api
