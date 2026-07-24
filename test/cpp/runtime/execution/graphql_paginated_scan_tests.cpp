#include "duckdb_api/authorization.hpp"
#include "duckdb_api/internal/runtime/decoding/decoded_page_buffer.hpp"
#include "duckdb_api/internal/runtime/decoding/graphql_response_decoder.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_paginated_scan.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/pagination/graphql_cursor_pagination.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "runtime/support/credential_provider_test_support.hpp"
#include "runtime/support/http_scan_executor_test_support.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ControlledResponse;
using duckdb_api_test::ManualHttpExecutionControl;
using duckdb_api_test::Require;
using duckdb_api_test::RotatingCredentialProvider;

class CancelOnPollControl final : public duckdb_api::ExecutionControl {
public:
	explicit CancelOnPollControl(std::size_t cancel_on_p) : cancel_on(cancel_on_p), polls(0) {
	}

	bool IsCancellationRequested() const noexcept override {
		return ++polls >= cancel_on;
	}

private:
	const std::size_t cancel_on;
	mutable std::size_t polls;
};

class NeverAdmissionCancelled final : public duckdb_api::internal::AdmissionCancellation {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

class TwoPageGateTransport final : public duckdb_api::internal::HttpTransport {
public:
	TwoPageGateTransport(std::string first_p, std::string second_p)
	    : first(std::move(first_p)), second(std::move(second_p)), requests(0), second_entered(false), proceed(false) {
	}

	duckdb_api::internal::HttpResponse Post(const duckdb_api::internal::HttpRequest &,
	                                        const duckdb_api::internal::HttpLimits &,
	                                        duckdb_api::ExecutionControl &) const override {
		std::unique_lock<std::mutex> guard(mutex);
		const auto ordinal = ++requests;
		if (ordinal == 2) {
			second_entered = true;
			condition.notify_all();
			condition.wait(guard, [&]() { return proceed; });
		}
		const auto &source = ordinal == 1 ? first : second;
		auto body = source;
		const auto bytes = static_cast<uint64_t>(body.size());
		return {200, 64, bytes, bytes, std::move(body), {{}, 0, false, {}, {}}};
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &,
	                                       const duckdb_api::internal::HttpLimits &,
	                                       duckdb_api::ExecutionControl &) const override {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "", "unexpected GET in GraphQL fixture");
	}

	bool WaitForSecond(std::chrono::milliseconds timeout) const {
		std::unique_lock<std::mutex> guard(mutex);
		return condition.wait_for(guard, timeout, [&]() { return second_entered; });
	}

	void Proceed() {
		std::lock_guard<std::mutex> guard(mutex);
		proceed = true;
		condition.notify_all();
	}

private:
	const std::string first;
	const std::string second;
	mutable std::mutex mutex;
	mutable std::condition_variable condition;
	mutable uint64_t requests;
	mutable bool second_entered;
	bool proceed;
};

class PostTransportGateControl final : public duckdb_api::ExecutionControl {
public:
	PostTransportGateControl() : armed(false), entered(false), proceed(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		std::unique_lock<std::mutex> guard(mutex);
		if (!armed || proceed) {
			return false;
		}
		entered = true;
		condition.notify_all();
		condition.wait(guard, [&]() { return proceed; });
		return false;
	}

	void Arm() {
		std::lock_guard<std::mutex> guard(mutex);
		armed = true;
	}

	bool WaitUntilEntered(std::chrono::milliseconds timeout) const {
		std::unique_lock<std::mutex> guard(mutex);
		return condition.wait_for(guard, timeout, [&]() { return entered; });
	}

	void Proceed() {
		std::lock_guard<std::mutex> guard(mutex);
		proceed = true;
		condition.notify_all();
	}

private:
	mutable std::mutex mutex;
	mutable std::condition_variable condition;
	bool armed;
	mutable bool entered;
	mutable bool proceed;
};

class PostTransportGateTransport final : public duckdb_api::internal::HttpTransport {
public:
	PostTransportGateTransport(std::string body_p, PostTransportGateControl &gate_p)
	    : body(std::move(body_p)), gate(gate_p), retained_bytes(0) {
	}

	duckdb_api::internal::HttpResponse Post(const duckdb_api::internal::HttpRequest &,
	                                        const duckdb_api::internal::HttpLimits &,
	                                        duckdb_api::ExecutionControl &) const override {
		auto response_body = body;
		retained_bytes.store(duckdb_api::internal::RetainedHttpStringAllocationBytes(response_body),
		                     std::memory_order_release);
		const auto bytes = static_cast<uint64_t>(response_body.size());
		gate.Arm();
		return {200, 64, bytes, bytes, std::move(response_body), {{}, 0, false, {}, {}}};
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &,
	                                       const duckdb_api::internal::HttpLimits &,
	                                       duckdb_api::ExecutionControl &) const override {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "", "unexpected GET in GraphQL fixture");
	}

	uint64_t RetainedBytes() const noexcept {
		return retained_bytes.load(std::memory_order_acquire);
	}

private:
	const std::string body;
	PostTransportGateControl &gate;
	mutable std::atomic<uint64_t> retained_bytes;
};

std::string Node(uint64_t id) {
	return std::string("{\"id\":\"R") + std::to_string(id) + "\",\"nameWithOwner\":\"owner/repository-" +
	       std::to_string(id) + "\",\"owner\":{\"login\":\"owner\"},\"stargazerCount\":" + std::to_string(id) +
	       ",\"primaryLanguage\":" + (id % 2 == 0 ? "null" : "{\"name\":\"Rust\"}") +
	       ",\"isPrivate\":false,\"isArchived\":false,\"updatedAt\":\"2026-01-01T00:00:00Z\"}";
}

