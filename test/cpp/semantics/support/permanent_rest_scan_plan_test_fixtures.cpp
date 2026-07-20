#include "semantics/support/permanent_rest_scan_plan_test_fixtures.hpp"

#include "connector/support/package_generation_test_fixtures.hpp"
#include "duckdb_api/scan_planner.hpp"

namespace duckdb_api_test {

duckdb_api::ScanPlan BuildValidPermanentRestScanPlanFixture() {
	const auto generation = BuildRestMaterializationPackageGenerationFixture();
	auto request = duckdb_api::BuildConservativeScanRequest(
	    generation.Connector(), PACKAGE_REST_MATERIALIZATION_RELATION, duckdb_api::LogicalSecretReference());
	request.explicit_inputs =
	    duckdb_api::ExplicitInputs({duckdb_api::ExplicitInput::Varchar("scope", "north america/\xCE\xB2")});
	return duckdb_api::BuildConservativeScanPlan(generation.Connector(), request);
}

} // namespace duckdb_api_test
