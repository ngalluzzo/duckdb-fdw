#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "support/live_scan_request.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::BuildLiveScanRequest;
using duckdb_api_test::Require;

void RequireConnectorRejected(duckdb_api::CompiledConnector connector, const std::string &field) {
	bool rejected = false;
	try {
		duckdb_api::BuildConservativeScanPlan(connector, BuildLiveScanRequest(connector));
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "planner accepted invalid connector metadata " + field);
}

void RequireRequestRejected(duckdb_api::ScanRequest request, const std::string &field) {
	bool rejected = false;
	try {
		const auto connector = duckdb_api::BuildNativeGithubConnector();
		duckdb_api::BuildConservativeScanPlan(connector, request);
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "planner accepted non-conservative request " + field);
}

void RequireBudgetFieldBounded(const duckdb_api::ResourceBudgets &baseline,
                               std::uint64_t duckdb_api::ResourceBudgets::*field, std::uint64_t semantic_cap,
                               const std::string &name) {
	auto invalid = baseline;
	invalid.*field = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "zero " + name + " budget was accepted");
	invalid = baseline;
	invalid.*field = semantic_cap + 1;
	Require(!invalid.IsWithinLiveRestBounds(), name + " budget widened its semantic cap");
}

void TestSourceConstantsAreNotPushdown() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveScanRequest(connector));
	Require(plan.Operation().query_parameters[0].name == "q" && plan.Operation().query_parameters[1].name == "per_page",
	        "fixed source constants disappeared from the executable operation");
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
	        "fixed q or per_page was misclassified as relational pushdown");
}

void TestConnectorCeilingsNarrowSemanticAndHostBudgets() {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.max_response_bytes = 2048;
	connector.resource_ceilings.max_records = 2;
	connector.resource_ceilings.max_extracted_string_bytes = 64;
	const auto narrowed = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveScanRequest(connector));
	Require(narrowed.Budgets().response_bytes == 2048 && narrowed.Budgets().decoded_records == 2 &&
	            narrowed.Budgets().extracted_string_bytes == 64,
	        "connector ceilings did not narrow semantic and host budgets");
	Require(narrowed.Budgets().IsWithinLiveRestBounds(),
	        "valid connector-narrowed budgets were rejected as outside live relation bounds");

	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.max_response_bytes = duckdb_api::HOST_MAX_RESPONSE_BYTES + 1;
	connector.resource_ceilings.max_extracted_string_bytes = duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES + 1;
	const auto host_capped = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveScanRequest(connector));
	Require(host_capped.Budgets().response_bytes == duckdb_api::HOST_MAX_RESPONSE_BYTES &&
	            host_capped.Budgets().decoded_records == duckdb_api::LIVE_RELATION_MAX_RECORDS &&
	            host_capped.Budgets().extracted_string_bytes == duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES,
	        "connector metadata widened host budgets");

	connector = duckdb_api::BuildNativeGithubConnector();
	connector.resource_ceilings.max_records = duckdb_api::LIVE_RELATION_MAX_RECORDS + 1;
	RequireConnectorRejected(connector, "with a record ceiling wider than the fixed response-page domain");

	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::response_bytes,
	                          duckdb_api::HOST_MAX_RESPONSE_BYTES, "response byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::header_bytes,
	                          duckdb_api::HOST_MAX_HEADER_BYTES, "header byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::decompressed_bytes,
	                          duckdb_api::HOST_MAX_DECOMPRESSED_BYTES, "decompressed byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::decoded_records,
	                          duckdb_api::LIVE_RELATION_MAX_RECORDS, "decoded record");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::extracted_string_bytes,
	                          duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES, "extracted string byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::json_nesting,
	                          duckdb_api::HOST_MAX_JSON_NESTING, "JSON nesting");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::decoded_memory_bytes,
	                          duckdb_api::HOST_MAX_DECODED_MEMORY_BYTES, "decoded memory byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::batch_rows,
	                          duckdb_api::OUTPUT_BATCH_ROWS, "batch row");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::wall_milliseconds,
	                          duckdb_api::MAX_EXECUTION_MILLISECONDS, "wall time");

	auto invalid = narrowed.Budgets();
	invalid.request_attempts = 2;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget enabled a second request attempt");
	invalid = narrowed.Budgets();
	invalid.request_attempts = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget disabled the required request attempt");
	invalid = narrowed.Budgets();
	invalid.concurrency = 2;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget enabled a second concurrent transfer");
	invalid = narrowed.Budgets();
	invalid.concurrency = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget removed the required concurrency slot");
}

void TestPrivateControlledCapabilityUsesTheSamePlanner() {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.scheme = duckdb_api::CompiledUrlScheme::HTTP;
	connector.operation.request.origin.host = duckdb_api::CompiledRestHost("127.0.0.1");
	connector.operation.request.origin.port = 8080;
	connector.network_policy.allowed_schemes = {"http"};
	connector.network_policy.allowed_hosts = {"127.0.0.1"};
	connector.network_policy.loopback_addresses_enabled = true;
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveScanRequest(connector));
	Require(plan.Operation().origin.scheme == duckdb_api::PlannedUrlScheme::HTTP &&
	            plan.Operation().origin.host == "127.0.0.1" && plan.Operation().origin.port == 8080 &&
	            plan.Network().loopback_addresses_enabled,
	        "private controlled capability did not traverse the production planner");
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
	        "private controlled capability changed relational meaning");
}

