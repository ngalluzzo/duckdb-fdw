#include "support/runtime_http_test_support.hpp"

#include "support/require.hpp"

namespace duckdb_api_test {

ManualControl::ManualControl() : cancelled(false) {
}

bool ManualControl::IsCancellationRequested() const noexcept {
	return cancelled.load(std::memory_order_acquire);
}

void ManualControl::Cancel() noexcept {
	cancelled.store(true, std::memory_order_release);
}

duckdb_api::ScanRequest BuildRuntimeRequest() {
	duckdb_api::ScanRequest request;
	request.connector_name = "github";
	request.relation_name = "duckdb_login_search_page";
	request.projected_columns = {"id", "login", "site_admin"};
	request.predicate = "TRUE";
	request.has_limit = false;
	request.has_offset = false;
	request.capabilities = {false, false, false, false, false, false, true, false};
	return request;
}

duckdb_api::ScanPlan BuildRuntimePlan(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanPlan(connector, BuildRuntimeRequest());
}

void RequireExecutionError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
                           const std::string &forbidden) {
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
	}
	Require(rejected, "expected a structured curl execution error");
}

} // namespace duckdb_api_test