std::string Page(uint64_t first, uint64_t count, bool has_next, const std::string &cursor) {
	std::string result = "{\"data\":{\"viewer\":{\"repositories\":{\"nodes\":[";
	for (uint64_t index = 0; index < count; index++) {
		if (index != 0) {
			result += ",";
		}
		result += Node(first + index);
	}
	result += "],\"pageInfo\":{\"hasNextPage\":";
	result += has_next ? "true" : "false";
	result += ",\"endCursor\":";
	result += cursor.empty() ? "null" : "\"" + cursor + "\"";
	result += "}}}},\"errors\":[]}";
	return result;
}

std::unique_ptr<duckdb_api::BatchStream> Open(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
                                              ManualHttpExecutionControl &control, uint64_t suffix) {
	auto token = duckdb_api_test::GeneratedHttpBearerToken(suffix);
	runtime->ExpectBearer("Bearer " + token);
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("graphql_secret");
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenPlanWithToken(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
                  ManualHttpExecutionControl &control, const duckdb_api::ScanPlan &plan, std::string token) {
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenAnonymous(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
              ManualHttpExecutionControl &control) {
	return runtime->Executor()->Open(duckdb_api_test::BuildValidAnonymousGraphqlScanPlanFixture(), control);
}

void RequireTerminalFailure(duckdb_api::BatchStream &stream, ManualHttpExecutionControl &control,
                            duckdb_api::ErrorStage stage, const std::string &field = "") {
	duckdb_api::TypedBatch batch;
	for (std::size_t attempt = 0; attempt < 2; attempt++) {
		try {
			(void)stream.Next(control, batch);
			throw std::runtime_error("GraphQL late failure must remain terminal");
		} catch (const duckdb_api::ExecutionError &error) {
			Require(error.Stage() == stage && (field.empty() || error.Field() == field),
			        "GraphQL late failure used the wrong stable stage or field");
		}
	}
}

void TestSequentialBodiesBackpressureAndNullableRows() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 100, true, "cursor-100")), ControlledResponse(200, Page(101, 1, false, ""))});
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 801);
	Require(runtime->Observations().empty(), "GraphQL Open performed request-body construction or transport I/O");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 64 && batch.column_types.size() == 8 &&
	            batch.IsSchemaAligned() && runtime->Observations().size() == 1,
	        "first GraphQL pull must publish 64 rows without prefetch");
	Require(batch.rows[0].values[0].varchar_value == "R1" && batch.rows[0].values[3].bigint_value == 1 &&
	            batch.rows[0].values[4].valid && !batch.rows[1].values[4].valid,
	        "GraphQL stream lost scalar or nullable value semantics");
	Require(stream->Next(control, batch) && batch.rows.size() == 36 && runtime->Observations().size() == 1,
	        "second pull must drain page one before requesting page two");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].varchar_value == "R101" &&
	            runtime->Observations().size() == 2,
	        "page two did not wait for page-one backpressure");
	Require(!stream->Next(control, batch) && batch.rows.empty(), "terminal GraphQL page did not exhaust cleanly");

	const auto observations = runtime->Observations();
	Require(observations[0].method == "POST" && observations[0].target == "/graphql" &&
	            observations[0].content_type == "application/json" &&
	            observations[0].body.find("\"cursor\":null") != std::string::npos &&
	            observations[1].body.find("\"cursor\":\"cursor-100\"") != std::string::npos,
	        "GraphQL cursor bodies were not canonical across pages");
	for (std::size_t index = 0; index < observations.size(); index++) {
		Require(observations[index].headers.size() == 4 && observations[index].headers[3].first == "Authorization" &&
		            observations[index].headers[3].second == "<redacted>" &&
		            observations[index].max_request_body_bytes == 8 * 1024 &&
		            observations[index].max_metadata_bytes == 0,
		        "GraphQL request authority, redaction, or body allowance drifted");
	}
	Require(runtime->ConsumeBearerExpectation(2), "GraphQL pages did not retain one isolated authorization capability");
}

void TestCredentialProviderSnapshotPersistsAcrossGraphqlPages() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 1, true, "provider-cursor")), ControlledResponse(200, Page(2, 1, false, ""))});
	ManualHttpExecutionControl control;
	auto initial = duckdb_api_test::GeneratedHttpBearerToken(820);
	auto replacement = duckdb_api_test::GeneratedHttpBearerToken(821);
	RotatingCredentialProvider provider(initial);
	runtime->ExpectBearer("Bearer " + initial);
	auto stream = runtime->Executor()->OpenWithCredentialProvider(
	    duckdb_api_test::BuildValidGraphqlScanPlanFixture("graphql_secret"), provider, control);
	Require(provider.ResolveCount() == 1 && runtime->Observations().empty(),
	        "GraphQL provider open did not resolve once before transport");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows[0].values[0].varchar_value == "R1" &&
	            runtime->Observations().size() == 1,
	        "provider-backed GraphQL scan did not emit its first page");
	provider.Replace(replacement);
	Require(stream->Next(control, batch) && batch.rows[0].values[0].varchar_value == "R2" &&
	            !stream->Next(control, batch),
	        "provider replacement interrupted the existing GraphQL snapshot");
	Require(provider.ResolveCount() == 1 && runtime->Observations().size() == 2 && runtime->ConsumeBearerExpectation(2),
	        "GraphQL scan re-resolved or changed credential between pages");
}

