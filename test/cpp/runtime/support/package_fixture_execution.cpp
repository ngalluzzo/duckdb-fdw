#include "package_fixture_execution.hpp"

#include "runtime/support/controlled_http_transport.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace duckdb_api_test {
namespace {

static const char FIXTURE_BEARER_TOKEN[] = "runtime_fixture_bearer_capability";

void CheckCancellation(duckdb_api::ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
}

bool IsLinkHeader(const std::string &name) {
	static const char expected[] = "link";
	if (name.size() != sizeof(expected) - 1) {
		return false;
	}
	for (std::size_t index = 0; index < sizeof(expected) - 1; index++) {
		const char byte = name[index];
		const char folded = byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') : byte;
		if (folded != expected[index]) {
			return false;
		}
	}
	return true;
}

uint64_t AddHeaderBytes(uint64_t total, std::size_t bytes) {
	const auto value = static_cast<uint64_t>(bytes);
	if (value > std::numeric_limits<uint64_t>::max() - total) {
		throw std::invalid_argument("controlled fixture response header bytes overflow");
	}
	return total + value;
}

std::vector<ControlledHttpResponse> BuildResponses(const RuntimeFixtureTranscript &transcript,
                                                   duckdb_api::ExecutionControl &control) {
	std::vector<ControlledHttpResponse> responses;
	responses.reserve(transcript.pages.size());
	for (const auto &page : transcript.pages) {
		CheckCancellation(control);
		uint64_t header_bytes = 0;
		std::vector<std::string> link_field_values;
		for (const auto &header : page.headers) {
			CheckCancellation(control);
			header_bytes = AddHeaderBytes(header_bytes, header.name.size());
			header_bytes = AddHeaderBytes(header_bytes, 2);
			header_bytes = AddHeaderBytes(header_bytes, header.value.size());
			header_bytes = AddHeaderBytes(header_bytes, 2);
			if (IsLinkHeader(header.name)) {
				link_field_values.push_back(header.value);
			}
		}
		responses.push_back({page.status, page.body, std::move(link_field_values), header_bytes, false, ""});
		CheckCancellation(control);
	}
	return responses;
}

RuntimeFixtureExecutionObservation NewObservation(const duckdb_api::ScanPlan &plan) {
	return {false, {}, plan.Snapshot(), false, duckdb_api::ErrorStage::INTERNAL, "", {}, false, 0};
}

void CaptureRequests(const ControlledHttpRuntime &runtime, RuntimeFixtureExecutionObservation &result) {
	const auto observed = runtime.Observations();
	result.requests.reserve(observed.size());
	for (const auto &request : observed) {
		result.requests.push_back({request.method, request.scheme, request.host, request.port, request.target,
		                           request.headers, request.body, request.content_type});
	}
	result.request_count = static_cast<uint64_t>(result.requests.size());
	result.transport_observed = !result.requests.empty();
}

void RequireAuthorizationRedaction(const RuntimeFixtureTranscript &transcript, ControlledHttpRuntime &runtime,
                                   const RuntimeFixtureExecutionObservation &result) {
	for (const auto &request : result.requests) {
		std::size_t authorization_headers = 0;
		for (const auto &header : request.headers) {
			if (header.first == "Authorization") {
				authorization_headers++;
				if (header.second != "<redacted>") {
					throw std::logic_error("controlled fixture observation exposed authorization bytes");
				}
			}
		}
		if (transcript.authorization == RuntimeFixtureAuthorizationState::BEARER_PRESENT) {
			if (authorization_headers != 1) {
				throw std::logic_error("controlled bearer fixture did not observe exactly one authorization header");
			}
		} else if (authorization_headers != 0) {
			throw std::logic_error("controlled non-bearer fixture observed authorization authority");
		}
	}
	if (transcript.authorization == RuntimeFixtureAuthorizationState::BEARER_PRESENT &&
	    !runtime.ConsumeBearerExpectation(result.request_count)) {
		throw std::logic_error("controlled fixture bearer bytes differed from Runtime's synthetic capability");
	}
}

} // namespace

RuntimeFixtureExecutionObservation
RuntimePackageFixtureExecutionService::Execute(const duckdb_api::ScanPlan &plan,
                                               const RuntimeFixtureTranscript &transcript,
                                               duckdb_api::ExecutionControl &control) const {
	CheckCancellation(control);
	if (transcript.authorization == RuntimeFixtureAuthorizationState::BEARER_MISSING) {
		if (!transcript.pages.empty()) {
			throw std::invalid_argument("bearer-missing fixture transcript cannot contain response pages");
		}
	} else if (transcript.pages.empty()) {
		throw std::invalid_argument("executable fixture transcript requires at least one response page");
	}

	auto result = NewObservation(plan);
	auto runtime = BuildControlledPackageHttpRuntime();
	if (transcript.authorization != RuntimeFixtureAuthorizationState::BEARER_MISSING) {
		runtime->RespondSequence(BuildResponses(transcript, control));
	}
	std::unique_ptr<duckdb_api::BatchStream> stream;
	try {
		switch (transcript.authorization) {
		case RuntimeFixtureAuthorizationState::ANONYMOUS:
			stream =
			    runtime->Executor()->OpenWithAuthorization(plan, duckdb_api::ScanAuthorization::Anonymous(), control);
			break;
		case RuntimeFixtureAuthorizationState::BEARER_PRESENT: {
			runtime->ExpectBearer(std::string("Bearer ") + FIXTURE_BEARER_TOKEN);
			stream = runtime->Executor()->OpenWithAuthorization(
			    plan, duckdb_api::ScanAuthorization::Bearer(std::string(FIXTURE_BEARER_TOKEN)), control);
			break;
		}
		case RuntimeFixtureAuthorizationState::BEARER_MISSING:
			stream = runtime->Executor()->Open(plan, control);
			if (stream) {
				stream->Close();
			}
			throw std::logic_error("bearer-missing fixture did not fail before stream admission");
		default:
			throw std::invalid_argument("unknown controlled fixture authorization state");
		}

		duckdb_api::TypedBatch batch;
		while (stream->Next(control, batch)) {
			for (auto &row : batch.rows) {
				result.rows.push_back(std::move(row));
			}
		}
		stream->Close();
		result.succeeded = true;
	} catch (const duckdb_api::ExecutionCancelled &) {
		if (stream) {
			stream->Cancel();
			stream->Close();
		}
		throw;
	} catch (const duckdb_api::ExecutionError &error) {
		if (stream) {
			stream->Close();
		}
		result.rows.clear();
		result.has_runtime_error = true;
		result.runtime_error_stage = error.Stage();
		result.runtime_error_field = error.Field();
	}

	CaptureRequests(*runtime, result);
	RequireAuthorizationRedaction(transcript, *runtime, result);
	return result;
}

} // namespace duckdb_api_test
