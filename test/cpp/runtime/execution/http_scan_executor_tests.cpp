#include "duckdb_api/authorization.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "runtime/support/http_scan_executor_test_support.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::BuildAnonymousHttpPlan;
using duckdb_api_test::BuildAuthenticatedHttpPlan;
using duckdb_api_test::GeneratedHttpBearerToken;
using duckdb_api_test::ManualHttpExecutionControl;
using duckdb_api_test::OneAuthenticatedHttpRow;
using duckdb_api_test::Require;
using duckdb_api_test::RequireHttpExecutionError;
using duckdb_api_test::ThreeHttpRows;

void TestOneRequestAndSchemaAlignedBatches() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, ThreeHttpRows());
	ManualHttpExecutionControl control;
	auto stream = runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	Require(runtime->Observation().request_count == 0, "Open performed network work");

	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch), "first batch was missing");
	Require(batch.IsSchemaAligned() && batch.rows.size() == 2, "first batch was not aligned or bounded");
	Require(batch.column_kinds ==
	            std::vector<duckdb_api::ValueKind>(
	                {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR, duckdb_api::ValueKind::BOOLEAN}),
	        "batch schema drifted");
	Require(batch.rows[0].values[0].bigint_value == 11 && batch.rows[0].values[1].varchar_value == "duckdb" &&
	            !batch.rows[0].values[2].boolean_value,
	        "first typed row drifted");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 33,
	        "second bounded batch drifted");
	Require(!stream->Next(control, batch) && batch.rows.empty() && batch.column_kinds.empty(),
	        "stream did not exhaust cleanly");
	Require(!stream->Next(control, batch), "cleanly exhausted stream did not remain exhausted");

	const auto observation = runtime->Observation();
	Require(observation.request_count == 1, "batch pulls did not perform exactly one request");
	Require(observation.method == "GET" && observation.scheme == "https" && observation.host == "api.github.com" &&
	            observation.port == 443 && observation.target == "/search/users?q=duckdb+in%3Alogin&per_page=3",
	        "structural request identity drifted");
	Require(observation.headers.size() == 3 &&
	            observation.headers[0] ==
	                std::make_pair(std::string("Accept"), std::string("application/vnd.github+json")) &&
	            observation.headers[1] == std::make_pair(std::string("User-Agent"), std::string("duckdb-api/0.6.0")) &&
	            observation.headers[2] ==
	                std::make_pair(std::string("X-GitHub-Api-Version"), std::string("2022-11-28")),
	        "fixed request headers drifted");
	Require(observation.max_header_bytes == duckdb_api::HOST_MAX_HEADER_BYTES &&
	            observation.max_response_bytes == duckdb_api::HOST_MAX_RESPONSE_BYTES &&
	            observation.max_decompressed_bytes == duckdb_api::HOST_MAX_DECOMPRESSED_BYTES,
	        "transport did not receive the applied hard budgets");
}

void TestAnonymousSinglePageIgnoresContinuationMetadata() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({duckdb_api_test::ControlledResponse(
	    200, ThreeHttpRows(),
	    {"<https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3&page=2>; rel=\"next\""})});
	ManualHttpExecutionControl control;
	auto stream = runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 2,
	        "Link-bearing anonymous page did not preserve its first batch");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && !stream->Next(control, batch),
	        "Link-bearing anonymous page changed fixed one-response exhaustion");
	Require(runtime->Observations().size() == 1,
	        "unpaginated anonymous relation followed captured continuation metadata");
	Require(runtime->Observation().max_metadata_bytes == 0,
	        "unpaginated relation retained ignored Link metadata against decoded memory");
}

void TestExactBearerRequestAndRootObject() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, OneAuthenticatedHttpRow());
	ManualHttpExecutionControl control;
	auto token = GeneratedHttpBearerToken(1);
	const auto expected_value = "Bearer " + token;
	runtime->ExpectBearer(expected_value);
	const auto plan = BuildAuthenticatedHttpPlan();
	Require(plan.Snapshot().find(token) == std::string::npos, "credential bytes entered the immutable plan snapshot");
	auto stream = runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
	Require(runtime->Observation().request_count == 0, "authorized Open performed network work");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.IsSchemaAligned(),
	        "authenticated root object did not produce one aligned row");
	Require(!stream->Next(control, batch), "authenticated root object was emitted more than once");
	const auto observation = runtime->Observation();
	Require(observation.method == "GET" && observation.scheme == "https" && observation.host == "api.github.com" &&
	            observation.port == 443 && observation.target == "/user",
	        "authenticated structural request identity drifted");
	Require(observation.headers.size() == 4 && observation.headers[3].first == "Authorization" &&
	            observation.headers[3].second == "<redacted>" && runtime->ConsumeBearerExpectation(1),
	        "fixed authenticator did not append exactly one canonical bearer header");
}