void TestLateOpenCancellationRemainsCancellation() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("graphql_secret");
	CancelOnPollControl direct_control(2);
	bool direct_cancelled = false;
	try {
		(void)runtime->Executor()->OpenWithAuthorization(
		    plan, duckdb_api::ScanAuthorization::GithubUserBearer("graphql_direct_cancel"), direct_control);
	} catch (const duckdb_api::ExecutionCancelled &) {
		direct_cancelled = true;
	}
	Require(direct_cancelled && runtime->Observations().empty(),
	        "late direct GraphQL cancellation was relabeled or reached transport");

	RotatingCredentialProvider provider("graphql_provider_cancel");
	// Provider admission adds pre/post-grant cancellation checkpoints before
	// the existing pre/post-resolution pair. Cancel after the provider returns
	// to prove the acquired resolution permit is released on that late edge.
	CancelOnPollControl provider_control(5);
	bool provider_cancelled = false;
	try {
		(void)runtime->Executor()->OpenWithCredentialProvider(plan, provider, provider_control);
	} catch (const duckdb_api::ExecutionCancelled &) {
		provider_cancelled = true;
	}
	Require(provider_cancelled && provider.ResolveCount() == 1 && runtime->Observations().empty(),
	        "late provider-backed GraphQL cancellation was relabeled or reached transport");
}

void TestAnonymousExecutionAndAuthorizationAlternatives() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, Page(1, 1, false, ""));
	ManualHttpExecutionControl control;
	auto stream = OpenAnonymous(runtime, control);
	Require(runtime->Observations().empty(), "anonymous GraphQL Open performed transport I/O");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].varchar_value == "R1" &&
	            !stream->Next(control, batch),
	        "anonymous GraphQL traversal did not preserve the admitted result contract");
	const auto observations = runtime->Observations();
	Require(observations.size() == 1 && observations[0].headers.size() == 3,
	        "anonymous GraphQL request acquired an authorization header");
	for (const auto &header : observations[0].headers) {
		Require(header.first != "Authorization", "anonymous GraphQL request emitted bearer authority");
	}

	const auto mismatch = duckdb_api_test::BuildControlledHttpRuntime();
	bool anonymous_for_bearer_rejected = false;
	try {
		(void)mismatch->Executor()->OpenWithAuthorization(
		    duckdb_api_test::BuildValidGraphqlScanPlanFixture("mismatch_secret"),
		    duckdb_api::ScanAuthorization::Anonymous(), control);
	} catch (const duckdb_api::ExecutionError &error) {
		anonymous_for_bearer_rejected = error.Stage() == duckdb_api::ErrorStage::AUTHENTICATION;
	}
	bool bearer_for_anonymous_rejected = false;
	try {
		(void)mismatch->Executor()->OpenWithAuthorization(
		    duckdb_api_test::BuildValidAnonymousGraphqlScanPlanFixture(),
		    duckdb_api::ScanAuthorization::Bearer(std::string("surplus_graphql_token")), control);
	} catch (const duckdb_api::ExecutionError &error) {
		bearer_for_anonymous_rejected = error.Stage() == duckdb_api::ErrorStage::AUTHENTICATION;
	}
	Require(anonymous_for_bearer_rejected && bearer_for_anonymous_rejected && mismatch->Observations().empty(),
	        "GraphQL authorization alternative mismatch reached transport or used the wrong stage");
}

void TestRemoteErrorIsRedactedAndTerminal() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	const std::string canary = "graphql-private-remote-message";
	runtime->Respond(200, "{\"data\":null,\"errors\":[{\"message\":\"" + canary + "\"}]}");
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 802);
	duckdb_api::TypedBatch batch;
	for (std::size_t attempt = 0; attempt < 2; attempt++) {
		try {
			(void)stream->Next(control, batch);
			throw std::runtime_error("GraphQL remote error must remain terminal");
		} catch (const duckdb_api::ExecutionError &error) {
			Require(error.Stage() == duckdb_api::ErrorStage::REMOTE_PROTOCOL && error.Field() == "errors" &&
			            error.SafeMessage() == "remote protocol response reported application errors" &&
			            error.SafeMessage().find(canary) == std::string::npos,
			        "GraphQL remote failure stage, field, redaction, or terminal replay drifted");
		}
	}
	Require(runtime->Observations().size() == 1 && runtime->ConsumeBearerExpectation(1),
	        "terminal GraphQL failure performed another request or lost bearer isolation");
}

void TestCloseBeforePullIsSideEffectFree() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, Page(1, 1, false, ""));
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 803);
	stream->Close();
	stream->Close();
	duckdb_api::TypedBatch batch;
	Require(!stream->Next(control, batch) && runtime->Observations().empty(),
	        "close before pull must release GraphQL capability without body or transport work");
}

void TestEmptyPageAdvancesWithoutPublishingFalseExhaustion() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 0, true, "empty-next")), ControlledResponse(200, Page(7, 1, false, ""))});
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 804);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].varchar_value == "R7" &&
	            runtime->Observations().size() == 2,
	        "empty nonterminal GraphQL page became false exhaustion or a published empty batch");
	Require(!stream->Next(control, batch), "empty-page sequence did not exhaust after its terminal row");
	Require(runtime->ConsumeBearerExpectation(2), "empty page changed the stream authorization capability");
}

