#include "duckdb_api/internal/runtime/execution/http_retry_controller.hpp"
#include "support/require.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api::ExecutionControl;
using duckdb_api::ExecutionError;
using duckdb_api::FailureProperties;
using duckdb_api::PlannedRateLimitGuidance;
using duckdb_api::PlannedRateLimitGuidanceFormat;
using duckdb_api::PlannedRateLimitMode;
using duckdb_api::PlannedRateLimitPrincipalScope;
using duckdb_api::RateLimitReason;
using duckdb_api::RetryPlan;
using duckdb_api::internal::AdmittedRateLimitPolicy;
using duckdb_api::internal::ExecuteHttpStepWithRetry;
using duckdb_api::internal::HttpAttemptCancelled;
using duckdb_api::internal::HttpAttemptFailure;
using duckdb_api::internal::HttpLimits;
using duckdb_api::internal::HttpObservedHeader;
using duckdb_api::internal::HttpRequest;
using duckdb_api::internal::HttpResponse;
using duckdb_api::internal::HttpTransport;
using duckdb_api::internal::HttpTransportFailureKind;
using duckdb_api::internal::RateLimitClock;
using duckdb_api::internal::RateLimitClockReceipt;
using duckdb_api::internal::RateLimitCoordinator;
using duckdb_api::internal::RateLimitPrincipalToken;
using duckdb_api::internal::RateLimitRuntimeContext;
using duckdb_api::internal::ResilienceExecutionState;
using duckdb_api::internal::ScanResourceAccounting;
using duckdb_api::internal::ScanResourceCounters;
using duckdb_api::internal::ScanResourceProfile;
using duckdb_api_test::Require;

class NeverCancelled final : public ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

class AdvancingClock final : public RateLimitClock {
public:
	explicit AdvancingClock(int64_t steady_p = 0) : steady(steady_p) {
	}

	RateLimitClockReceipt CaptureReceipt() const noexcept override {
		const auto value = steady.load(std::memory_order_acquire);
		return {100000 + value, value};
	}

	int64_t SteadyNowMilliseconds() const noexcept override {
		return steady.load(std::memory_order_acquire);
	}

	void WaitFor(std::condition_variable &, std::unique_lock<std::mutex> &, uint64_t milliseconds) const override {
		steady.fetch_add(static_cast<int64_t>(milliseconds), std::memory_order_acq_rel);
	}

private:
	mutable std::atomic<int64_t> steady;
};

struct ScriptedStep {
	HttpResponse response;
	bool partial;
	bool cancelled;
};

HttpResponse Response(uint32_t status, std::vector<HttpObservedHeader> fields = {},
                      std::vector<std::string> dates = {}) {
	return {status, 64, 0, 0, "", {{}, 0, false, std::move(fields), std::move(dates)}};
}

ScriptedStep Complete(uint32_t status, std::vector<HttpObservedHeader> fields = {},
                      std::vector<std::string> dates = {}) {
	return {Response(status, std::move(fields), std::move(dates)), false, false};
}

ScriptedStep Partial(uint32_t status) {
	return {Response(status), true, false};
}

ScriptedStep TransientFailure() {
	return {{0, 0, 0, 0, "", {{}, 0, false, {}, {}}}, true, false};
}

ScriptedStep CancelledAttempt(uint32_t status, uint64_t header_bytes, uint64_t response_bytes,
                              uint64_t decompressed_response_bytes) {
	return {
	    {status, header_bytes, response_bytes, decompressed_response_bytes, "", {{}, 0, false, {}, {}}}, false, true};
}

class ScriptedTransport final : public HttpTransport {
public:
	explicit ScriptedTransport(std::vector<ScriptedStep> steps_p)
	    : mutex(), steps(std::move(steps_p)), next(0), retained_names(), retain_date(false) {
	}

