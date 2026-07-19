#include "support/runtime_http_test_support.hpp"

#include "support/require.hpp"

#include <chrono>

namespace duckdb_api_test {

ManualControl::ManualControl() : cancelled(false) {
}

bool ManualControl::IsCancellationRequested() const noexcept {
	return cancelled.load(std::memory_order_acquire);
}

void ManualControl::Cancel() noexcept {
	cancelled.store(true, std::memory_order_release);
}

duckdb_api::ScanRequest BuildRuntimeRequest(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanRequest(connector, "duckdb_login_search_page",
	                                                duckdb_api::LogicalSecretReference());
}

duckdb_api::ScanPlan BuildRuntimePlan(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanPlan(connector, BuildRuntimeRequest(connector));
}

duckdb_api::ScanPlan BuildAuthenticatedRuntimePlan(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanPlan(
	    connector, duckdb_api::BuildConservativeScanRequest(
	                   connector, "authenticated_user", duckdb_api::LogicalSecretReference::Named("fixture_secret")));
}

duckdb_api::ScanPlan BuildAuthenticatedRepositoriesRuntimePlan(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanPlan(
	    connector,
	    duckdb_api::BuildConservativeScanRequest(connector, "authenticated_repositories",
	                                             duckdb_api::LogicalSecretReference::Named("fixture_secret")));
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
		Require(error.Stage() == stage, "curl execution error stage drifted from " +
		                                    std::to_string(static_cast<int>(stage)) + " to " +
		                                    std::to_string(static_cast<int>(error.Stage())));
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        "curl diagnostic was empty or unbounded");
		if (!forbidden.empty()) {
			Require(error.SafeMessage().find(forbidden) == std::string::npos,
			        "curl diagnostic exposed controlled response data or authority");
		}
		if (!forbidden_secondary.empty()) {
			Require(error.SafeMessage().find(forbidden_secondary) == std::string::npos,
			        "curl diagnostic exposed secondary controlled response data or authority");
		}
	}
	Require(rejected, "expected a structured curl execution error");
}

} // namespace duckdb_api_test
