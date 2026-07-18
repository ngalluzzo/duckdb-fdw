#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::Require;

void TestCanonicalConservativeRequest() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto request = duckdb_api::BuildConservativeScanRequest(connector);
	const std::vector<std::string> expected_projection = {"id", "login", "site_admin"};

	Require(request.connector_name == connector.connector_name && request.relation_name == connector.relation_name,
	        "request identity did not come from the immutable connector");
	Require(request.explicit_inputs.empty(), "native request unexpectedly exposed relation inputs");
	Require(request.projected_columns == expected_projection, "request did not preserve the full declared projection");
	Require(request.predicate == "TRUE" && request.orderings.empty() && !request.has_limit && !request.has_offset,
	        "request did not use conservative unavailable relational metadata");
	Require(request.capabilities.IsConservativePreview(), "request did not report the accepted adapter capabilities");
	Require(request.Snapshot() ==
	            "connector=github;relation=duckdb_login_search_page;inputs=[];projection=id,login,site_admin;"
	            "predicate=TRUE;ordering=[];limit=unset;offset=unset;capabilities=projection:unavailable,filter:"
	            "unavailable,ordering:unavailable,limit:unavailable,offset:unavailable,progress:unavailable,"
	            "cancellation:verified,secrets:unavailable",
	        "conservative request snapshot changed");
}

void TestRequestIsDerivedRatherThanHardCoded() {
	auto controlled = duckdb_api::BuildNativeGithubConnector();
	controlled.connector_name = "controlled";
	controlled.relation_name = "controlled_page";
	controlled.columns[0].name = "controlled_id";

	const auto first = duckdb_api::BuildConservativeScanRequest(controlled);
	const auto second = duckdb_api::BuildConservativeScanRequest(controlled);
	Require(first.Snapshot() == second.Snapshot(), "identical immutable metadata produced different requests");
	Require(first.connector_name == "controlled" && first.relation_name == "controlled_page" &&
	            first.projected_columns == std::vector<std::string>({"controlled_id", "login", "site_admin"}),
	        "request builder duplicated canonical product identifiers or columns");
}

void TestCapabilityClassification() {
	duckdb_api::AdapterCapabilities capabilities = {false, false, false, false, false, false, true, false};
	Require(capabilities.IsConservativePreview(), "accepted native capabilities were not conservative");
	capabilities.projection = true;
	Require(!capabilities.IsConservativePreview(), "available projection was silently classified as unavailable");
	capabilities.projection = false;
	capabilities.cancellation = false;
	Require(!capabilities.IsConservativePreview(), "unverified cancellation was accepted by the native profile");
}

} // namespace

int main() {
	try {
		TestCanonicalConservativeRequest();
		TestRequestIsDerivedRatherThanHardCoded();
		TestCapabilityClassification();
		std::cout << "scan request tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan request tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
