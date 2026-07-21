#pragma once

#include "package_fixture_execution.hpp"
#include "runtime/support/controlled_http_transport.hpp"

#include <vector>

namespace duckdb_api_test {
namespace internal {

struct RuntimeFixtureResponseAccountingOverrides {
	std::vector<uint64_t> wire_response_bytes;
};

void ValidateRuntimeFixtureTranscript(const RuntimeFixtureTranscript &transcript);
std::vector<ControlledHttpResponse>
BuildRuntimeFixtureResponses(const RuntimeFixtureTranscript &transcript, duckdb_api::ExecutionControl &control,
                             bool transport_failure, const RuntimeFixtureResponseAccountingOverrides *overrides);
RuntimeFixtureExecutionObservation NewRuntimeFixtureObservation(const duckdb_api::ScanPlan &plan,
                                                                RuntimeFixtureCancellationPoint point);
void CaptureRuntimeFixtureRequests(const RuntimeFixtureTranscript &transcript, ControlledHttpRuntime &runtime,
                                   RuntimeFixtureExecutionObservation &observation);

RuntimeFixtureExecutionObservation
RunRuntimeFixtureScenario(const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &transcript,
                          RuntimeFixtureScenario scenario, duckdb_api::ExecutionControl &outer_control,
                          bool capture_cancellation,
                          const RuntimeFixtureResponseAccountingOverrides *overrides = nullptr);

} // namespace internal
} // namespace duckdb_api_test
