#include "live_rest/runtime.hpp"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

class ManualCancellation final : public live_rest::CancellationView {
public:
	ManualCancellation() : cancelled(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire);
	}

	void Cancel() noexcept {
		cancelled.store(true, std::memory_order_release);
	}

private:
	std::atomic<bool> cancelled;
};

struct TransportState {
	TransportState()
	    : calls(0), status(200), throw_transport_error(false), cancel_during_get(nullptr), observed_cancellation(false) {
	}

	std::size_t calls;
	std::string url;
	std::vector<live_rest::HttpHeader> headers;
	live_rest::HttpLimits limits;
	uint32_t status;
	std::string body;
	bool throw_transport_error;
	ManualCancellation *cancel_during_get;
	bool observed_cancellation;
};

class FakeTransport final : public live_rest::HttpTransport {
public:
	explicit FakeTransport(std::shared_ptr<TransportState> state_p) : state(std::move(state_p)) {
	}

	live_rest::HttpResponse Get(const std::string &url, const std::vector<live_rest::HttpHeader> &fixed_headers,
	                            const live_rest::HttpLimits &limits,
	                            const live_rest::CancellationView &cancellation) const override {
		state->calls++;
		state->url = url;
		state->headers = fixed_headers;
		state->limits = limits;
		if (state->cancel_during_get != nullptr) {
			state->cancel_during_get->Cancel();
			state->observed_cancellation = cancellation.IsCancellationRequested();
			if (state->observed_cancellation) {
				throw live_rest::ExecutionCancelled();
			}
		}
		if (state->throw_transport_error) {
			throw std::runtime_error("transport leaked SECRET_TRANSPORT_CANARY from response");
		}
		return {state->status, state->body};
	}

private:
	std::shared_ptr<TransportState> state;
};

std::unique_ptr<live_rest::BatchStream> OpenStream(const std::shared_ptr<TransportState> &state,
                                                   const live_rest::LiveScanPlan &plan,
	                                               const live_rest::CancellationView &cancellation) {
	std::unique_ptr<live_rest::HttpTransport> transport(new FakeTransport(state));
	const auto executor = live_rest::BuildScanExecutor(std::move(transport));
	return executor->Open(plan, cancellation);
}

std::shared_ptr<TransportState> Response(uint32_t status, const std::string &body) {
	const auto result = std::make_shared<TransportState>();
	result->status = status;
	result->body = body;
	return result;
}

void RequireRuntimeError(const std::function<void()> &action, live_rest::RuntimeStage stage,
	                     const std::string &field, const std::vector<std::string> &forbidden = {}) {
	bool rejected = false;
	try {
		action();
	} catch (const live_rest::RuntimeError &error) {
		rejected = true;
		Require(error.Stage() == stage, "runtime error stage did not match the expected boundary");
		Require(error.Field() == field, "runtime error field did not match the expected field");
		const std::string safe_message(error.what());
		Require(!safe_message.empty() && safe_message.size() <= 128, "runtime error was empty or unbounded");
		for (std::size_t index = 0; index < forbidden.size(); index++) {
			Require(safe_message.find(forbidden[index]) == std::string::npos,
			        "runtime error exposed forbidden transport or response data");
		}
	}
	Require(rejected, "expected a structured runtime error");
}

void RequireCancellation(const std::function<void()> &action) {
	bool cancelled = false;
	try {
		action();
	} catch (const live_rest::ExecutionCancelled &) {
		cancelled = true;
	}
	Require(cancelled, "expected execution cancellation");
}

std::string OneRow(const std::string &overrides = "") {
	if (!overrides.empty()) {
		return std::string("{\"items\":[{") + overrides + "}]}";
	}
	return "{\"items\":[{\"id\":1,\"login\":\"duckdb\",\"site_admin\":false}]}";
}

void TestRequestShapeAndBatches() {
	ManualCancellation cancellation;
	const auto plan = live_rest::BuildLiveScanPlan("http://127.0.0.1:8080");
	const auto state = Response(
	    200,
	    "{\"ignored\":{\"nested\":[true,null,3.5]},\"items\":["
	    "{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false,\"ignored\":1},"
	    "{\"site_admin\":true,\"login\":\"duck\\u0064b-fdw\",\"id\":22},"
	    "{\"id\":33,\"login\":\"three\",\"site_admin\":false}]}"
	);
	auto stream = OpenStream(state, plan, cancellation);
	Require(state->calls == 0, "Open performed network I/O");

	std::vector<live_rest::LiveRow> rows;
	Require(stream->Next(cancellation, rows), "first batch was missing");
	Require(rows.size() == 2, "first batch did not honor the two-row plan bound");
	Require(rows[0].id == 11 && rows[0].login == "duckdb" && !rows[0].site_admin,
	        "first typed row was decoded incorrectly");
	Require(rows[1].id == 22 && rows[1].login == "duckdb-fdw" && rows[1].site_admin,
	        "second typed row or Unicode escape was decoded incorrectly");
	Require(state->calls == 1, "first pull did not perform exactly one request");
	Require(state->url == plan.url, "transport received the wrong fixed URL");
	Require(state->limits.max_response_bytes == plan.max_response_bytes &&
	            state->limits.wall_milliseconds == plan.wall_milliseconds,
	        "transport received the wrong hard limits");
	Require(state->headers.size() == 3, "fixed request header set drifted");
	Require(state->headers[0].name == "Accept" && state->headers[0].value == "application/vnd.github+json",
	        "fixed Accept header drifted");
	Require(state->headers[1].name == "User-Agent" &&
	            state->headers[1].value == "duckdb-fdw-live-rest-product-proof",
	        "fixed User-Agent header drifted");
	Require(state->headers[2].name == "X-GitHub-Api-Version" && state->headers[2].value == "2022-11-28",
	        "fixed GitHub API version header drifted");

	Require(stream->Next(cancellation, rows), "second batch was missing");
	Require(rows.size() == 1 && rows[0].id == 33 && rows[0].login == "three" && !rows[0].site_admin,
	        "final typed row was decoded incorrectly");
	Require(!stream->Next(cancellation, rows) && rows.empty(), "stream did not exhaust cleanly");
	Require(state->calls == 1, "batch pulls repeated the HTTP request");
}

