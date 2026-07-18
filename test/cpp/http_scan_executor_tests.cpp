#include "duckdb_api/connector.hpp"
#include "duckdb_api/internal/http_scan_executor.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"
#include "support/controlled_http_transport.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using duckdb_api_test::Require;

class ManualControl final : public duckdb_api::ExecutionControl {
public:
	ManualControl() : cancelled(false) {
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

duckdb_api::ScanRequest Request() {
	duckdb_api::ScanRequest request;
	request.connector_name = "github";
	request.relation_name = "duckdb_login_search_page";
	request.projected_columns = {"id", "login", "site_admin"};
	request.predicate = "TRUE";
	request.has_limit = false;
	request.has_offset = false;
	request.capabilities = {false, false, false, false, false, false, true, false};
	return request;
}

duckdb_api::ScanPlan Plan() {
	return duckdb_api::BuildConservativeScanPlan(duckdb_api::BuildNativeGithubConnector(), Request());
}

std::string ThreeRows() {
	return "{\"items\":[{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false},"
	       "{\"id\":22,\"login\":\"duckdb-fdw\",\"site_admin\":true},"
	       "{\"id\":33,\"login\":\"three\",\"site_admin\":false}]}";
}

void RequireError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
                  const std::string &forbidden = "") {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == stage, "executor error stage drifted");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        "executor diagnostic was empty or unbounded");
		if (!forbidden.empty()) {
			Require(error.SafeMessage().find(forbidden) == std::string::npos,
			        "executor exposed transport or response data");
		}
	}
	Require(rejected, "expected a structured executor error");
}

void TestOneRequestAndSchemaAlignedBatches() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->Respond(200, ThreeRows());
	ManualControl control;
	auto stream = runtime->Executor()->Open(Plan(), control);
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

	const auto observation = runtime->Observation();
	Require(observation.request_count == 1, "batch pulls did not perform exactly one request");
	Require(observation.method == "GET" && observation.scheme == "https" && observation.host == "api.github.com" &&
	            observation.port == 443 && observation.target == "/search/users?q=duckdb+in%3Alogin&per_page=3",
	        "structural request identity drifted");
	Require(observation.headers.size() == 3 &&
	            observation.headers[0] ==
	                std::make_pair(std::string("Accept"), std::string("application/vnd.github+json")) &&
	            observation.headers[1] == std::make_pair(std::string("User-Agent"), std::string("duckdb-api/0.3.0")) &&
	            observation.headers[2] ==
	                std::make_pair(std::string("X-GitHub-Api-Version"), std::string("2022-11-28")),
	        "fixed request headers drifted");
	Require(observation.max_header_bytes == duckdb_api::HOST_MAX_HEADER_BYTES &&
	            observation.max_response_bytes == duckdb_api::HOST_MAX_RESPONSE_BYTES &&
	            observation.max_decompressed_bytes == duckdb_api::HOST_MAX_DECOMPRESSED_BYTES,
	        "transport did not receive the applied hard budgets");
}

