#include "duckdb_api/authorization.hpp"
#include "duckdb_api/execution.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::ControlledResponse;
using duckdb_api_test::PackageGraphqlRuntimeRecipeCounterexample;
using duckdb_api_test::Require;

class NeverCancelled final : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

std::string GraphqlPage(const std::string &id, bool active, bool more, const std::string &cursor) {
	return std::string("{\"data\":{\"account\":{\"events\":{\"nodes\":[{\"id\":\"") + id +
	       "\",\"active\":" + (active ? "true" : "false") + "}],\"pagination\":{\"more\":" + (more ? "true" : "false") +
	       ",\"next\":" + (cursor.empty() ? "null" : "\"" + cursor + "\"") + "}}}}}";
}

void TestNonGithubPackageGraphqlExecutesTwoCursorPages(const std::string &repository_root) {
	const auto runtime = duckdb_api_test::BuildControlledPackageHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, GraphqlPage("event-1", true, true, "regional-cursor")),
	                          ControlledResponse(200, GraphqlPage("event-2", false, false, ""))});
	NeverCancelled control;
	auto stream =
	    runtime->Executor()->Open(duckdb_api_test::BuildNonGithubPackageGraphqlPlan(repository_root), control);
	Require(runtime->Observations().empty(), "package GraphQL Open performed request or body work");

	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
	            batch.rows[0].values[0].varchar_value == "event-1" && batch.rows[0].values[1].boolean_value,
	        "package GraphQL page one did not decode its declared schema");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
	            batch.rows[0].values[0].varchar_value == "event-2" && !batch.rows[0].values[1].boolean_value &&
	            !stream->Next(control, batch),
	        "package GraphQL cursor page did not decode and exhaust");

	const auto observations = runtime->Observations();
	Require(observations.size() == 2, "package GraphQL execution did not remain sequential and one-attempt");
	for (const auto &observation : observations) {
		Require(observation.method == "POST" && observation.scheme == "https" &&
		            observation.host == "api.example.com" && observation.port == 8443 &&
		            observation.target == "/v1/graphql-events" && observation.content_type == "application/json" &&
		            observation.headers.size() == 1 && observation.headers[0].first == "X-Client" &&
		            observation.headers[0].second == "duckdb-api-test",
		        "package GraphQL request lost its typed non-GitHub authority");
	}
	Require(observations[0].body.find("{\"query\":\"query AcmeRegionalEvents") == 0 &&
	            observations[0].body.find("\"variables\":{\"pageSize\":50,\"cursor\":null}") != std::string::npos &&
	            observations[1].body.find("\"cursor\":\"regional-cursor\"") != std::string::npos,
	        "package GraphQL renderer or cursor body materialization drifted");
}

void TestNondefaultPortRestAndLinkContinuation(const std::string &repository_root) {
	const auto explicit_port = duckdb_api_test::BuildControlledPackageHttpRuntime();
	explicit_port->RespondSequence(
	    {ControlledResponse(200, "[{\"id\":\"event-1\",\"active\":true}]",
	                        {"<https://api.example.com:8443/v1/events?per_page=50&page=2>; rel=next"}),
	     ControlledResponse(200, "[{\"id\":\"event-2\",\"active\":false}]")});
	NeverCancelled control;
	auto stream =
	    explicit_port->Executor()->Open(duckdb_api_test::BuildNonGithubPackageRestPlan(repository_root), control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
	            batch.rows[0].values[0].varchar_value == "event-1" && stream->Next(control, batch) &&
	            batch.rows[0].values[0].varchar_value == "event-2" && !stream->Next(control, batch),
	        "package REST did not follow the exact nondefault-port continuation");
	const auto observations = explicit_port->Observations();
	Require(observations.size() == 2 && observations[0].host == "api.example.com" && observations[0].port == 8443 &&
	            observations[0].target == "/v1/events?per_page=50&page=1" &&
	            observations[1].target == "/v1/events?per_page=50&page=2" && observations[0].headers.size() == 1 &&
	            observations[0].headers[0].first == "X-Client",
	        "package REST request reconstruction lost its path, query, header, or port");

	const auto omitted_port = duckdb_api_test::BuildControlledPackageHttpRuntime();
	omitted_port->RespondSequence(
	    {ControlledResponse(200, "[{\"id\":\"event-1\",\"active\":true}]",
	                        {"<https://api.example.com/v1/events?per_page=50&page=2>; rel=next"}),
	     ControlledResponse(200, "[{\"id\":\"event-2\",\"active\":false}]")});
	auto denied =
	    omitted_port->Executor()->Open(duckdb_api_test::BuildNonGithubPackageRestPlan(repository_root), control);
	bool rejected = false;
	try {
		(void)denied->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = error.Stage() == duckdb_api::ErrorStage::POLICY && error.Field() == "pagination.next";
	}
	Require(rejected && omitted_port->Observations().size() == 1,
	        "omitted nondefault Link port reached another request or used the wrong failure");
}

