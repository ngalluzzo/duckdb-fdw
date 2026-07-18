#include "support/scan_plan_test_fixtures.hpp"

namespace duckdb_api_test {

// Link-time probe for Runtime's intended consumption boundary. This translation
// unit deliberately includes only the safe fixture header.
std::string ConsumeSafeScanPlanFixtureHeader(const std::string &exact_logical_secret_name) {
	const auto valid = BuildValidAuthenticatedPlanFixture(exact_logical_secret_name);
	const auto invalid =
	    BuildNetworkPlanCounterexample(exact_logical_secret_name, NetworkPlanCounterexample::REDIRECTS_ENABLED);
	return valid.RelationName() + ":" + invalid.RelationName();
}

} // namespace duckdb_api_test
