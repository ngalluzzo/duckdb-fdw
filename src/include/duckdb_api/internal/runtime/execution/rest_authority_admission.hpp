#pragma once

#include "duckdb_api/scan_plan.hpp"

namespace duckdb_api {
namespace internal {

struct HttpExecutionProfile;

// Intersects typed plan network and authentication authority with one Runtime
// generation's private execution profile. It copies no credential bytes and
// performs no authorization or I/O.
bool HasSupportedRestAuthority(const ScanPlan &plan, const HttpExecutionProfile &profile, bool &requires_bearer);

} // namespace internal
} // namespace duckdb_api
