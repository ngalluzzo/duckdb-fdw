#include "duckdb_api/package_bound_scan_planner.hpp"

#include "duckdb_api/scan_planner.hpp"

#include <utility>

namespace duckdb_api {

PackageBoundScanPlanningService::PackageBoundScanPlanningService(CompiledPackageGeneration generation_p)
    : generation(std::move(generation_p)), bound_generation(generation.OpaqueHandle()) {
}

ScanPlan PackageBoundScanPlanningService::Plan(const CompiledGenerationHandle &generation_handle,
                                               const ScanRequest &request) const {
	if (!bound_generation.IsSameGeneration(generation_handle)) {
		throw PlanningError(PlanningErrorCode::INVALID_CONTRACT,
		                    "planning generation handle does not match the bound package generation");
	}
	return BuildConservativeScanPlan(generation.Connector(), request);
}

} // namespace duckdb_api