void TestBearerTokenBoundaryPrecedesTransport() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, OneAuthenticatedHttpRow());
	ManualHttpExecutionControl control;
	const auto limit = duckdb_api::ScanAuthorization::GithubUserBearerTokenByteLimit();
	auto exact = std::string(static_cast<std::size_t>(limit), 'e');
	auto stream = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(exact)), control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch), "exact-limit bearer token did not reach the controlled Runtime service");
	Require(runtime->Observation().request_count == 1, "exact-limit bearer token did not perform one request");

	auto over = std::string(static_cast<std::size_t>(limit + 1), 'o');
	bool rejected = false;
	try {
		(void)runtime->Executor()->OpenWithAuthorization(
		    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(over)), control);
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "header_bytes",
		        "one-byte-over bearer token used the wrong safe resource diagnostic");
	}
	Require(rejected, "one-byte-over bearer token was accepted");
	Require(runtime->Observation().request_count == 1, "one-byte-over bearer token reached the controlled transport");
}

void TestAuthenticatedStatusFailures() {
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;
	for (uint32_t status = 401; status <= 403; status += 2) {
		const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
		auto token = GeneratedHttpBearerToken(status);
		const auto credential_canary = token;
		const auto response_canary = token + "_response_body";
		runtime->Respond(status, response_canary);
		auto stream = runtime->Executor()->OpenWithAuthorization(
		    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		RequireHttpExecutionError([&]() { stream->Next(control, batch); },
		                          status == 401 ? duckdb_api::ErrorStage::AUTHENTICATION
		                                        : duckdb_api::ErrorStage::AUTHORIZATION,
		                          credential_canary);
		Require(runtime->Observation().request_count == 1, "authenticated status failure replayed transport");
	}
}

void TestSimultaneousAuthorizationSnapshots() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	ManualHttpExecutionControl first_control;
	ManualHttpExecutionControl second_control;
	auto first_token = GeneratedHttpBearerToken(31);
	auto second_token = GeneratedHttpBearerToken(32);
	const auto first_header = "Bearer " + first_token;
	const auto second_header = "Bearer " + second_token;
	runtime->RespondWithBearerBarrier(first_header, OneAuthenticatedHttpRow("first-isolated"), second_header,
	                                  OneAuthenticatedHttpRow("second-isolated"));
	auto first = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(first_token)),
	    first_control);
	auto second = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(second_token)),
	    second_control);
	duckdb_api::TypedBatch first_batch;
	duckdb_api::TypedBatch second_batch;
	std::atomic<bool> first_succeeded(false);
	std::atomic<bool> second_succeeded(false);
	std::thread first_worker([&]() {
		try {
			first_succeeded.store(first->Next(first_control, first_batch), std::memory_order_release);
		} catch (...) {
		}
	});
	std::thread second_worker([&]() {
		try {
			second_succeeded.store(second->Next(second_control, second_batch), std::memory_order_release);
		} catch (...) {
		}
	});
	const auto overlapped = runtime->WaitForRequestCount(2, std::chrono::seconds(2));
	runtime->ReleaseBearerBarrier();
	first_worker.join();
	second_worker.join();
	Require(overlapped, "isolated scans did not overlap at the transport barrier");
	Require(first_succeeded.load(std::memory_order_acquire) && second_succeeded.load(std::memory_order_acquire),
	        "one isolated scan failed");
	Require(first_batch.rows[0].values[1].varchar_value == "first-isolated" &&
	            second_batch.rows[0].values[1].varchar_value == "second-isolated",
	        "authorization identity crossed its originating stream");
	const auto observations = runtime->Observations();
	Require(observations.size() == 2, "isolated scans did not perform two independent requests");
	for (std::size_t index = 0; index < observations.size(); index++) {
		Require(observations[index].headers.size() == 4, "isolated scan header count drifted");
		Require(observations[index].headers[3].first == "Authorization" &&
		            observations[index].headers[3].second == "<redacted>",
		        "ordinary observations exposed bearer credential bytes");
	}

	ManualHttpExecutionControl cancelled_control;
	ManualHttpExecutionControl surviving_control;
	auto cancelled_token = GeneratedHttpBearerToken(33);
	auto surviving_token = GeneratedHttpBearerToken(34);
	const auto cancelled_header = "Bearer " + cancelled_token;
	const auto surviving_header = "Bearer " + surviving_token;
	runtime->RespondWithBearerBarrier(cancelled_header, OneAuthenticatedHttpRow("cancelled-isolated"), surviving_header,
	                                  OneAuthenticatedHttpRow("surviving-isolated"));
	auto cancelled_stream = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(cancelled_token)),
	    cancelled_control);
	auto surviving_stream = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(surviving_token)),
	    surviving_control);
	duckdb_api::TypedBatch cancelled_batch;
	duckdb_api::TypedBatch surviving_batch;
	std::atomic<bool> cancellation_observed(false);
	std::atomic<bool> survivor_succeeded(false);
	std::thread cancelled_worker([&]() {
		try {
			(void)cancelled_stream->Next(cancelled_control, cancelled_batch);
		} catch (const duckdb_api::ExecutionCancelled &) {
			cancellation_observed.store(true, std::memory_order_release);
		} catch (...) {
		}
	});
	std::thread surviving_worker([&]() {
		try {
			survivor_succeeded.store(surviving_stream->Next(surviving_control, surviving_batch),
			                         std::memory_order_release);
		} catch (...) {
		}
	});
	const auto teardown_overlapped = runtime->WaitForRequestCount(4, std::chrono::seconds(2));
	cancelled_stream->Cancel();
	cancelled_stream->Close();
	if (!teardown_overlapped) {
		surviving_stream->Cancel();
	}
	runtime->ReleaseBearerBarrier();
	cancelled_worker.join();
	surviving_worker.join();
	Require(teardown_overlapped, "close/cancel scans did not overlap at the transport barrier");
	Require(cancellation_observed.load(std::memory_order_acquire),
	        "closing one overlapping authorized stream did not cancel it");
	Require(survivor_succeeded.load(std::memory_order_acquire) &&
	            surviving_batch.rows[0].values[1].varchar_value == "surviving-isolated",
	        "closing one authorized stream disturbed the other stream's identity");
}