void TestLateStatusTransportDecodeAndCursorFailuresAreTerminal() {
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;

	const auto status_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	status_runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 1, true, "status-next")), ControlledResponse(429, "private-status")});
	auto status = Open(status_runtime, control, 805);
	Require(status->Next(control, batch), "late-status fixture did not publish its first page");
	RequireTerminalFailure(*status, control, duckdb_api::ErrorStage::HTTP_STATUS);
	Require(status_runtime->Observations().size() == 2 && status_runtime->ConsumeBearerExpectation(2),
	        "late status failure retried or changed bearer authority");

	const auto transport_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	transport_runtime->RespondSequence({ControlledResponse(200, Page(1, 1, true, "transport-next")),
	                                    duckdb_api_test::ControlledTransportFailure("private transport canary")});
	auto transport = Open(transport_runtime, control, 806);
	Require(transport->Next(control, batch), "late-transport fixture did not publish its first page");
	RequireTerminalFailure(*transport, control, duckdb_api::ErrorStage::TRANSPORT);
	Require(transport_runtime->Observations().size() == 2 && transport_runtime->ConsumeBearerExpectation(2),
	        "late transport failure retried or changed bearer authority");

	const auto decode_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	decode_runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 1, true, "decode-next")), ControlledResponse(200, "{\"data\":[}")});
	auto decode = Open(decode_runtime, control, 807);
	Require(decode->Next(control, batch), "late-decode fixture did not publish its first page");
	RequireTerminalFailure(*decode, control, duckdb_api::ErrorStage::DECODE);
	Require(decode_runtime->Observations().size() == 2 && decode_runtime->ConsumeBearerExpectation(2),
	        "late decode failure retried or changed bearer authority");

	const auto cursor_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	cursor_runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 1, true, "repeat")), ControlledResponse(200, Page(2, 1, true, "repeat"))});
	auto cursor = Open(cursor_runtime, control, 808);
	Require(cursor->Next(control, batch) && batch.rows[0].values[0].varchar_value == "R1",
	        "repeated-cursor fixture did not publish only its committed first page");
	RequireTerminalFailure(*cursor, control, duckdb_api::ErrorStage::POLICY, "pagination.cursor");
	Require(cursor_runtime->Observations().size() == 2 && cursor_runtime->ConsumeBearerExpectation(2),
	        "repeated cursor was published, retried, or changed bearer authority");
}

void TestCancellationCloseAndDestructionAfterPartialOutput() {
	duckdb_api::TypedBatch batch;
	const auto pre_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	pre_runtime->Respond(200, Page(1, 1, false, ""));
	ManualHttpExecutionControl pre_control;
	auto pre = Open(pre_runtime, pre_control, 809);
	pre_control.Cancel();
	bool pre_cancelled = false;
	try {
		(void)pre->Next(pre_control, batch);
	} catch (const duckdb_api::ExecutionCancelled &) {
		pre_cancelled = true;
	}
	Require(pre_cancelled && pre_runtime->Observations().empty(),
	        "pre-request cancellation reached bearer placement or transport");

	const auto between_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	between_runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 1, true, "between-next")), ControlledResponse(200, Page(2, 1, false, ""))});
	ManualHttpExecutionControl between_control;
	auto between = Open(between_runtime, between_control, 810);
	Require(between->Next(between_control, batch), "between-page cancellation fixture lost its first row");
	between->Cancel();
	bool between_cancelled = false;
	try {
		(void)between->Next(between_control, batch);
	} catch (const duckdb_api::ExecutionCancelled &) {
		between_cancelled = true;
	}
	Require(between_cancelled && between_runtime->Observations().size() == 1 &&
	            between_runtime->ConsumeBearerExpectation(1),
	        "between-page cancellation became exhaustion or performed a second request");

	const auto close_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	close_runtime->RespondSequence(
	    {ControlledResponse(200, Page(1, 100, true, "close-next")), ControlledResponse(200, Page(101, 1, false, ""))});
	ManualHttpExecutionControl close_control;
	auto closed = Open(close_runtime, close_control, 811);
	Require(closed->Next(close_control, batch) && batch.rows.size() == 64,
	        "partial-close fixture did not publish its first bounded batch");
	closed->Close();
	closed->Close();
	Require(!closed->Next(close_control, batch) && close_runtime->Observations().size() == 1 &&
	            close_runtime->ConsumeBearerExpectation(1),
	        "partial close retained rows, authorization, or next-page authority");

	const auto destroy_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	destroy_runtime->RespondSequence({ControlledResponse(200, Page(1, 100, true, "destroy-next")),
	                                  ControlledResponse(200, Page(101, 1, false, ""))});
	ManualHttpExecutionControl destroy_control;
	{
		auto destroyed = Open(destroy_runtime, destroy_control, 812);
		Require(destroyed->Next(destroy_control, batch) && batch.rows.size() == 64,
		        "destruction fixture did not publish its first bounded batch");
	}
	Require(destroy_runtime->Observations().size() == 1 && destroy_runtime->ConsumeBearerExpectation(1),
	        "destruction after partial output performed a later request or retained bearer authority");
}