void TestStatusAndTransportRedaction() {
	ManualCancellation cancellation;
	const auto plan = live_rest::BuildLiveScanPlan("https://api.github.com");
	std::vector<live_rest::LiveRow> rows;

	const auto oversized_state =
	    Response(200, std::string(static_cast<std::size_t>(plan.max_response_bytes + 1), 'x'));
	auto oversized_stream = OpenStream(oversized_state, plan, cancellation);
	RequireRuntimeError([&]() { oversized_stream->Next(cancellation, rows); }, live_rest::RuntimeStage::RESOURCE,
	                    "");

	const auto status_state = Response(503, "SECRET_STATUS_BODY_CANARY https://secret.invalid/token");
	auto status_stream = OpenStream(status_state, plan, cancellation);
	RequireRuntimeError([&]() { status_stream->Next(cancellation, rows); }, live_rest::RuntimeStage::HTTP_STATUS,
	                    "", {"SECRET_STATUS_BODY_CANARY", "secret.invalid", status_state->body});

	const auto transport_state = Response(200, OneRow());
	transport_state->throw_transport_error = true;
	auto transport_stream = OpenStream(transport_state, plan, cancellation);
	RequireRuntimeError([&]() { transport_stream->Next(cancellation, rows); }, live_rest::RuntimeStage::TRANSPORT,
	                    "", {"SECRET_TRANSPORT_CANARY", "response"});
	Require(!transport_stream->Next(cancellation, rows), "failed request was replayed on a later pull");
	Require(transport_state->calls == 1, "transport failure triggered a retry");
}

void TestCancellationAndCloseLifecycle() {
	const auto plan = live_rest::BuildLiveScanPlan("https://api.github.com");
	std::vector<live_rest::LiveRow> rows;

	ManualCancellation cancelled_before_open;
	cancelled_before_open.Cancel();
	const auto unopened_state = Response(200, OneRow());
	std::unique_ptr<live_rest::HttpTransport> unopened_transport(new FakeTransport(unopened_state));
	const auto executor = live_rest::BuildScanExecutor(std::move(unopened_transport));
	RequireCancellation([&]() { executor->Open(plan, cancelled_before_open); });
	Require(unopened_state->calls == 0, "cancelled Open acquired network authority");

	ManualCancellation cancellation;
	const auto stream_cancel_state = Response(200, OneRow());
	auto cancelled_stream = OpenStream(stream_cancel_state, plan, cancellation);
	cancelled_stream->Cancel();
	cancelled_stream->Cancel();
	RequireCancellation([&]() { cancelled_stream->Next(cancellation, rows); });
	Require(stream_cancel_state->calls == 0, "cancelled stream performed a request");

	const auto close_state = Response(200, OneRow());
	auto closed_stream = OpenStream(close_state, plan, cancellation);
	closed_stream->Close();
	closed_stream->Close();
	Require(!closed_stream->Next(cancellation, rows) && rows.empty(), "closed stream did not remain exhausted");
	Require(close_state->calls == 0, "closed stream performed a request");

	ManualCancellation during_get;
	const auto during_get_state = Response(200, OneRow());
	during_get_state->cancel_during_get = &during_get;
	auto during_get_stream = OpenStream(during_get_state, plan, during_get);
	RequireCancellation([&]() { during_get_stream->Next(during_get, rows); });
	Require(during_get_state->calls == 1 && during_get_state->observed_cancellation,
	        "transport did not observe its call-scoped cancellation view");
}

void TestPlanAndConstructionRefusals() {
	ManualCancellation cancellation;
	std::vector<live_rest::LiveRow> rows;
	const auto state = Response(200, OneRow());
	auto mutated = live_rest::BuildLiveScanPlan("https://api.github.com");
	mutated.redirects_enabled = true;
	std::unique_ptr<live_rest::HttpTransport> transport(new FakeTransport(state));
	const auto executor = live_rest::BuildScanExecutor(std::move(transport));
	RequireRuntimeError([&]() { executor->Open(mutated, cancellation); }, live_rest::RuntimeStage::PLAN, "");
	Require(state->calls == 0, "invalid plan reached the transport");

	bool rejected_null = false;
	try {
		live_rest::BuildScanExecutor(std::unique_ptr<live_rest::HttpTransport>());
	} catch (const std::invalid_argument &) {
		rejected_null = true;
	}
	Require(rejected_null, "executor accepted a null transport");
	(void)rows;
}

} // namespace

int main() {
	try {
		TestRequestShapeAndBatches();
		TestStatusAndTransportRedaction();
		TestCancellationAndCloseLifecycle();
		TestPlanAndConstructionRefusals();
		std::cout << "live REST HTTP scan runtime tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "live REST HTTP scan runtime tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