void TestAuthenticatedLifecycleAndRecovery() {
	ManualHttpExecutionControl control;
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, OneAuthenticatedHttpRow());
	auto unopened_token = GeneratedHttpBearerToken(51);
	auto unopened = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(unopened_token)),
	    control);
	unopened->Close();
	unopened->Close();
	Require(runtime->Observation().request_count == 0, "closing an unpulled authorized stream performed a request");

	runtime->BlockUntilCancelled();
	auto blocked_token = GeneratedHttpBearerToken(52);
	auto blocked = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(blocked_token)),
	    control);
	duckdb_api::TypedBatch blocked_batch;
	std::atomic<bool> cancellation_observed(false);
	std::thread worker([&]() {
		try {
			(void)blocked->Next(control, blocked_batch);
		} catch (const duckdb_api::ExecutionCancelled &) {
			cancellation_observed.store(true, std::memory_order_release);
		}
	});
	for (std::size_t index = 0; index < 500 && runtime->Observation().request_count == 0; index++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	blocked->Close();
	blocked->Cancel();
	worker.join();
	Require(cancellation_observed.load(std::memory_order_acquire),
	        "concurrent authorized close did not cancel the active transfer");

	runtime->FailWithUnknownTransportDiagnostic("dependency diagnostic must remain redacted");
	auto failed_token = GeneratedHttpBearerToken(53);
	const auto failed_canary = failed_token;
	auto failed = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(failed_token)),
	    control);
	duckdb_api::TypedBatch failed_batch;
	RequireHttpExecutionError([&]() { failed->Next(control, failed_batch); }, duckdb_api::ErrorStage::TRANSPORT,
	                          failed_canary);
	failed->Close();

	runtime->Respond(200, OneAuthenticatedHttpRow("recovered"));
	auto recovered_token = GeneratedHttpBearerToken(54);
	auto recovered = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(recovered_token)),
	    control);
	duckdb_api::TypedBatch recovered_batch;
	Require(recovered->Next(control, recovered_batch) && recovered_batch.rows[0].values[1].varchar_value == "recovered",
	        "shared executor did not recover after authorized cancellation and transport failure");
}

