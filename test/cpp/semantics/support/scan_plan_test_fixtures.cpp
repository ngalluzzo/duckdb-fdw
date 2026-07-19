#include "semantics/support/scan_plan_test_fixtures.hpp"

#include "support/live_scan_request.hpp"

#include <stdexcept>

namespace duckdb_api_test {
namespace {

const duckdb_api::CompiledRelation &FindNativeRelation(const duckdb_api::CompiledConnector &connector,
                                                       const std::string &exact_relation_name) {
	const auto *relation = connector.FindRelation(exact_relation_name);
	if (relation == nullptr) {
		throw std::logic_error("native connector omitted the exact fixture relation");
	}
	return *relation;
}

} // namespace

duckdb_api::ScanPlan BuildValidAnonymousPlanFixture() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindNativeRelation(connector, "duckdb_login_search_page");
	return duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, relation.Name()));
}

duckdb_api::ScanPlan BuildValidAuthenticatedPlanFixture(const std::string &exact_logical_secret_name) {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindNativeRelation(connector, "authenticated_user");
	return duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation.Name(), exact_logical_secret_name));
}

} // namespace duckdb_api_test