void TestTransportCancellationAndTwoStreamIsolation() {
	const auto blocked_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	blocked_runtime->BlockUntilCancelled();
	ManualHttpExecutionControl blocked_control;
	auto blocked = Open(blocked_runtime, blocked_control, 813);
	duckdb_api::TypedBatch blocked_batch;
	std::atomic<bool> saw_cancel(false);
	std::thread blocked_worker([&]() {
		try {
			(void)blocked->Next(blocked_control, blocked_batch);
		} catch (const duckdb_api::ExecutionCancelled &) {
			saw_cancel.store(true, std::memory_order_release);
		}
	});
	const auto reached_transport = blocked_runtime->WaitForRequestCount(1, std::chrono::seconds(2));
	blocked_control.Cancel();
	blocked_worker.join();
	Require(reached_transport && saw_cancel.load(std::memory_order_acquire) && blocked_batch.rows.empty(),
	        "transport cancellation published rows or missed its cancellation checkpoint");

	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	ManualHttpExecutionControl first_control;
	ManualHttpExecutionControl second_control;
	const std::string first_token = "graphql_stream_token_one";
	const std::string second_token = "graphql_stream_token_two";
	runtime->RespondWithBearerBarrier("Bearer " + first_token, Page(11, 1, false, ""), "Bearer " + second_token,
	                                  Page(22, 1, false, ""));
	RotatingCredentialProvider first_provider(first_token, 0x41);
	RotatingCredentialProvider second_provider(second_token, 0x42);
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("graphql_secret");
	// The admitted maximum wire/decompression envelope intentionally permits only one request in an exact
	// 32 MiB bulkhead. Exercise independent stream state across two exact provider-principal bulkheads so both requests
	// may overlap without weakening the accepted per-bulkhead byte ceiling.
	auto first = runtime->Executor()->OpenWithCredentialProvider(plan, first_provider, first_control);
	auto second = runtime->Executor()->OpenWithCredentialProvider(plan, second_provider, second_control);
	duckdb_api::TypedBatch first_batch;
	duckdb_api::TypedBatch second_batch;
	std::atomic<bool> first_ok(false);
	std::atomic<bool> second_ok(false);
	std::thread first_worker([&]() {
		try {
			first_ok.store(first->Next(first_control, first_batch), std::memory_order_release);
		} catch (...) {
		}
	});
	std::thread second_worker([&]() {
		try {
			second_ok.store(second->Next(second_control, second_batch), std::memory_order_release);
		} catch (...) {
		}
	});
	const auto overlapped = runtime->WaitForRequestCount(2, std::chrono::seconds(2));
	runtime->ReleaseBearerBarrier();
	first_worker.join();
	second_worker.join();
	Require(overlapped && first_ok.load(std::memory_order_acquire) && second_ok.load(std::memory_order_acquire) &&
	            first_batch.rows[0].values[0].varchar_value == "R11" &&
	            second_batch.rows[0].values[0].varchar_value == "R22",
	        "concurrent GraphQL streams crossed cursor, body, response, or bearer state");
}

void TestIntegratedThirtyTwoPageBoundary() {
	std::vector<duckdb_api_test::ControlledHttpResponse> terminal_pages;
	for (uint64_t page = 1; page <= 32; page++) {
		terminal_pages.push_back(
		    ControlledResponse(200, Page(page, 0, page != 32, page == 32 ? "" : "cursor-" + std::to_string(page))));
	}
	const auto terminal_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	terminal_runtime->RespondSequence(std::move(terminal_pages));
	ManualHttpExecutionControl control;
	auto terminal = Open(terminal_runtime, control, 814);
	duckdb_api::TypedBatch batch;
	Require(!terminal->Next(control, batch) && terminal_runtime->Observations().size() == 32 &&
	            terminal_runtime->ConsumeBearerExpectation(32),
	        "32-page exact terminal boundary did not exhaust cleanly and sequentially");

	std::vector<duckdb_api_test::ControlledHttpResponse> over_pages;
	for (uint64_t page = 1; page <= 32; page++) {
		over_pages.push_back(ControlledResponse(200, Page(page, 0, true, "over-" + std::to_string(page))));
	}
	const auto over_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	over_runtime->RespondSequence(std::move(over_pages));
	auto over = Open(over_runtime, control, 815);
	RequireTerminalFailure(*over, control, duckdb_api::ErrorStage::RESOURCE, "pages");
	Require(over_runtime->Observations().size() == 32 && over_runtime->ConsumeBearerExpectation(32),
	        "common page-accounting denial performed another request or replayed its terminal failure");
}

