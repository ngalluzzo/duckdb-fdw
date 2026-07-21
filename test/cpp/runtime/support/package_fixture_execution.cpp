#include "package_fixture_execution.hpp"

#include "runtime/support/controlled_http_transport.hpp"
#include "runtime/support/package_fixture_observation_internal.hpp"
#include "runtime/support/package_fixture_scenario_internal.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace duckdb_api_test {
namespace {

static const char FIXTURE_BEARER_TOKEN[] = "runtime_fixture_bearer_capability";

std::unique_ptr<duckdb_api::BatchStream> OpenFixtureStream(const duckdb_api::ScanPlan &plan,
                                                           const RuntimeFixtureTranscript &transcript,
                                                           ControlledHttpRuntime &runtime,
                                                           duckdb_api::ExecutionControl &control) {
	switch (transcript.authorization) {
	case RuntimeFixtureAuthorizationState::ANONYMOUS:
		return runtime.Executor()->OpenWithAuthorization(plan, duckdb_api::ScanAuthorization::Anonymous(), control);
	case RuntimeFixtureAuthorizationState::BEARER_PRESENT:
		runtime.ExpectBearer(std::string("Bearer ") + FIXTURE_BEARER_TOKEN);
		return runtime.Executor()->OpenWithAuthorization(
		    plan, duckdb_api::ScanAuthorization::Bearer(std::string(FIXTURE_BEARER_TOKEN)), control);
	case RuntimeFixtureAuthorizationState::BEARER_MISSING: {
		auto stream = runtime.Executor()->Open(plan, control);
		if (stream) {
			stream->Close();
		}
		throw std::logic_error("bearer-missing fixture did not fail before stream admission");
	}
	default:
		throw std::invalid_argument("unknown controlled fixture authorization state");
	}
}

void CancelAndClose(std::unique_ptr<duckdb_api::BatchStream> &stream,
                    RuntimeFixtureExecutionObservation &result) noexcept {
	if (!stream) {
		return;
	}
	stream->Cancel();
	result.stream_cancel_invoked = true;
	stream->Close();
	result.stream_close_invoked = true;
}

void Close(std::unique_ptr<duckdb_api::BatchStream> &stream, RuntimeFixtureExecutionObservation &result) noexcept {
	if (!stream) {
		return;
	}
	stream->Close();
	result.stream_close_invoked = true;
}

RuntimeFixtureExecutionObservation RunFixtureScenario(const duckdb_api::ScanPlan &plan,
                                                      const RuntimeFixtureTranscript &transcript,
                                                      RuntimeFixtureScenario scenario,
                                                      duckdb_api::ExecutionControl &outer_control,
                                                      bool capture_cancellation) {
	internal::ValidateRuntimeFixtureScenario(scenario);
	auto result = internal::NewRuntimeFixtureObservation(plan, scenario.Cancellation());
	internal::RuntimeFixtureScenarioControl control(outer_control, scenario.Cancellation());
	auto runtime = BuildControlledPackageHttpRuntime();
	std::unique_ptr<duckdb_api::BatchStream> stream;
	std::thread transport_canceller;
	std::exception_ptr cancellation;

	try {
		if (control.IsCancellationRequested()) {
			throw duckdb_api::ExecutionCancelled();
		}
		internal::ValidateRuntimeFixtureTranscript(transcript);
		if (transcript.authorization != RuntimeFixtureAuthorizationState::BEARER_MISSING) {
			const bool transport_failure = scenario.Failure() == RuntimeFixtureFailureExpectation::TRANSPORT;
			runtime->RespondSequence(internal::BuildRuntimeFixtureResponses(transcript, control, transport_failure));
		}
		stream = OpenFixtureStream(plan, transcript, *runtime, control);

		if (scenario.Cancellation() == RuntimeFixtureCancellationPoint::BEFORE_REQUEST) {
			control.Reach(RuntimeFixtureCancellationPoint::BEFORE_REQUEST);
		}
		if (scenario.Cancellation() == RuntimeFixtureCancellationPoint::TRANSPORT) {
			runtime->BlockUntilCancelled();
			transport_canceller = std::thread([&]() {
				if (runtime->WaitForRequestCount(1, std::chrono::seconds(2))) {
					control.Reach(RuntimeFixtureCancellationPoint::TRANSPORT);
				}
			});
		}

		while (true) {
			duckdb_api::TypedBatch batch;
			bool produced = false;
			{
				RuntimeFixtureCheckpointScope checkpoint_scope(control);
				produced = stream->Next(control, batch);
			}
			if (!produced) {
				break;
			}
			for (auto &row : batch.rows) {
				result.rows.push_back(std::move(row));
			}

			if (scenario.Cancellation() == RuntimeFixtureCancellationPoint::PAGE_BOUNDARY) {
				if (plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED ||
				    batch.rows.empty() || batch.rows.size() >= plan.Pagination().PageBudgets().batch_rows) {
					throw std::logic_error(
					    "page-boundary cancellation requires a paginated first page smaller than one output batch");
				}
				control.Reach(RuntimeFixtureCancellationPoint::PAGE_BOUNDARY);
			}

			if (scenario.Cancellation() == RuntimeFixtureCancellationPoint::STREAM_CLOSE) {
				control.Reach(RuntimeFixtureCancellationPoint::STREAM_CLOSE);
				CancelAndClose(stream, result);
				duckdb_api::TypedBatch after_close;
				result.post_close_exhaustion_observed = !stream->Next(control, after_close) && after_close.rows.empty();
				result.rows.clear();
				result.cancellation_observed = true;
				result.checkpoint_reached = control.CheckpointReached();
				break;
			}
		}

		if (!result.cancellation_observed) {
			Close(stream, result);
			result.succeeded = true;
		}
	} catch (const duckdb_api::ExecutionCancelled &) {
		CancelAndClose(stream, result);
		result.rows.clear();
		result.cancellation_observed = true;
		result.checkpoint_reached = control.CheckpointReached();
		cancellation = std::current_exception();
	} catch (const duckdb_api::ExecutionError &error) {
		Close(stream, result);
		result.rows.clear();
		result.has_runtime_error = true;
		result.runtime_error_stage = error.Stage();
		result.runtime_error_field = error.Field();
	} catch (...) {
		CancelAndClose(stream, result);
		if (transport_canceller.joinable()) {
			transport_canceller.join();
		}
		throw;
	}

	if (transport_canceller.joinable()) {
		transport_canceller.join();
	}
	if (scenario.Cancellation() != RuntimeFixtureCancellationPoint::NONE && !control.CheckpointReached()) {
		throw std::logic_error("fixture scenario did not reach its selected Runtime cancellation checkpoint");
	}
	internal::CaptureRuntimeFixtureRequests(transcript, *runtime, result);
	internal::ValidateRuntimeFixtureFailure(scenario.Failure(), result);
	if (cancellation && !capture_cancellation) {
		std::rethrow_exception(cancellation);
	}
	return result;
}

} // namespace

RuntimeFixtureExecutionObservation
RuntimePackageFixtureExecutionService::Execute(const duckdb_api::ScanPlan &plan,
                                               const RuntimeFixtureTranscript &transcript,
                                               duckdb_api::ExecutionControl &control) const {
	return RunFixtureScenario(plan, transcript, RuntimeFixtureScenario::Standard(), control, false);
}

RuntimeFixtureExecutionObservation RuntimePackageFixtureExecutionService::ExecuteScenario(
    const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &transcript, RuntimeFixtureScenario scenario,
    duckdb_api::ExecutionControl &control) const {
	return RunFixtureScenario(plan, transcript, scenario, control, true);
}

} // namespace duckdb_api_test