void TestFailureStagesRedactionAndNoReplay() {
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;
	const auto status_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	status_runtime->Respond(503, "SECRET_STATUS_BODY https://secret.invalid/path");
	auto status_stream = status_runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	RequireHttpExecutionError([&]() { status_stream->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS,
	                          "SECRET_STATUS_BODY");
	RequireHttpExecutionError([&]() { status_stream->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS,
	                          "SECRET_STATUS_BODY");
	Require(status_runtime->Observation().request_count == 1, "status failure performed more than one request");

	const auto unknown_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	unknown_runtime->FailWithUnknownTransportDiagnostic("SECRET_TRANSPORT api.github.com internal curl detail");
	auto unknown_stream = unknown_runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	RequireHttpExecutionError([&]() { unknown_stream->Next(control, batch); }, duckdb_api::ErrorStage::TRANSPORT,
	                          "SECRET_TRANSPORT");
	RequireHttpExecutionError([&]() { unknown_stream->Next(control, batch); }, duckdb_api::ErrorStage::TRANSPORT,
	                          "SECRET_TRANSPORT");

	const auto oversized_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	oversized_runtime->Respond(200, std::string(duckdb_api::HOST_MAX_RESPONSE_BYTES + 1, 'x'));
	auto oversized_stream = oversized_runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	RequireHttpExecutionError([&]() { oversized_stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
}

void TestCancellationAndIdempotentClose() {
	const auto unopened_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	ManualHttpExecutionControl cancelled;
	cancelled.Cancel();
	bool open_cancelled = false;
	try {
		unopened_runtime->Executor()->Open(BuildAnonymousHttpPlan(), cancelled);
	} catch (const duckdb_api::ExecutionCancelled &) {
		open_cancelled = true;
	}
	Require(open_cancelled && unopened_runtime->Observation().request_count == 0,
	        "pre-open cancellation acquired request authority");

	ManualHttpExecutionControl control;
	const auto closed_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	closed_runtime->Respond(200, ThreeHttpRows());
	auto closed_stream = closed_runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	closed_stream->Close();
	closed_stream->Close();
	duckdb_api::TypedBatch batch;
	Require(!closed_stream->Next(control, batch) && closed_runtime->Observation().request_count == 0,
	        "closed stream performed a request");

	const auto blocked_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	blocked_runtime->BlockUntilCancelled();
	auto blocked_stream = blocked_runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	std::atomic<bool> observed_cancel(false);
	std::atomic<bool> returned(false);
	std::thread worker([&]() {
		try {
			blocked_stream->Next(control, batch);
		} catch (const duckdb_api::ExecutionCancelled &) {
			observed_cancel.store(true, std::memory_order_release);
		}
		returned.store(true, std::memory_order_release);
	});
	for (std::size_t index = 0; index < 500 && blocked_runtime->Observation().request_count == 0; index++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	blocked_stream->Cancel();
	blocked_stream->Cancel();
	worker.join();
	Require(returned.load(std::memory_order_acquire) && observed_cancel.load(std::memory_order_acquire),
	        "in-flight transport did not observe stream cancellation");
	Require(blocked_runtime->Observation().request_count == 1, "cancelled request was replayed");
}

void TestDeadlinePersistsAcrossBatchPulls() {
	const uint64_t controlled_wall_milliseconds = 30;
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime(controlled_wall_milliseconds);
	runtime->Respond(200, ThreeHttpRows());
	ManualHttpExecutionControl control;
	auto stream = runtime->Executor()->Open(BuildAnonymousHttpPlan(), control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 2,
	        "deadline regression did not produce the first batch");
	std::this_thread::sleep_for(std::chrono::milliseconds(controlled_wall_milliseconds + 20));
	RequireHttpExecutionError([&]() { stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
	RequireHttpExecutionError([&]() { stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
	Require(runtime->Observation().request_count == 1, "delayed second pull replayed the request");
}

void TestCleanExhaustionOutlivesDeadline() {
	const uint64_t controlled_wall_milliseconds = 30;
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime(controlled_wall_milliseconds);
	runtime->Respond(200, OneAuthenticatedHttpRow());
	ManualHttpExecutionControl control;
	auto stream = runtime->Executor()->OpenWithAuthorization(
	    BuildAuthenticatedHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(GeneratedHttpBearerToken(90)),
	    control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && !stream->Next(control, batch),
	        "short-deadline fixture did not reach clean exhaustion");
	std::this_thread::sleep_for(std::chrono::milliseconds(controlled_wall_milliseconds + 20));
	Require(!stream->Next(control, batch) && runtime->Observation().request_count == 1,
	        "clean exhaustion changed into a deadline failure or replayed transport");
}

} // namespace

int main() {
	try {
		TestOneRequestAndSchemaAlignedBatches();
		TestAnonymousSinglePageIgnoresContinuationMetadata();
		TestExactBearerRequestAndRootObject();
		TestBearerTokenBoundaryPrecedesTransport();
		TestAuthenticatedStatusFailures();
		TestSimultaneousAuthorizationSnapshots();
		TestAuthenticatedLifecycleAndRecovery();
		TestFailureStagesRedactionAndNoReplay();
		TestCancellationAndIdempotentClose();
		TestDeadlinePersistsAcrossBatchPulls();
		TestCleanExhaustionOutlivesDeadline();
		std::cout << "HTTP scan executor tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "HTTP scan executor tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