void TestCursorTransferExactDecodedMemoryBoundary() {
	ManualHttpExecutionControl control;
	const std::string cursor_value(512, 'c');
	const auto body = Page(1, 1, true, cursor_value);
	const auto baseline_plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("boundary_secret");
	const duckdb_api::internal::HttpExecutionProfile profile {
	    duckdb_api::PlannedUrlScheme::HTTPS,
	    "api.github.com",
	    443,
	    false,
	    false,
	    false,
	    30000,
	    100,
	    duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP,
	    duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN,
	    duckdb_api::RETRY_MAX_DELAY_MILLISECONDS,
	    duckdb_api::RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN};
	auto admitted = duckdb_api::internal::TryAdmitGraphqlPlan(baseline_plan, profile);
	Require(static_cast<bool>(admitted), "GraphQL cursor boundary fixture did not admit its baseline plan");
	const duckdb_api::internal::GraphqlDecodeLimits limits {
	    admitted->PageBudgets().decoded_records, admitted->PageBudgets().extracted_string_bytes,
	    admitted->PageBudgets().json_nesting, admitted->PageBudgets().decoded_memory_bytes,
	    std::chrono::steady_clock::now() + std::chrono::seconds(5)};
	auto decoded = duckdb_api::internal::DecodeGraphqlResponse(body, *admitted, limits, control);
	duckdb_api::internal::GraphqlCursorState cursor(admitted->MaxPages(),
	                                                admitted->PageBudgets().extracted_string_bytes);
	const auto cursor_before = cursor.RetainedMemoryBytes();
	cursor.MarkRequestStarted();
	cursor.Advance(decoded.has_next, std::move(decoded.end_cursor));
	const auto cursor_after = cursor.RetainedMemoryBytes();
	const auto decode_peak = cursor_before + decoded.peak_memory_bytes;
	const auto handoff =
	    duckdb_api::internal::TypedBatchHandoffMemoryBytes(decoded.rows.size(), admitted->Columns().size());
	const auto handoff_peak = decoded.retained_memory_bytes + cursor_after + handoff;
	const auto exact_bytes = std::max(decode_peak, handoff_peak);
	Require(exact_bytes > 1 && decoded.cursor_memory_bytes > 0,
	        "GraphQL cursor boundary fixture did not retain a decoded cursor");

	const auto exact_plan =
	    duckdb_api_test::BuildGraphqlDecodedMemoryBoundaryPlanFixture("boundary_secret", exact_bytes);
	const auto exact_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	exact_runtime->Respond(200, body);
	const std::string exact_token = "graphql_cursor_exact_boundary";
	exact_runtime->ExpectBearer("Bearer " + exact_token);
	auto exact_stream = OpenPlanWithToken(exact_runtime, control, exact_plan, exact_token);
	duckdb_api::TypedBatch batch;
	Require(exact_stream->Next(control, batch) && batch.rows.size() == 1 &&
	            batch.rows[0].values[0].varchar_value == "R1" && exact_runtime->ConsumeBearerExpectation(1),
	        "GraphQL cursor ownership transfer rejected its exact physical decoded-memory boundary");
	exact_stream->Close();

	const auto one_under_plan =
	    duckdb_api_test::BuildGraphqlDecodedMemoryBoundaryPlanFixture("boundary_secret", exact_bytes - 1);
	const auto one_under_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	one_under_runtime->Respond(200, body);
	const std::string one_under_token = "graphql_cursor_one_under_boundary";
	one_under_runtime->ExpectBearer("Bearer " + one_under_token);
	auto one_under_stream = OpenPlanWithToken(one_under_runtime, control, one_under_plan, one_under_token);
	bool rejected = false;
	try {
		(void)one_under_stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "decoded_memory_bytes";
	}
	Require(rejected && one_under_runtime->ConsumeBearerExpectation(1),
	        "GraphQL cursor ownership transfer accepted one byte below its physical peak");

	// Drive the actual page-drain branch. While page two is blocked in
	// transport, the page-one decode and handoff reservations must be gone, the
	// same live page permit must hold the exact cursor heap, and the only other
	// bytes may be the page-two attempt envelope.
	auto direct_admitted = duckdb_api::internal::TryAdmitGraphqlPlan(baseline_plan, profile);
	Require(static_cast<bool>(direct_admitted), "direct GraphQL cursor-transfer fixture did not admit");
	const auto &page_budget = direct_admitted->PageBudgets();
	const duckdb_api::internal::PageResourceLimits page_limits {
	    direct_admitted->ResiliencePolicy().max_attempts_per_step,
	    page_budget.header_bytes,
	    page_budget.response_bytes,
	    page_budget.decompressed_bytes,
	    page_budget.decoded_records,
	    page_budget.decoded_memory_bytes,
	    page_budget.concurrency,
	    page_budget.serialized_request_body_bytes};
	const auto metadata_budget = direct_admitted->RateLimitPolicy().WaitingEnabled()
	                                 ? std::min(page_budget.header_bytes, page_budget.decoded_memory_bytes)
	                                 : 0;
	const auto attempt_bytes =
	    duckdb_api::internal::rate_limit_detail::AttemptBufferEnvelope(page_limits, metadata_budget);
	auto clock = duckdb_api::internal::NewSystemRateLimitClock();
	auto controller = std::make_shared<duckdb_api::internal::AdmissionController>(
	    duckdb_api::internal::AdmissionProfile::Hard(), clock);
	const auto identity = duckdb_api::internal::AdmissionIdentity::Complete(
	    "graphql_cursor_transfer", {"https", direct_admitted->Host(), direct_admitted->Port()}, "repositories",
	    duckdb_api::internal::AdmissionProtocol::GRAPHQL, "repositories",
	    duckdb_api::internal::AdmissionPrincipalToken::Direct(duckdb_api::internal::AdmissionDirectPrincipal::BEARER));
	duckdb_api::internal::AdmissionController::Permit scan_permit;
	duckdb_api::internal::AdmissionObservation observation {};
	NeverAdmissionCancelled admission_cancellation;
	const auto now = clock->SteadyNowMilliseconds();
	const duckdb_api::internal::AdmissionWaitPolicy wait {now + 1000, false, 0, false, 0};
	Require(controller->AcquireScan(identity, wait, admission_cancellation, &scan_permit, &observation) ==
	            duckdb_api::internal::AdmissionAcquireStatus::ACQUIRED,
	        "direct GraphQL cursor-transfer fixture did not acquire scan authority");
	auto coordinator = std::make_shared<duckdb_api::internal::RateLimitCoordinator>(
	    duckdb_api::internal::RateLimitCoordinator::HardLimits(), clock);
	duckdb_api::internal::RateLimitRuntimeContext rate_runtime(
	    coordinator, clock, duckdb_api::internal::RateLimitPrincipalToken::Anonymous());
	duckdb_api::internal::AdmissionRuntimeContext admission_runtime(controller, identity);
	auto gated_transport = std::make_shared<TwoPageGateTransport>(body, Page(2, 1, false, ""));
	std::shared_ptr<const duckdb_api::internal::HttpTransport> transport = gated_transport;
	auto direct_stream = duckdb_api::internal::OpenGraphqlPaginatedScan(
	    std::move(direct_admitted), duckdb_api::ScanAuthorization::GithubUserBearer("direct_cursor_transfer_token"),
	    transport, 30000, std::move(rate_runtime), std::move(admission_runtime), std::move(scan_permit), control);
	duckdb_api::TypedBatch direct_batch;
	Require(direct_stream->Next(control, direct_batch) && direct_batch.rows.size() == 1,
	        "direct GraphQL cursor-transfer fixture did not publish page one");
	bool second_ok = false;
	std::exception_ptr second_error;
	std::thread second_pull([&]() {
		try {
			duckdb_api::TypedBatch next;
			second_ok = direct_stream->Next(control, next) && next.rows.size() == 1;
		} catch (...) {
			second_error = std::current_exception();
		}
	});
	const auto reached_second = gated_transport->WaitForSecond(std::chrono::seconds(2));
	const auto during_transfer = controller->Usage();
	gated_transport->Proceed();
	second_pull.join();
	const auto page_two_usage = controller->Usage();
	if (second_error) {
		std::rethrow_exception(second_error);
	}
	Require(reached_second && during_transfer.active_scans == 1 && during_transfer.in_flight_requests == 1 &&
	            during_transfer.buffered_rows == 0 && during_transfer.buffered_bytes == cursor_after + attempt_bytes,
	        "page drain did not atomically narrow to exact cursor bytes before page-two attempt growth");
	Require(second_ok, "direct GraphQL cursor-transfer fixture did not publish page two");
	const auto page_two_handoff = duckdb_api::internal::TypedBatchHandoffMemoryBytes(1, admitted->Columns().size());
	Require(page_two_usage.in_flight_requests == 0 &&
	            page_two_usage.buffered_bytes == page_limits.decoded_memory_bytes + page_two_handoff &&
	            page_two_usage.buffered_rows == page_limits.decoded_records + 1,
	        "page two was not fully reserved before decode and published handoff");
	Require(!direct_stream->Next(control, direct_batch), "terminal GraphQL cursor page did not exhaust");
	direct_stream->Close();
	const auto drained = controller->Usage();
	Require(drained.active_scans == 0 && drained.in_flight_requests == 0 && drained.buffered_bytes == 0 &&
	            drained.buffered_rows == 0,
	        "GraphQL cursor-transfer completion retained admission authority");

	// Gate the first caller checkpoint after a complete transport return. The
	// attempt envelope and request are already gone, while decode has not yet
	// reserved its page allowance, so only the response's verified pinned heap
	// may remain charged.
	auto narrowing_admitted = duckdb_api::internal::TryAdmitGraphqlPlan(baseline_plan, profile);
	Require(static_cast<bool>(narrowing_admitted), "response-narrowing GraphQL fixture did not admit");
	duckdb_api::internal::AdmissionController::Permit narrowing_scan_permit;
	const auto narrowing_now = clock->SteadyNowMilliseconds();
	const duckdb_api::internal::AdmissionWaitPolicy narrowing_wait {narrowing_now + 1000, false, 0, false, 0};
	Require(controller->AcquireScan(identity, narrowing_wait, admission_cancellation, &narrowing_scan_permit,
	                                &observation) == duckdb_api::internal::AdmissionAcquireStatus::ACQUIRED,
	        "response-narrowing GraphQL fixture did not acquire scan authority");
	duckdb_api::internal::RateLimitRuntimeContext narrowing_rate_runtime(
	    coordinator, clock, duckdb_api::internal::RateLimitPrincipalToken::Anonymous());
	duckdb_api::internal::AdmissionRuntimeContext narrowing_admission_runtime(controller, identity);
	PostTransportGateControl narrowing_control;
	auto narrowing_transport = std::make_shared<PostTransportGateTransport>(Page(1, 1, false, ""), narrowing_control);
	std::shared_ptr<const duckdb_api::internal::HttpTransport> narrowing_transport_view = narrowing_transport;
	auto narrowing_stream = duckdb_api::internal::OpenGraphqlPaginatedScan(
	    std::move(narrowing_admitted),
	    duckdb_api::ScanAuthorization::GithubUserBearer("direct_response_narrowing_token"), narrowing_transport_view,
	    30000, std::move(narrowing_rate_runtime), std::move(narrowing_admission_runtime),
	    std::move(narrowing_scan_permit), narrowing_control);
	bool narrowing_ok = false;
	std::exception_ptr narrowing_error;
	std::thread narrowing_pull([&]() {
		try {
			duckdb_api::TypedBatch next;
			narrowing_ok = narrowing_stream->Next(narrowing_control, next) && next.rows.size() == 1;
		} catch (...) {
			narrowing_error = std::current_exception();
		}
	});
	const auto reached_narrowed_response = narrowing_control.WaitUntilEntered(std::chrono::seconds(2));
	const auto narrowed_usage = controller->Usage();
	const auto exact_response_bytes = narrowing_transport->RetainedBytes();
	narrowing_control.Proceed();
	narrowing_pull.join();
	if (narrowing_error) {
		std::rethrow_exception(narrowing_error);
	}
	Require(reached_narrowed_response && exact_response_bytes > 0 && narrowed_usage.active_scans == 1 &&
	            narrowed_usage.in_flight_requests == 0 && narrowed_usage.buffered_rows == 0 &&
	            narrowed_usage.buffered_bytes == exact_response_bytes,
	        "complete response did not atomically narrow its worst-case attempt envelope to exact pinned storage");
	Require(narrowing_ok, "response-narrowing GraphQL fixture did not decode after the gated checkpoint");
	duckdb_api::TypedBatch terminal_batch;
	Require(!narrowing_stream->Next(narrowing_control, terminal_batch),
	        "response-narrowing GraphQL fixture did not exhaust");
	narrowing_stream->Close();
	const auto narrowing_drained = controller->Usage();
	Require(narrowing_drained.active_scans == 0 && narrowing_drained.in_flight_requests == 0 &&
	            narrowing_drained.buffered_bytes == 0 && narrowing_drained.buffered_rows == 0,
	        "response-narrowing GraphQL fixture retained admission authority");
}

