#pragma once

#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <memory>

namespace duckdb_api {
namespace query_internal {

// Query-owned logical-optimization state. The credential-free baseline request
// is retained so every callback invocation replans from the same authority.
// The selected immutable plan is independently owned by each DuckDB bind-data
// copy, including the execution-specific copies DuckDB makes when substituting
// prepared-statement parameters.
class TableFunctionPlanState {
public:
	TableFunctionPlanState(ScanRequest baseline_request, ScanPlan baseline_plan);
	TableFunctionPlanState(const TableFunctionPlanState &other);
	TableFunctionPlanState(TableFunctionPlanState &&other) noexcept = default;
	TableFunctionPlanState &operator=(const TableFunctionPlanState &) = delete;
	TableFunctionPlanState &operator=(TableFunctionPlanState &&) = delete;

	const ScanRequest &BaselineRequest() const noexcept;
	const ScanPlan &SelectedPlan() const noexcept;

	// Strong replacement: construction completes before the selected pointer is
	// swapped, so a planning exception cannot leave partial state.
	void ReplaceSelectedPlan(ScanPlan selected_plan);

private:
	const ScanRequest baseline_request;
	std::unique_ptr<const ScanPlan> selected_plan;
};

} // namespace query_internal
} // namespace duckdb_api
