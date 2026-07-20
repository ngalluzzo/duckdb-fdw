#pragma once

#include "scan_planner_internal.hpp"

namespace duckdb_api {
namespace scan_planner_internal {

void ValidateGraphqlOperationProfile(const CompiledRelation &relation, const CompiledOperation &operation,
                                     const CompiledNetworkPolicy &network_policy);
PlannedGraphqlOperation PlanGraphqlOperation(const CompiledOperation &operation);

} // namespace scan_planner_internal
} // namespace duckdb_api