void TestMidScanFailureExposure() {
	// RFC 0021: a failure after page one emitted rows must carry the cumulative
	// exposure (rows_exposed) and the in-flight step ordinal, proving the
	// catch-boundary enrichment is accurate at a real mid-scan failure.
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, Page(1, 64, true, "cursor-64")), ControlledResponse(429, "")});
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 900);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 64,
	        "page one did not emit its rows before the mid-scan failure");
	bool failed = false;
	try {
		(void)stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		failed = true;
		Require(error.Classified(), "mid-scan failure was not classified");
		Require(error.Properties().failure_class == duckdb_api::FailureClass::RATE_LIMIT,
		        "429 mid-scan failure was not classified as rate_limit");
		Require(error.Properties().rows_exposed == 64,
		        "mid-scan failure did not carry the cumulative rows exposed from page one");
		Require(error.Properties().step == 2, "mid-scan failure did not carry the in-flight traversal-step ordinal");
	}
	Require(failed, "page two 429 did not terminate the scan");
}

void TestSameGraphqlPagePostExposureFailureIsNeverReplayable() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime(100);
	runtime->Respond(200, Page(1, 100, false, ""));
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 901);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 64 &&
	            stream->Diagnostics().exposure_state == duckdb_api::ExposureState::EXPOSED,
	        "GraphQL fixture did not cross its same-page emission boundary");
	std::this_thread::sleep_for(std::chrono::milliseconds(120));
	bool failed = false;
	try {
		(void)stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		failed = error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Classified() &&
		         error.Properties().exposure_state == duckdb_api::ExposureState::EXPOSED &&
		         error.Properties().rows_exposed == 64 &&
		         error.Properties().replay_classification == duckdb_api::ReplayClassification::NEVER_REPLAYABLE;
	}
	Require(failed && stream->Diagnostics().exposure_state == duckdb_api::ExposureState::EXPOSED,
	        "same-page post-exposure GraphQL failure retained replay authority or regressed diagnostics");
}

