#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/execution/admission_controller.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_retry_controller.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <cstdint>
#include <memory>

namespace duckdb_api {
namespace internal {

// Opens one isolated pull-driven GraphQL cursor stream. Admission and
// authorization-alternative matching occur before this boundary; body bytes,
// bearer placement, transport, decoder, and mutable cursor state remain lazy.
std::unique_ptr<BatchStream>
OpenGraphqlPaginatedScan(std::unique_ptr<const AdmittedGraphqlRequestProfile> admitted_profile,
                         ScanAuthorization authorization, std::shared_ptr<const HttpTransport> transport,
                         uint64_t max_wall_milliseconds, RateLimitRuntimeContext rate_limit_runtime,
                         AdmissionRuntimeContext admission_runtime, AdmissionController::Permit scan_permit,
                         ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
