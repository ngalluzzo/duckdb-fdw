#pragma once

#include "package_fixture_execution.hpp"
#include "runtime/support/controlled_http_transport.hpp"

#include <vector>

namespace duckdb_api_test {
namespace internal {

void ValidateRuntimeFixtureTranscript(const RuntimeFixtureTranscript &transcript);
std::vector<ControlledHttpResponse> BuildRuntimeFixtureResponses(const RuntimeFixtureTranscript &transcript,
                                                                 duckdb_api::ExecutionControl &control,
                                                                 bool transport_failure);
RuntimeFixtureExecutionObservation NewRuntimeFixtureObservation(const duckdb_api::ScanPlan &plan,
                                                                RuntimeFixtureCancellationPoint point);
void CaptureRuntimeFixtureRequests(const RuntimeFixtureTranscript &transcript, ControlledHttpRuntime &runtime,
                                   RuntimeFixtureExecutionObservation &observation);

} // namespace internal
} // namespace duckdb_api_test
