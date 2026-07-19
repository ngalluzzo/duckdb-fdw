#pragma once

#include "duckdb_api/execution.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace duckdb_api_test {

class ManualHttpExecutionControl final : public duckdb_api::ExecutionControl {
public:
	ManualHttpExecutionControl();
	bool IsCancellationRequested() const noexcept override;
	void Cancel() noexcept;

private:
	std::atomic<bool> cancelled;
};

duckdb_api::ScanPlan BuildAnonymousHttpPlan();
duckdb_api::ScanPlan BuildAuthenticatedHttpPlan();
std::string ThreeHttpRows();
std::string OneAuthenticatedHttpRow(const std::string &login = "duckdb");
std::string GeneratedHttpBearerToken(uint64_t suffix);

void RequireHttpExecutionError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
                               const std::string &forbidden = "");

} // namespace duckdb_api_test
