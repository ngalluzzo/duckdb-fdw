#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace duckdb_api_test {

class ManualControl final : public duckdb_api::ExecutionControl {
public:
	ManualControl();
	bool IsCancellationRequested() const noexcept override;
	void Cancel() noexcept;

private:
	std::atomic<bool> cancelled;
};

duckdb_api::ScanRequest BuildRuntimeRequest();
duckdb_api::ScanPlan BuildRuntimePlan(const duckdb_api::CompiledConnector &connector);

void RequireExecutionError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
                           const std::string &forbidden = "");

} // namespace duckdb_api_test