void TestRetryEnabledGraphqlUsesOneCredentialSnapshotAndAtomicPage() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(503, ""),
	                          duckdb_api_test::ControlledTransientTransportFailure(
	                              duckdb_api::internal::HttpTransportFailureKind::EMPTY_RESPONSE),
	                          ControlledResponse(200, Page(1, 2, false, ""))});
	ManualHttpExecutionControl control;
	const std::string token = "graphql_retry_snapshot_token";
	runtime->ExpectBearer("Bearer " + token);
	auto stream = OpenPlanWithToken(runtime, control,
	                                duckdb_api_test::BuildRetryEnabledGraphqlScanPlanFixture("retry_secret"), token);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 2 && runtime->Observations().size() == 3 &&
	            runtime->ConsumeBearerExpectation(3),
	        "GraphQL retry did not rebuild three attempts from one credential snapshot");
	const auto diagnostics = stream->Diagnostics();
	Require(diagnostics.aggregate_attempts == 3 && diagnostics.current_step == 1 &&
	            diagnostics.cumulative_delay_milliseconds > 0 &&
	            diagnostics.exposure_state == duckdb_api::ExposureState::EXPOSED,
	        "GraphQL recovery did not preserve atomic-page structured diagnostics");
	Require(!stream->Next(control, batch) && stream->Diagnostics().exposure_state == duckdb_api::ExposureState::EXPOSED,
	        "GraphQL exhaustion regressed the terminal step's exposed diagnostics");
	stream->Cancel();
	Require(stream->Diagnostics().exposure_state == duckdb_api::ExposureState::EXPOSED,
	        "post-exhaustion GraphQL cancellation regressed exposed diagnostics");

	const auto malformed_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	malformed_runtime->Respond(200, "{");
	const std::string malformed_token = "graphql_retry_decode_token";
	malformed_runtime->ExpectBearer("Bearer " + malformed_token);
	auto malformed = OpenPlanWithToken(malformed_runtime, control,
	                                   duckdb_api_test::BuildRetryEnabledGraphqlScanPlanFixture("retry_decode_secret"),
	                                   malformed_token);
	bool failed = false;
	try {
		(void)malformed->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		failed = error.Stage() == duckdb_api::ErrorStage::DECODE && error.Classified() &&
		         error.Properties().exposure_state == duckdb_api::ExposureState::UNACCEPTED;
	}
	Require(failed && malformed_runtime->Observations().size() == 1 && malformed_runtime->ConsumeBearerExpectation(1),
	        "GraphQL decode failure replayed an unaccepted but ambiguous response");
}

} // namespace

int main() {
	try {
		TestSequentialBodiesBackpressureAndNullableRows();
		TestCredentialProviderSnapshotPersistsAcrossGraphqlPages();
		TestLateOpenCancellationRemainsCancellation();
		TestAnonymousExecutionAndAuthorizationAlternatives();
		TestRemoteErrorIsRedactedAndTerminal();
		TestCloseBeforePullIsSideEffectFree();
		TestEmptyPageAdvancesWithoutPublishingFalseExhaustion();
		TestLateStatusTransportDecodeAndCursorFailuresAreTerminal();
		TestCancellationCloseAndDestructionAfterPartialOutput();
		TestTransportCancellationAndTwoStreamIsolation();
		TestIntegratedThirtyTwoPageBoundary();
		TestCursorTransferExactDecodedMemoryBoundary();
		TestMidScanFailureExposure();
		TestSameGraphqlPagePostExposureFailureIsNeverReplayable();
		TestRetryEnabledGraphqlUsesOneCredentialSnapshotAndAtomicPage();
		std::cout << "GraphQL paginated scan tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