void TestFailureStagesRedactionAndNoReplay() {
	ManualControl control;
	duckdb_api::TypedBatch batch;

	const auto status_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	status_runtime->Respond(503, "SECRET_STATUS_BODY https://secret.invalid/path");
	auto status_stream = status_runtime->Executor()->Open(Plan(), control);
	RequireError([&]() { status_stream->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS,
	             "SECRET_STATUS_BODY");
	Require(!status_stream->Next(control, batch), "HTTP status failure was replayed");
	Require(status_runtime->Observation().request_count == 1, "status failure performed more than one request");

	const auto unknown_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	unknown_runtime->FailWithUnknownTransportDiagnostic("SECRET_TRANSPORT api.github.com internal curl detail");
	auto unknown_stream = unknown_runtime->Executor()->Open(Plan(), control);
	RequireError([&]() { unknown_stream->Next(control, batch); }, duckdb_api::ErrorStage::TRANSPORT,
	             "SECRET_TRANSPORT");
	Require(!unknown_stream->Next(control, batch), "unknown transport failure was replayed");

	const auto oversized_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	oversized_runtime->Respond(200, std::string(duckdb_api::HOST_MAX_RESPONSE_BYTES + 1, 'x'));
	auto oversized_stream = oversized_runtime->Executor()->Open(Plan(), control);
	RequireError([&]() { oversized_stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
}

void TestCancellationAndIdempotentClose() {
	const auto unopened_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	ManualControl cancelled;
	cancelled.Cancel();
	bool open_cancelled = false;
	try {
		unopened_runtime->Executor()->Open(Plan(), cancelled);
	} catch (const duckdb_api::ExecutionCancelled &) {
		open_cancelled = true;
	}
	Require(open_cancelled && unopened_runtime->Observation().request_count == 0,
	        "pre-open cancellation acquired request authority");

	ManualControl control;
	const auto closed_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	closed_runtime->Respond(200, ThreeRows());
	auto closed_stream = closed_runtime->Executor()->Open(Plan(), control);
	closed_stream->Close();
	closed_stream->Close();
	duckdb_api::TypedBatch batch;
	Require(!closed_stream->Next(control, batch) && closed_runtime->Observation().request_count == 0,
	        "closed stream performed a request");

	const auto blocked_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	blocked_runtime->BlockUntilCancelled();
	auto blocked_stream = blocked_runtime->Executor()->Open(Plan(), control);
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
	runtime->Respond(200, ThreeRows());
	ManualControl control;
	auto stream = runtime->Executor()->Open(Plan(), control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 2,
	        "deadline regression did not produce the first batch");
	std::this_thread::sleep_for(std::chrono::milliseconds(controlled_wall_milliseconds + 20));
	RequireError([&]() { stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
	Require(!stream->Next(control, batch), "expired stream resumed after delayed second pull");
	Require(runtime->Observation().request_count == 1, "delayed second pull replayed the request");
}

void TestExecutionProfileNeverWidensRecordAuthority() {
	const uint64_t narrower_record_authority = 2;
	const auto runtime =
	    duckdb_api_test::BuildControlledHttpRuntime(duckdb_api::MAX_EXECUTION_MILLISECONDS, narrower_record_authority);
	runtime->Respond(200, ThreeRows());
	ManualControl control;
	Require(Plan().Budgets().decoded_records == 3,
	        "record-authority counterexample did not retain the valid product plan");
	RequireError([&]() { (void)runtime->Executor()->Open(Plan(), control); }, duckdb_api::ErrorStage::POLICY);
	Require(runtime->Observation().request_count == 0, "plan wider than executor authority reached transport");

	RequireError(
	    [&]() { (void)duckdb_api_test::BuildControlledHttpRuntime(duckdb_api::MAX_EXECUTION_MILLISECONDS, 0); },
	    duckdb_api::ErrorStage::INTERNAL);
	RequireError(
	    [&]() { (void)duckdb_api_test::BuildControlledHttpRuntime(duckdb_api::MAX_EXECUTION_MILLISECONDS, 4); },
	    duckdb_api::ErrorStage::INTERNAL);
}

void TestNullTransportRejected() {
	RequireError(
	    [&]() { duckdb_api::internal::BuildHttpScanExecutor(std::unique_ptr<duckdb_api::internal::HttpTransport>()); },
	    duckdb_api::ErrorStage::INTERNAL);
}

} // namespace

int main() {
	try {
		TestOneRequestAndSchemaAlignedBatches();
		TestFailureStagesRedactionAndNoReplay();
		TestCancellationAndIdempotentClose();
		TestDeadlinePersistsAcrossBatchPulls();
		TestExecutionProfileNeverWidensRecordAuthority();
		TestNullTransportRejected();
		std::cout << "HTTP scan executor tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "HTTP scan executor tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