void TestPackageRecipeCounterexamplesIssueZeroRequests(const std::string &repository_root) {
	const auto count = static_cast<std::size_t>(PackageGraphqlRuntimeRecipeCounterexample::COUNT);
	Require(count == 38, "Runtime recipe counterexample corpus changed without updating the execution oracle");
	NeverCancelled control;
	for (std::size_t value = 0; value < count; value++) {
		const auto runtime = duckdb_api_test::BuildControlledPackageHttpRuntime();
		const auto plan = duckdb_api_test::BuildPackageGraphqlRuntimeRecipeCounterexample(
		    repository_root, "package_recipe_secret", static_cast<PackageGraphqlRuntimeRecipeCounterexample>(value));
		bool rejected = false;
		try {
			(void)runtime->Executor()->OpenWithAuthorization(
			    plan, duckdb_api::ScanAuthorization::Bearer("package_recipe_counterexample_token"), control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = error.Stage() == duckdb_api::ErrorStage::POLICY;
		}
		Require(rejected && runtime->Observations().empty(),
		        "package recipe counterexample reached transport at index " + std::to_string(value));
	}
}

void TestDestinationAndAddressWideningIssueZeroRequests() {
	using duckdb_api_test::NetworkPlanCounterexample;
	const NetworkPlanCounterexample counterexamples[] = {
	    NetworkPlanCounterexample::WIDENED_HOSTS, NetworkPlanCounterexample::PRIVATE_ADDRESSES_ENABLED,
	    NetworkPlanCounterexample::LINK_LOCAL_ADDRESSES_ENABLED, NetworkPlanCounterexample::LOOPBACK_ADDRESSES_ENABLED};
	NeverCancelled control;
	for (std::size_t index = 0; index < sizeof(counterexamples) / sizeof(counterexamples[0]); index++) {
		const auto runtime = duckdb_api_test::BuildControlledPackageHttpRuntime();
		bool rejected = false;
		try {
			(void)runtime->Executor()->OpenWithAuthorization(
			    duckdb_api_test::BuildNetworkPlanCounterexample("network_policy_secret", counterexamples[index]),
			    duckdb_api::ScanAuthorization::Bearer("network_policy_counterexample_token"), control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = error.Stage() == duckdb_api::ErrorStage::POLICY;
		}
		Require(rejected && runtime->Observations().empty(),
		        "destination or address widening reached transport at index " + std::to_string(index));
	}
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "expected absolute repository root argument");
		const std::string repository_root(argv[1]);
		TestNonGithubPackageGraphqlExecutesTwoCursorPages(repository_root);
		TestNondefaultPortRestAndLinkContinuation(repository_root);
		TestPackageRecipeCounterexamplesIssueZeroRequests(repository_root);
		TestDestinationAndAddressWideningIssueZeroRequests();
		std::cout << "package HTTP execution tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package HTTP execution tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