	uint64_t RequestCount() const {
		std::lock_guard<std::mutex> guard(mutex);
		return static_cast<uint64_t>(next);
	}
	std::vector<std::string> RetainedNames() const {
		std::lock_guard<std::mutex> guard(mutex);
		return retained_names;
	}
	bool RetainedDate() const {
		std::lock_guard<std::mutex> guard(mutex);
		return retain_date;
	}

protected:
	HttpResponse Get(const HttpRequest &, const HttpLimits &limits, ExecutionControl &) const override {
		std::lock_guard<std::mutex> guard(mutex);
		if (next >= steps.size()) {
			throw std::runtime_error("script exhausted");
		}
		retained_names = limits.retained_header_names;
		retain_date = limits.retain_date;
		auto step = steps[next++];
		if (step.cancelled) {
			throw HttpAttemptCancelled({step.response.status, step.response.header_bytes, step.response.response_bytes,
			                            step.response.decompressed_response_bytes});
		}
		if (step.partial) {
			throw HttpAttemptFailure(HttpTransportFailureKind::RECEIVE_FAILED,
			                         {step.response.status, step.response.header_bytes, step.response.response_bytes,
			                          step.response.decompressed_response_bytes},
			                         ExecutionError(duckdb_api::ErrorStage::TRANSPORT, "", "HTTP request failed"));
		}
		return step.response;
	}

private:
	mutable std::mutex mutex;
	std::vector<ScriptedStep> steps;
	mutable std::size_t next;
	mutable std::vector<std::string> retained_names;
	mutable bool retain_date;
};

AdmittedRateLimitPolicy WaitingPolicy(PlannedRateLimitMode mode = PlannedRateLimitMode::WAIT, uint64_t max_attempts = 3,
                                      uint64_t max_delay = 30000, uint64_t max_wait = 30000) {
	return {true,
	        "rate_limit_fixture",
	        mode,
	        {429},
	        "core_requests",
	        PlannedRateLimitPrincipalScope::CREDENTIAL_AUTHORITY,
	        {PlannedRateLimitGuidance {"x-reset", PlannedRateLimitGuidanceFormat::DELTA_SECONDS}},
	        "",
	        "",
	        max_attempts,
	        max_attempts,
	        max_delay,
	        max_wait,
	        3,
	        true};
}

AdmittedRateLimitPolicy FailPolicy(uint32_t status = 429) {
	return {true,
	        "rate_limit_fixture",
	        PlannedRateLimitMode::FAIL,
	        {static_cast<uint16_t>(status)},
	        "core_requests",
	        PlannedRateLimitPrincipalScope::CREDENTIAL_AUTHORITY,
	        {},
	        "",
	        "",
	        1,
	        1,
	        0,
	        0,
	        3,
	        false};
}

struct Outcome {
	bool failed;
	bool cancelled;
	FailureProperties properties;
	uint32_t response_status;
	ScanResourceCounters counters;
	ResilienceExecutionState state;
	uint64_t requests;
	std::vector<std::string> retained_names;
	bool retain_date;
};

Outcome Run(const RetryPlan &retry, const AdmittedRateLimitPolicy &rate_limit, std::vector<ScriptedStep> steps,
            uint64_t wall_milliseconds = 1000, std::shared_ptr<const RateLimitClock> injected_clock = {}) {
	const auto attempts_per_step = std::max(retry.max_attempts_per_step, rate_limit.max_attempts_per_step);
	const auto attempts_per_scan = std::max(retry.max_attempts_per_scan, rate_limit.max_attempts_per_scan);
	const auto combined_wait =
	    retry.max_cumulative_waiting_milliseconds_per_scan + rate_limit.max_cumulative_waiting_milliseconds_per_scan;
	const ScanResourceProfile profile = {{attempts_per_step, 4096, 4096, 4096, 1, 4096, 1, 0},
	                                     {attempts_per_scan, 1, 4096 * attempts_per_scan, 4096 * attempts_per_scan,
	                                      4096 * attempts_per_scan, 1, 4096, wall_milliseconds, 1, 0, combined_wait,
	                                      retry.max_cumulative_waiting_milliseconds_per_scan,
	                                      rate_limit.max_cumulative_waiting_milliseconds_per_scan, wall_milliseconds}};
	ScanResourceAccounting accounting(profile);
	auto transport = std::make_shared<ScriptedTransport>(std::move(steps));
	std::shared_ptr<const HttpTransport> transport_view = transport;
	auto clock = injected_clock ? std::move(injected_clock) : duckdb_api::internal::NewSystemRateLimitClock();
	auto coordinator = std::make_shared<RateLimitCoordinator>(RateLimitCoordinator::HardLimits(), clock);
	RateLimitRuntimeContext runtime(coordinator, clock, RateLimitPrincipalToken::Anonymous());
	ResilienceExecutionState state;
	NeverCancelled control;
	bool failed = false;
	bool cancelled = false;
	FailureProperties properties {};
	uint32_t response_status = 0;
	try {
		auto result = ExecuteHttpStepWithRetry(
		    retry, rate_limit, runtime, state, accounting, transport_view, 4096, 7,
		    []() { return HttpRequest {"GET", "https", "api.example.test", 443, "/events", {}, "", ""}; }, control);
		response_status = result.response.status;
	} catch (const ExecutionError &error) {
		failed = true;
		Require(error.Classified(), "terminal rate-limit execution error was not classified");
		properties = error.Properties();
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancelled = true;
	}
	return {failed,
	        cancelled,
	        properties,
	        response_status,
	        accounting.Counters(),
	        state,
	        transport->RequestCount(),
	        transport->RetainedNames(),
	        transport->RetainedDate()};
}

