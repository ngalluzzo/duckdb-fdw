#pragma once

#include "duckdb_api/execution.hpp"

#include <cstdint>

namespace duckdb_api {
namespace internal {

struct HttpExecutionProfile;

// Closed result of validating the complete immutable Semantics handoff against
// the installed Runtime profile. Admission performs no network I/O, retains no
// plan or cancellation view, and grants only the stream kind; authorization is
// still matched and moved by the executor before construction.
enum class AdmittedHttpOperation : uint8_t { ANONYMOUS_SEARCH, AUTHENTICATED_USER, AUTHENTICATED_REPOSITORIES };

bool TryAdmitHttpPlan(const ScanPlan &plan, const HttpExecutionProfile &profile, AdmittedHttpOperation &operation);

} // namespace internal
} // namespace duckdb_api
