#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::Require;

void RequireConnectorRejected(duckdb_api::CompiledConnector connector, const std::string &field) {
	bool rejected = false;
	try {
		duckdb_api::BuildConservativeScanPlan(connector, duckdb_api::BuildConservativeScanRequest());
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "planner accepted connector drift in " + field);
}

void RequireRequestRejected(duckdb_api::ScanRequest request, const std::string &field) {
	bool rejected = false;
	try {
		duckdb_api::BuildConservativeScanPlan(duckdb_api::BuildCompiledConnector("fixture-digest"), request);
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "planner accepted non-conservative request " + field);
}

void TestConservativeRequest() {
	const auto request = duckdb_api::BuildConservativeScanRequest();
	Require(request.connector_name == "example" && request.relation_name == "items", "ScanRequest identity drifted");
	Require(request.explicit_inputs.empty(), "ScanRequest gained explicit inputs");
	Require(request.projected_columns == std::vector<std::string>({"id", "name", "active"}),
	        "ScanRequest projection drifted");
	Require(request.predicate == "TRUE" && request.orderings.empty() && !request.has_limit && !request.has_offset,
	        "ScanRequest no longer preserves conservative relational ownership");
	Require(request.capabilities.IsConservativePreview(), "ScanRequest capability profile drifted");
	Require(request.Snapshot() ==
	            "connector=example;relation=items;inputs=[];projection=id,name,active;predicate=TRUE;ordering=[];"
	            "limit=unset;offset=unset;capabilities=projection:unavailable,filter:unavailable,ordering:unavailable,"
	            "limit:unavailable,offset:unavailable,progress:unavailable,cancellation:verified,secrets:unavailable",
	        "ScanRequest snapshot drifted");
}

void TestConservativePlan() {
	const auto plan = duckdb_api::BuildConservativeScanPlan(duckdb_api::BuildCompiledConnector("fixture-digest"),
	                                                        duckdb_api::BuildConservativeScanRequest());
	Require(plan.operation_name == "items_list" && plan.executor_name == "fixture_rest" && plan.method == "GET" &&
	            plan.path == "/items" && plan.extractor == "$.items[*]" && plan.fixture_digest == "fixture-digest",
	        "ScanPlan operation drifted");
	Require(plan.output_columns == std::vector<std::string>({"id", "name", "active"}),
	        "ScanPlan projection closure drifted");
	Require(plan.remote_predicate == "TRUE" && plan.runtime_residual_predicate == "TRUE" &&
	            plan.remote_ordering.empty() && plan.runtime_ordering.empty() && !plan.has_remote_limit &&
	            !plan.has_remote_offset && !plan.has_runtime_limit && !plan.has_runtime_offset,
	        "ScanPlan claimed unsupported relational work");
	Require(plan.duckdb_owned_operations == std::vector<std::string>({"filter", "ordering", "limit", "offset"}),
	        "ScanPlan DuckDB ownership drifted");
	Require(!plan.pagination_enabled && !plan.providers_enabled && !plan.retry_enabled && !plan.cache_enabled &&
	            !plan.network_enabled && plan.budgets.IsPreviewBudget(),
	        "ScanPlan capability or resource envelope drifted");
	Require(plan.Snapshot() ==
	            "operation=items_list;executor=fixture_rest;method=GET;path=/items;extractor=$.items[*];"
	            "fixture=fixture-digest;projection=id,name,active;remote_predicate=TRUE;runtime_residual=TRUE;"
	            "duckdb_owns=filter,ordering,limit,offset;remote_ordering=[];runtime_ordering=[];remote_limit=unset;"
	            "remote_offset=unset;runtime_limit=unset;runtime_offset=unset;pagination=disabled;providers=disabled;"
	            "retry=disabled;cache=disabled;network=disabled;budgets=fixture_bytes:4096,records:32,name_bytes:128,"
	            "json_nesting:16,batch_rows:2,wall_ms:5000,concurrency:1",
	        "ScanPlan snapshot drifted");
}

void TestConnectorAndRequestRejection() {
	auto connector = duckdb_api::BuildCompiledConnector("fixture-digest");
	connector.columns.pop_back();
	RequireConnectorRejected(connector, "schema width");
	connector = duckdb_api::BuildCompiledConnector("fixture-digest");
	connector.columns[0].nullable = true;
	RequireConnectorRejected(connector, "nullability");
	connector = duckdb_api::BuildCompiledConnector("fixture-digest");
	connector.columns[1].extractor = "$.other";
	RequireConnectorRejected(connector, "column extractor");
	connector = duckdb_api::BuildCompiledConnector("fixture-digest");
	connector.path = "/other";
	RequireConnectorRejected(connector, "operation path");

	auto request = duckdb_api::BuildConservativeScanRequest();
	request.explicit_inputs.push_back("unexpected");
	RequireRequestRejected(request, "with explicit inputs");
	request = duckdb_api::BuildConservativeScanRequest();
	request.projected_columns.pop_back();
	RequireRequestRejected(request, "with incomplete projection closure");
	request = duckdb_api::BuildConservativeScanRequest();
	request.predicate = "id > 1";
	RequireRequestRejected(request, "with an unavailable predicate");
	request = duckdb_api::BuildConservativeScanRequest();
	request.orderings.push_back("id");
	RequireRequestRejected(request, "with unavailable ordering");
	request = duckdb_api::BuildConservativeScanRequest();
	request.has_limit = true;
	RequireRequestRejected(request, "with unavailable limit");
	request = duckdb_api::BuildConservativeScanRequest();
	request.has_offset = true;
	RequireRequestRejected(request, "with unavailable offset");
	request = duckdb_api::BuildConservativeScanRequest();
	request.capabilities.projection = true;
	RequireRequestRejected(request, "with projection capability");
	request = duckdb_api::BuildConservativeScanRequest();
	request.capabilities.cancellation = false;
	RequireRequestRejected(request, "without verified cancellation");
}

} // namespace

int main() {
	try {
		TestConservativeRequest();
		TestConservativePlan();
		TestConnectorAndRequestRejection();
		std::cout << "scan planner tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan planner tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
