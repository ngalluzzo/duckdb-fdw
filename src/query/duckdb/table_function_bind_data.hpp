#pragma once

#include "table_function_plan_state.hpp"

#include "duckdb/function/function.hpp"
#include "duckdb_api/execution.hpp"

#include <memory>
#include <utility>

namespace duckdb {
namespace duckdb_api_query_internal {

// Query's private DuckDB bind object. Every Copy owns a deep plan-state copy;
// only the immutable executor service is shared. The type lives beside the
// adapter so focused lifecycle tests can prove the actual FunctionData copy
// boundary rather than a look-alike state object.
struct DuckdbApiBindData final : public TableFunctionData {
	DuckdbApiBindData(duckdb_api::ScanRequest baseline_request, duckdb_api::ScanPlan baseline_plan,
	                  std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : plan_state(std::move(baseline_request), std::move(baseline_plan)), executor(std::move(executor_p)) {
	}

	DuckdbApiBindData(const duckdb_api::query_internal::TableFunctionPlanState &plan_state_p,
	                  std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : plan_state(plan_state_p), executor(std::move(executor_p)) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return unique_ptr<FunctionData>(new DuckdbApiBindData(plan_state, executor));
	}

	duckdb_api::query_internal::TableFunctionPlanState plan_state;
	const std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

} // namespace duckdb_api_query_internal
} // namespace duckdb
