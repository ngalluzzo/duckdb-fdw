#include "runtime/support/runtime_http_test_support.hpp"

#include "semantics/support/scan_plan_test_fixtures.hpp"

#include <chrono>
#include <stdexcept>

namespace duckdb_api_test {

ManualControl::ManualControl() : cancelled(false) {
}

bool ManualControl::IsCancellationRequested() const noexcept {
	return cancelled.load(std::memory_order_acquire);
}

void ManualControl::Cancel() noexcept {
	cancelled.store(true, std::memory_order_release);
}

duckdb_api::ScanPlan BuildRuntimePlan() {
	return BuildValidAnonymousPlanFixture();
}

duckdb_api::ScanPlan BuildAuthenticatedRuntimePlan() {
	return BuildValidAuthenticatedPlanFixture("fixture_secret");
}

duckdb_api::ScanPlan BuildAuthenticatedRepositoriesRuntimePlan() {
	return BuildValidAuthenticatedRepositoriesPlanFixture("fixture_secret");
}

duckdb_api::ScanPlan BuildVisibilityPrivateRuntimePlan() {
	return BuildVisibilityPrivatePlanFixture("fixture_secret");
}

duckdb_api::ScanPlan BuildAmbiguousPredicateFallbackRuntimePlan() {
	return BuildAmbiguousPredicateFallbackPlanFixture("fixture_secret");
}

std::string RuntimeCurlBearerToken(uint64_t suffix) {
	return "curl_runtime_generated_" +
	       std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())) + "_" +
	       std::to_string(suffix);
}

void RequireExecutionError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
                           const std::string &forbidden, const std::string &forbidden_secondary) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		if (error.Stage() != stage) {
			throw std::runtime_error("curl execution error stage drifted from " +
			                         std::to_string(static_cast<int>(stage)) + " to " +
			                         std::to_string(static_cast<int>(error.Stage())));
		}
		if (error.SafeMessage().empty() || error.SafeMessage().size() > 128) {
			throw std::runtime_error("curl diagnostic was empty or unbounded");
		}
		if (!forbidden.empty()) {
			if (error.SafeMessage().find(forbidden) != std::string::npos) {
				throw std::runtime_error("curl diagnostic exposed controlled response data or authority");
			}
		}
		if (!forbidden_secondary.empty()) {
			if (error.SafeMessage().find(forbidden_secondary) != std::string::npos) {
				throw std::runtime_error("curl diagnostic exposed secondary controlled response data or authority");
			}
		}
	}
	if (!rejected) {
		throw std::runtime_error("expected a structured curl execution error");
	}
}

} // namespace duckdb_api_test
