#pragma once

#include "duckdb_api/execution.hpp"

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

duckdb_api::ScanPlan BuildRuntimePlan();
duckdb_api::ScanPlan BuildAuthenticatedRuntimePlan();
duckdb_api::ScanPlan BuildAuthenticatedRepositoriesRuntimePlan();
duckdb_api::ScanPlan BuildVisibilityPrivateRuntimePlan();
std::string RuntimeCurlBearerToken(uint64_t suffix);

void RequireExecutionError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
                           const std::string &forbidden = "", const std::string &forbidden_secondary = "");

} // namespace duckdb_api_test
