#include "support/scan_plan_test_fixtures.hpp"

#include "support/live_scan_request.hpp"

#include <stdexcept>

namespace duckdb_api_test {
namespace {

const duckdb_api::CompiledRelation &FindNativeRelation(const duckdb_api::CompiledConnector &connector,
                                                       duckdb_api::CompiledCredentialRequirement requirement) {
	for (const auto &relation : connector.Relations()) {
		if (relation.Authentication().Requirement() == requirement) {
			return relation;
		}
	}
	throw std::logic_error("native connector omitted the required fixture relation");
}

} // namespace

duckdb_api::ScanPlan BuildValidAnonymousPlanFixture() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindNativeRelation(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	return duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, relation.Name()));
}

duckdb_api::ScanPlan BuildValidAuthenticatedPlanFixture(const std::string &exact_logical_secret_name) {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindNativeRelation(connector, duckdb_api::CompiledCredentialRequirement::REQUIRED);
	return duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation.Name(), exact_logical_secret_name));
}

} // namespace duckdb_api_test
