#include "support/controlled_http_transport.hpp"

#include "duckdb_api/internal/http_scan_executor.hpp"
#include "duckdb_api/internal/http_transport.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace duckdb_api_test {

struct ControlledHttpRuntime::State {
	State() : mode(ControlledHttpMode::RESPONSE), status(200), bearer_barrier_released(true) {
		observation.request_count = 0;
		observation.port = 0;
		observation.max_header_bytes = 0;
		observation.max_response_bytes = 0;
		observation.max_decompressed_bytes = 0;
	}

	mutable std::mutex mutex;
	std::condition_variable condition;
	ControlledHttpMode mode;
	uint32_t status;
	std::string body;
	std::string diagnostic;
	ControlledRequestObservation observation;
	std::vector<ControlledRequestObservation> observations;
	std::vector<std::pair<std::string, std::string>> bearer_responses;
	bool bearer_barrier_released;
};

namespace {

class ControlledTransport final : public duckdb_api::internal::HttpTransport {
public:
	explicit ControlledTransport(std::shared_ptr<ControlledHttpRuntime::State> state_p) : state(std::move(state_p)) {
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &request,
	                                       const duckdb_api::internal::HttpLimits &limits,
	                                       duckdb_api::ExecutionControl &control) const override {
		ControlledHttpMode mode;
		uint32_t status;
		std::string body;
		std::string diagnostic;
		bool wait_for_bearer_barrier = false;
		{
			std::lock_guard<std::mutex> guard(state->mutex);
			state->observation.request_count++;
			state->observation.method = request.method;
			state->observation.scheme = request.scheme;
			state->observation.host = request.host;
			state->observation.port = request.port;
			state->observation.target = request.target;
			state->observation.headers.clear();
			for (std::size_t index = 0; index < request.headers.size(); index++) {
				state->observation.headers.push_back(
				    std::make_pair(request.headers[index].name, request.headers[index].value));
			}
			state->observation.max_header_bytes = limits.max_header_bytes;
			state->observation.max_response_bytes = limits.max_response_bytes;
			state->observation.max_decompressed_bytes = limits.max_decompressed_bytes;
			state->observations.push_back(state->observation);
			mode = state->mode;
			status = state->status;
			if (mode == ControlledHttpMode::BEARER_RESPONSE_BARRIER) {
				wait_for_bearer_barrier = true;
				for (std::size_t response_index = 0; response_index < state->bearer_responses.size();
				     response_index++) {
					for (std::size_t header_index = 0; header_index < request.headers.size(); header_index++) {
						if (request.headers[header_index].name == "Authorization" &&
						    request.headers[header_index].value == state->bearer_responses[response_index].first) {
							body = state->bearer_responses[response_index].second;
						}
					}
				}
			} else {
				body = state->body;
			}
			diagnostic = state->diagnostic;
		}
		state->condition.notify_all();

		if (wait_for_bearer_barrier) {
			if (body.empty()) {
				throw std::runtime_error("controlled bearer response was not configured");
			}
			std::unique_lock<std::mutex> guard(state->mutex);
			while (!state->bearer_barrier_released && !control.IsCancellationRequested() &&
			       std::chrono::steady_clock::now() < limits.deadline) {
				state->condition.wait_for(guard, std::chrono::milliseconds(1));
			}
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
			if (!state->bearer_barrier_released) {
				throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "wall_milliseconds",
				                                 "execution exceeded its wall-time budget");
			}
		}

		if (mode == ControlledHttpMode::TRANSPORT_FAILURE) {
			throw std::runtime_error(diagnostic);
		}
		if (mode == ControlledHttpMode::BLOCK_UNTIL_CANCEL) {
			while (!control.IsCancellationRequested() && std::chrono::steady_clock::now() < limits.deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "wall_milliseconds",
			                                 "execution exceeded its wall-time budget");
		}
		if (static_cast<uint64_t>(body.size()) > limits.max_response_bytes) {
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "response_bytes",
			                                 "HTTP response exceeded its byte budget");
		}
		if (static_cast<uint64_t>(body.size()) > limits.max_decompressed_bytes) {
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "decompressed_bytes",
			                                 "HTTP response exceeded its decompressed-byte budget");
		}
		return {status, 64, static_cast<uint64_t>(body.size()), std::move(body)};
	}