const RetryPlan NO_RETRY {1, 1, 0, 0};

void TestImmediateRecoveryAndTargetedObservation() {
	auto outcome = Run(NO_RETRY, WaitingPolicy(),
	                   {Complete(429, {{"x-reset", "0"}}), Complete(200, {{"x-reset", "not-observed"}})});
	Require(!outcome.failed && outcome.response_status == 200 && outcome.requests == 2,
	        "one immediate rate-limit repeat did not recover");
	Require(outcome.state.rate_limit_events == 1 && outcome.state.rate_limit_repeats == 1 &&
	            outcome.state.rate_limit_waits == 0 && outcome.counters.request_attempts == 2,
	        "immediate recovery counters were not exact");
	Require(outcome.retained_names == std::vector<std::string> {"x-reset"} && !outcome.retain_date,
	        "transport did not receive the exact targeted delta field set");
}

void TestFailModeAndSuccessfulResponseNonPacing() {
	auto failed = Run(NO_RETRY, FailPolicy(), {Complete(429, {{"undeclared", "private-canary"}})});
	Require(failed.failed && failed.requests == 1 &&
	            failed.properties.rate_limit_reason == RateLimitReason::POLICY_FAIL,
	        "fail mode parsed or repeated a matching response");
	auto success = Run(NO_RETRY, WaitingPolicy(), {Complete(200, {{"x-reset", "malformed"}})});
	Require(!success.failed && success.requests == 1 && success.state.rate_limit_events == 0,
	        "successful response metadata triggered proactive pacing");
}

void TestMalformedMissingAndExcessiveGuidance() {
	auto missing = Run(NO_RETRY, WaitingPolicy(), {Complete(429)});
	Require(missing.failed && missing.properties.rate_limit_reason == RateLimitReason::GUIDANCE_MISSING,
	        "missing guidance used the wrong terminal reason");
	auto duplicate = Run(NO_RETRY, WaitingPolicy(), {Complete(429, {{"x-reset", "0"}, {"x-reset", "1"}})});
	Require(duplicate.failed && duplicate.properties.rate_limit_reason == RateLimitReason::MALFORMED_GUIDANCE,
	        "duplicate guidance did not fail closed");
	auto excessive =
	    Run(NO_RETRY, WaitingPolicy(PlannedRateLimitMode::WAIT, 2, 1, 1000), {Complete(429, {{"x-reset", "1"}})});
	Require(excessive.failed && excessive.properties.rate_limit_reason == RateLimitReason::GUIDANCE_EXCEEDS_POLICY,
	        "excessive guidance was clamped or misclassified");
}

void TestRemainingBucketAndImmediateStability() {
	auto remaining = WaitingPolicy();
	remaining.remaining_quota_header = "x-remaining";
	auto malformed_remaining = Run(NO_RETRY, remaining, {Complete(429, {{"x-reset", "0"}, {"x-remaining", "NaN"}})});
	Require(malformed_remaining.failed &&
	            malformed_remaining.properties.rate_limit_reason == RateLimitReason::MALFORMED_GUIDANCE,
	        "malformed remaining data did not fail closed");

	auto bucket = WaitingPolicy();
	bucket.remote_bucket_header = "x-bucket";
	auto changed = Run(NO_RETRY, bucket,
	                   {Complete(429, {{"x-reset", "0"}, {"x-bucket", "alpha"}}),
	                    Complete(429, {{"x-reset", "1"}, {"x-bucket", "beta"}})});
	Require(changed.failed && changed.requests == 2 &&
	            changed.properties.rate_limit_reason == RateLimitReason::BUCKET_CHANGED,
	        "remote bucket drift transferred quota authority");

	auto repeated =
	    Run(NO_RETRY, WaitingPolicy(), {Complete(429, {{"x-reset", "0"}}), Complete(429, {{"x-reset", "0"}})});
	Require(repeated.failed && repeated.requests == 2 &&
	            repeated.properties.rate_limit_reason == RateLimitReason::REPEATED_IMMEDIATE,
	        "second consecutive immediate response obtained another request");
}

