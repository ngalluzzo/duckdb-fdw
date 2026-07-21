#pragma once

#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"

#include <vector>

namespace duckdb_api {
namespace internal {

struct HttpExecutionProfile;

// Validates Link traversal, page bindings, signed BIGINT progression, and
// aggregate budgets after common REST request materialization has succeeded.
bool HasSupportedRestPagination(const ScanPlan &plan, const HttpExecutionProfile &profile,
                                const std::vector<AdmittedQueryParameter> &query);

} // namespace internal
} // namespace duckdb_api