private:
	const std::shared_ptr<ControlledHttpRuntime::State> state;
};

} // namespace

ControlledHttpRuntime::ControlledHttpRuntime(std::shared_ptr<State> state_p,
                                             std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
    : state(std::move(state_p)), executor(std::move(executor_p)) {
}

std::shared_ptr<const duckdb_api::ScanExecutor> ControlledHttpRuntime::Executor() const {
	return executor;
}

void ControlledHttpRuntime::Respond(uint32_t status_p, std::string body_p) {
	std::lock_guard<std::mutex> guard(state->mutex);
	state->mode = ControlledHttpMode::RESPONSE;
	state->status = status_p;
	state->body = std::move(body_p);
	state->diagnostic.clear();
	state->bearer_responses.clear();
	state->bearer_barrier_released = true;
	state->condition.notify_all();
}

void ControlledHttpRuntime::RespondWithBearerBarrier(std::string first_header, std::string first_body,
                                                     std::string second_header, std::string second_body) {
	std::lock_guard<std::mutex> guard(state->mutex);
	state->mode = ControlledHttpMode::BEARER_RESPONSE_BARRIER;
	state->status = 200;
	state->body.clear();
	state->diagnostic.clear();
	state->bearer_responses.clear();
	state->bearer_responses.push_back(std::make_pair(std::move(first_header), std::move(first_body)));
	state->bearer_responses.push_back(std::make_pair(std::move(second_header), std::move(second_body)));
	state->bearer_barrier_released = false;
}

void ControlledHttpRuntime::FailWithUnknownTransportDiagnostic(std::string diagnostic_p) {
	std::lock_guard<std::mutex> guard(state->mutex);
	state->mode = ControlledHttpMode::TRANSPORT_FAILURE;
	state->body.clear();
	state->diagnostic = std::move(diagnostic_p);
	state->bearer_responses.clear();
	state->bearer_barrier_released = true;
	state->condition.notify_all();
}

void ControlledHttpRuntime::BlockUntilCancelled() {
	std::lock_guard<std::mutex> guard(state->mutex);
	state->mode = ControlledHttpMode::BLOCK_UNTIL_CANCEL;
	state->body.clear();
	state->diagnostic.clear();
	state->bearer_responses.clear();
	state->bearer_barrier_released = true;
	state->condition.notify_all();
}

bool ControlledHttpRuntime::WaitForRequestCount(uint64_t count, std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> guard(state->mutex);
	return state->condition.wait_for(guard, timeout, [&]() { return state->observations.size() >= count; });
}

void ControlledHttpRuntime::ReleaseBearerBarrier() {
	{
		std::lock_guard<std::mutex> guard(state->mutex);
		state->bearer_barrier_released = true;
	}
	state->condition.notify_all();
}

ControlledRequestObservation ControlledHttpRuntime::Observation() const {
	std::lock_guard<std::mutex> guard(state->mutex);
	return state->observation;
}

std::vector<ControlledRequestObservation> ControlledHttpRuntime::Observations() const {
	std::lock_guard<std::mutex> guard(state->mutex);
	return state->observations;
}

std::shared_ptr<ControlledHttpRuntime> BuildControlledHttpRuntime(uint64_t max_wall_milliseconds,
                                                                  uint64_t max_decoded_records) {
	auto state = std::make_shared<ControlledHttpRuntime::State>();
	std::unique_ptr<duckdb_api::internal::HttpTransport> transport(new ControlledTransport(state));
	const duckdb_api::internal::HttpExecutionProfile profile {duckdb_api::PlannedUrlScheme::HTTPS,
	                                                          "api.github.com",
	                                                          443,
	                                                          false,
	                                                          false,
	                                                          false,
	                                                          max_wall_milliseconds,
	                                                          max_decoded_records};
	auto executor = duckdb_api::internal::BuildHttpScanExecutorForProfile(std::move(transport), profile);
	return std::shared_ptr<ControlledHttpRuntime>(new ControlledHttpRuntime(std::move(state), std::move(executor)));
}

} // namespace duckdb_api_test
