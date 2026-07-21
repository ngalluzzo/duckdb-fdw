#pragma once

#include "duckdb_api/package_fixture_runner.hpp"

namespace duckdb_api {
namespace connector {
namespace internal {

PackageFixtureLimits EffectivePackageFixtureLimits(const PackageFixtureLimits &host_limits);

} // namespace internal
} // namespace connector
} // namespace duckdb_api
