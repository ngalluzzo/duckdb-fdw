#include "duckdb_api/authorization.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "runtime/support/http_scan_executor_test_support.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
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
OpenWithToken(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
              ManualHttpExecutionControl &control, std::string token) {
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("graphql_secret");
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
	Require(stream->Next(control, batch) && batch.rows.size() == 64 && batch.column_kinds.size() == 8 &&
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
	auto first = OpenWithToken(runtime, first_control, first_token);
	auto second = OpenWithToken(runtime, second_control, second_token);
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

} // namespace

int main() {
	try {
		TestSequentialBodiesBackpressureAndNullableRows();
		TestAnonymousExecutionAndAuthorizationAlternatives();
		TestRemoteErrorIsRedactedAndTerminal();
		TestCloseBeforePullIsSideEffectFree();
		TestEmptyPageAdvancesWithoutPublishingFalseExhaustion();
		TestLateStatusTransportDecodeAndCursorFailuresAreTerminal();
		TestCancellationCloseAndDestructionAfterPartialOutput();
		TestTransportCancellationAndTwoStreamIsolation();
		TestIntegratedThirtyTwoPageBoundary();
		std::cout << "GraphQL paginated scan tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