void TestPrecedencePartialResponseAndCombinedAttemptCeiling() {
	auto rate_503 = WaitingPolicy(PlannedRateLimitMode::WAIT, 2, 30000, 100);
	rate_503.statuses = {503};
	const RetryPlan retry {2, 2, 20, 100};
	auto precedence = Run(retry, rate_503, {Complete(503, {{"x-reset", "0"}}), Complete(200)});
	Require(!precedence.failed && precedence.state.rate_limit_repeats == 1 &&
	            precedence.state.ordinary_retry_repeats == 0,
	        "matching 503 did not take precedence over ordinary gateway retry");

	auto partial = Run(retry, WaitingPolicy(PlannedRateLimitMode::WAIT, 2, 30000, 100), {Partial(429)});
	Require(partial.failed && partial.requests == 1 && partial.state.rate_limit_events == 0,
	        "partial matching response entered rate-limit dispatch or replayed");

	auto bounded = WaitingPolicy(PlannedRateLimitMode::WAIT, 2, 30000, 100);
	auto combined = Run(retry, bounded, {TransientFailure(), Complete(429, {{"x-reset", "0"}}), Complete(200)});
	Require(combined.failed && combined.requests == 2 &&
	            combined.properties.rate_limit_reason == RateLimitReason::ATTEMPTS_EXHAUSTED,
	        "ordinary retry plus rate-limit handling received additive attempt pools");
}

void TestCancelledAttemptCommitsObservedBytes() {
	auto outcome = Run(NO_RETRY, FailPolicy(), {CancelledAttempt(200, 17, 23, 19)});
	Require(!outcome.failed && outcome.cancelled && outcome.requests == 1,
	        "attempt cancellation was not preserved after accounting");
	Require(outcome.counters.header_bytes == 17 && outcome.counters.wire_response_bytes == 23 &&
	            outcome.counters.decompressed_response_bytes == 19 && outcome.counters.request_attempts == 1,
	        "attempt cancellation bypassed observed-byte accounting");
}

void TestDeadlinePrecheck() {
	auto policy = WaitingPolicy(PlannedRateLimitMode::WAIT_IF_DEADLINE_ALLOWS, 2, 30000, 30000);
	auto outcome = Run(NO_RETRY, policy, {Complete(429, {{"x-reset", "2"}})}, 50);
	Require(outcome.failed && outcome.requests == 1 &&
	            outcome.properties.rate_limit_reason == RateLimitReason::DEADLINE_INSUFFICIENT,
	        "deadline-insufficient guidance enqueued or slept");
}

void TestPositiveWaitAndExactWaitingBoundary() {
	auto clock = std::make_shared<AdvancingClock>();
	auto policy = WaitingPolicy(PlannedRateLimitMode::WAIT_IF_DEADLINE_ALLOWS, 2, 1000, 1000);
	auto outcome = Run(NO_RETRY, policy, {Complete(429, {{"x-reset", "1"}}), Complete(200)}, 5000, clock);
	Require(!outcome.failed && outcome.response_status == 200 && outcome.requests == 2 &&
	            outcome.state.rate_limit_repeats == 1 && outcome.state.rate_limit_waits == 1 &&
	            outcome.counters.cumulative_rate_limit_waiting_milliseconds == 1000 &&
	            outcome.counters.cumulative_waiting_milliseconds == 1000,
	        "positive guidance at the exact waiting-budget boundary did not recover with exact accounting");
}

} // namespace

int main() {
	try {
		TestImmediateRecoveryAndTargetedObservation();
		TestFailModeAndSuccessfulResponseNonPacing();
		TestMalformedMissingAndExcessiveGuidance();
		TestRemainingBucketAndImmediateStability();
		TestPrecedencePartialResponseAndCombinedAttemptCeiling();
		TestCancelledAttemptCommitsObservedBytes();
		TestDeadlinePrecheck();
		TestPositiveWaitAndExactWaitingBoundary();
		std::cout << "Rate-limit execution tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