void TestConnectorCounterexamples() {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns.clear();
	RequireConnectorRejected(connector, "with no schema");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[0].nullable = true;
	RequireConnectorRejected(connector, "with nullable required output");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[0].logical_type = "DOUBLE";
	RequireConnectorRejected(connector, "with unsupported output type");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[0].extractor.clear();
	RequireConnectorRejected(connector, "with missing extractor");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[1].name = connector.columns[0].name;
	RequireConnectorRejected(connector, "with duplicate column");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.fallback = false;
	RequireConnectorRejected(connector, "without fallback operation");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.retry_enabled = true;
	RequireConnectorRejected(connector, "with retry enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.authentication_enabled = true;
	RequireConnectorRejected(connector, "with authentication enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.pagination_enabled = true;
	RequireConnectorRejected(connector, "with pagination enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.path = "search/users";
	RequireConnectorRejected(connector, "with invalid path");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.path = "/search/users?per_page=3";
	RequireConnectorRejected(connector, "with query structure in path");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.path = "/search/users#page";
	RequireConnectorRejected(connector, "with fragment structure in path");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.query_parameters[0].name = "q?hidden";
	RequireConnectorRejected(connector, "with URL structure in query name");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.query_parameters[0].encoded_value = "duckdb#fragment";
	RequireConnectorRejected(connector, "with URL structure in encoded query value");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.query_parameters[1].name = connector.operation.request.query_parameters[0].name;
	RequireConnectorRejected(connector, "with duplicate query name");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.headers[0].value.clear();
	RequireConnectorRejected(connector, "with empty header value");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.scheme = duckdb_api::CompiledUrlScheme::HTTP;
	RequireConnectorRejected(connector, "with origin outside declared scheme");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.host = duckdb_api::CompiledRestHost("example.com");
	RequireConnectorRejected(connector, "with origin outside declared host");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.port = 444;
	RequireConnectorRejected(connector, "with non-canonical HTTPS port");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.port = 0;
	RequireConnectorRejected(connector, "with zero origin port");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.allowed_schemes.push_back("http");
	RequireConnectorRejected(connector, "with widened scheme capability");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.allowed_hosts.push_back("example.com");
	RequireConnectorRejected(connector, "with widened host capability");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.redirects_enabled = true;
	RequireConnectorRejected(connector, "with redirects enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.private_addresses_enabled = true;
	RequireConnectorRejected(connector, "with private-address authority enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.link_local_addresses_enabled = true;
	RequireConnectorRejected(connector, "with link-local authority enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.loopback_addresses_enabled = true;
	RequireConnectorRejected(connector, "with loopback authority inconsistent with HTTPS origin");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.max_response_bytes = 0;
	RequireConnectorRejected(connector, "with zero response budget");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.resource_ceilings.max_records = 0;
	RequireConnectorRejected(connector, "with zero record budget");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.resource_ceilings.max_extracted_string_bytes = 0;
	RequireConnectorRejected(connector, "with zero extracted-string budget");
}

void TestRequestCounterexamples() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	auto request = BuildLiveScanRequest(connector);
	request.connector_name = "other";
	RequireRequestRejected(request, "with wrong connector");
	request = BuildLiveScanRequest(connector);
	request.relation_name = "other";
	RequireRequestRejected(request, "with wrong relation");
	request = BuildLiveScanRequest(connector);
	request.explicit_inputs.push_back("unexpected");
	RequireRequestRejected(request, "with explicit inputs");
	request = BuildLiveScanRequest(connector);
	request.projected_columns.pop_back();
	RequireRequestRejected(request, "with incomplete projection closure");
	request = BuildLiveScanRequest(connector);
	request.predicate = "id > 1";
	RequireRequestRejected(request, "with unavailable predicate");
	request = BuildLiveScanRequest(connector);
	request.orderings.push_back("id");
	RequireRequestRejected(request, "with unavailable ordering");
	request = BuildLiveScanRequest(connector);
	request.has_limit = true;
	RequireRequestRejected(request, "with unavailable limit");
	request = BuildLiveScanRequest(connector);
	request.has_offset = true;
	RequireRequestRejected(request, "with unavailable offset");
	request = BuildLiveScanRequest(connector);
	request.capabilities.projection = true;
	RequireRequestRejected(request, "with projection capability");
	request = BuildLiveScanRequest(connector);
	request.capabilities.cancellation = false;
	RequireRequestRejected(request, "without verified cancellation");
}

} // namespace

int main() {
	try {
		TestSourceConstantsAreNotPushdown();
		TestConnectorCeilingsNarrowSemanticAndHostBudgets();
		TestPrivateControlledCapabilityUsesTheSamePlanner();
		TestConnectorCounterexamples();
		TestRequestCounterexamples();
		std::cout << "scan planner tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan planner tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
