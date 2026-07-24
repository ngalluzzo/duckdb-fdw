#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/execution/admission_controller.hpp"
#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_retry_controller.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

// Closed Remote Runtime service for a sequential paginated REST profile. Plan
// admission occurs before this construction boundary. Open owns one moved
// authorization capability and returns the ordinary BatchStream team API; no
// pagination type crosses into Query Experience.
std::unique_ptr<BatchStream>
OpenPaginatedRestScan(std::unique_ptr<const AdmittedPaginatedRestRequestProfile> admitted_profile,
                      ScanAuthorization authorization, std::shared_ptr<const HttpTransport> transport,
                      uint64_t max_wall_milliseconds, RateLimitRuntimeContext rate_limit_runtime,
                      AdmissionRuntimeContext admission_runtime, AdmissionController::Permit scan_permit,
                      ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
