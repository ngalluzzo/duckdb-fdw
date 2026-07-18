#include "support/controlled_http_transport.hpp"

#include "duckdb_api/internal/http_scan_executor.hpp"
#include "duckdb_api/internal/http_transport.hpp"

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace duckdb_api_test {

struct ControlledHttpRuntime::State {
	State() : mode(ControlledHttpMode::RESPONSE), status(200) {
		observation.request_count = 0;
		observation.port = 0;
		observation.max_header_bytes = 0;
		observation.max_response_bytes = 0;
		observation.max_decompressed_bytes = 0;
	}

	mutable std::mutex mutex;
	ControlledHttpMode mode;
	uint32_t status;
	std::string body;
	std::string diagnostic;
	ControlledRequestObservation observation;
};

namespace {

class ControlledTransport final : public duckdb_api::internal::HttpTransport {
public:
	explicit ControlledTransport(std::shared_ptr<ControlledHttpRuntime::State> state_p)
	    : state(std::move(state_p)) {
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &request,
	                                      const duckdb_api::internal::HttpLimits &limits,
	                                      duckdb_api::ExecutionControl &control) const override {
		ControlledHttpMode mode;
		uint32_t status;
		std::string body;
		std::string diagnostic;
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
			mode = state->mode;
			status = state->status;
			body = state->body;
			diagnostic = state->diagnostic;
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
}

void ControlledHttpRuntime::FailWithUnknownTransportDiagnostic(std::string diagnostic_p) {
	std::lock_guard<std::mutex> guard(state->mutex);
	state->mode = ControlledHttpMode::TRANSPORT_FAILURE;
	state->body.clear();
	state->diagnostic = std::move(diagnostic_p);
}

void ControlledHttpRuntime::BlockUntilCancelled() {
	std::lock_guard<std::mutex> guard(state->mutex);
	state->mode = ControlledHttpMode::BLOCK_UNTIL_CANCEL;
	state->body.clear();
	state->diagnostic.clear();
}

ControlledRequestObservation ControlledHttpRuntime::Observation() const {
	std::lock_guard<std::mutex> guard(state->mutex);
	return state->observation;
}

std::shared_ptr<ControlledHttpRuntime> BuildControlledHttpRuntime() {
	auto state = std::make_shared<ControlledHttpRuntime::State>();
	std::unique_ptr<duckdb_api::internal::HttpTransport> transport(new ControlledTransport(state));
	auto executor = duckdb_api::internal::BuildHttpScanExecutor(std::move(transport));
	return std::shared_ptr<ControlledHttpRuntime>(new ControlledHttpRuntime(std::move(state), std::move(executor)));
}

} // namespace duckdb_api_test
