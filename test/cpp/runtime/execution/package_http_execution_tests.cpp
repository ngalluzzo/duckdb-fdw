#include "duckdb_api/authorization.hpp"
#include "duckdb_api/execution.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ControlledResponse;
using duckdb_api_test::PackageGraphqlRuntimeRecipeCounterexample;
using duckdb_api_test::PackageHttpNumericOriginCounterexample;
using duckdb_api_test::Require;

class NeverCancelled final : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

std::string GraphqlPage(const std::string &id, bool active, bool more, const std::string &cursor) {
	return std::string("{\"data\":{\"organization\":{\"eventFeed\":{\"nodes\":[{\"id\":\"") + id +
	       "\",\"active\":" + (active ? "true" : "false") + ",\"stats\":{\"attendance\":" + (active ? "120" : "25") +
	       "},\"classification\":" + (active ? "{\"label\":\"public\"}" : "null") +
	       "}],\"pagination\":{\"more\":" + (more ? "true" : "false") +
	       ",\"next\":" + (cursor.empty() ? "null" : "\"" + cursor + "\"") + "}}}},\"errors\":[]}";
}

std::string RestPage(const std::string &id, bool active) {
	return std::string("[{\"id\":\"") + id + "\",\"active\":" + (active ? "true" : "false") +
	       ",\"stats\":{\"attendance\":" + (active ? "120" : "25") +
	       "},\"classification\":" + (active ? "{\"label\":\"public\"}" : "{\"label\":\"members\"}") + "}]";
}

void TestNonGithubPackageGraphqlExecutesTwoCursorPages(const std::string &repository_root) {
	const auto runtime = duckdb_api_test::BuildControlledPackageHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, GraphqlPage("event-1", true, true, "regional-cursor")),
	                          ControlledResponse(200, GraphqlPage("event-2", false, false, ""))});
	std::string token = "non_github_graphql_execution_token";
	runtime->ExpectBearer("Bearer " + token);
	NeverCancelled control;
	auto stream =
	    runtime->Executor()->OpenWithAuthorization(duckdb_api_test::BuildNonGithubPackageGraphqlPlan(repository_root),
	                                               duckdb_api::ScanAuthorization::Bearer(std::move(token)), control);
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
		            observation.headers.size() == 2 && observation.headers[0].first == "X-Client" &&
		            observation.headers[0].second == "duckdb-api-test" &&
		            observation.headers[1].first == "Authorization" && observation.headers[1].second == "<redacted>",
		        "package GraphQL request lost its typed non-GitHub authority");
	}
	Require(observations[0].body.find("{\"query\":\"query AcmeRegionalEvents") == 0 &&
	            observations[0].body.find("\"variables\":{\"pageSize\":50,\"cursor\":null}") != std::string::npos &&
	            observations[1].body.find("\"cursor\":\"regional-cursor\"") != std::string::npos &&
	            runtime->ConsumeBearerExpectation(2),
	        "package GraphQL renderer or cursor body materialization drifted");
}

void TestNondefaultPortRestAndLinkContinuation(const std::string &repository_root) {
	const auto explicit_port = duckdb_api_test::BuildControlledPackageHttpRuntime();
	std::string explicit_port_token = "non_github_rest_execution_token";
	explicit_port->ExpectBearer("Bearer " + explicit_port_token);
	explicit_port->RespondSequence(
	    {ControlledResponse(200, RestPage("event-1", true),
	                        {"<https://api.example.com:8443/v1/events?per_page=50&page=2>; rel=next"}),
	     ControlledResponse(200, RestPage("event-2", false))});
	NeverCancelled control;
	auto stream = explicit_port->Executor()->OpenWithAuthorization(
	    duckdb_api_test::BuildNonGithubPackageRestPlan(repository_root),
	    duckdb_api::ScanAuthorization::Bearer(std::move(explicit_port_token)), control);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
	            batch.rows[0].values[0].varchar_value == "event-1" && stream->Next(control, batch) &&
	            batch.rows[0].values[0].varchar_value == "event-2" && !stream->Next(control, batch),
	        "package REST did not follow the exact nondefault-port continuation");
	const auto observations = explicit_port->Observations();
	Require(observations.size() == 2 && observations[0].host == "api.example.com" && observations[0].port == 8443 &&
	            observations[0].target == "/v1/events?per_page=50&page=1" &&
	            observations[1].target == "/v1/events?per_page=50&page=2" && observations[0].headers.size() == 2 &&
	            observations[0].headers[0].first == "X-Client" && observations[0].headers[1].first == "Authorization" &&
	            observations[0].headers[1].second == "<redacted>" && explicit_port->ConsumeBearerExpectation(2),
	        "package REST request reconstruction lost its path, query, header, or port");

	const auto omitted_port = duckdb_api_test::BuildControlledPackageHttpRuntime();
	std::string omitted_port_token = "non_github_rest_denied_link_token";
	omitted_port->ExpectBearer("Bearer " + omitted_port_token);
	omitted_port->RespondSequence(
	    {ControlledResponse(200, RestPage("event-1", true),
	                        {"<https://api.example.com/v1/events?per_page=50&page=2>; rel=next"}),
	     ControlledResponse(200, RestPage("event-2", false))});
	auto denied = omitted_port->Executor()->OpenWithAuthorization(
	    duckdb_api_test::BuildNonGithubPackageRestPlan(repository_root),
	    duckdb_api::ScanAuthorization::Bearer(std::move(omitted_port_token)), control);
	bool rejected = false;
	try {
		(void)denied->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = error.Stage() == duckdb_api::ErrorStage::POLICY && error.Field() == "pagination.next";
	}
	Require(rejected && omitted_port->Observations().size() == 1 && omitted_port->ConsumeBearerExpectation(1),
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

void TestNumericOriginsFailBeforeCredentialOrTransport(const std::string &repository_root) {
	const auto count = static_cast<std::size_t>(PackageHttpNumericOriginCounterexample::COUNT);
	Require(count == 9, "package numeric-origin counterexample corpus changed without updating the execution oracle");
	NeverCancelled control;
	for (std::size_t value = 0; value < count; value++) {
		const auto counterexample = static_cast<PackageHttpNumericOriginCounterexample>(value);
		for (std::size_t protocol = 0; protocol < 2; protocol++) {
			const auto runtime = duckdb_api_test::BuildControlledPackageHttpRuntime();
			std::string token = "numeric_origin_credential_canary";
			runtime->ExpectBearer("Bearer " + token);
			const auto plan = protocol == 0 ? duckdb_api_test::BuildRepositoryPackageRestNumericOriginCounterexample(
			                                      repository_root, "numeric_origin_secret", counterexample)
			                                : duckdb_api_test::BuildRepositoryPackageGraphqlNumericOriginCounterexample(
			                                      repository_root, "numeric_origin_secret", counterexample);
			bool rejected = false;
			try {
				(void)runtime->Executor()->OpenWithAuthorization(
				    plan, duckdb_api::ScanAuthorization::Bearer(std::move(token)), control);
			} catch (const duckdb_api::ExecutionError &error) {
				rejected = error.Stage() == duckdb_api::ErrorStage::POLICY;
			}
			Require(rejected && runtime->Observations().empty() && runtime->ConsumeBearerExpectation(0),
			        "numeric package origin reached credential placement or transport for variation " +
			            std::to_string(value) + ":" + std::to_string(protocol));
		}
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
		TestNumericOriginsFailBeforeCredentialOrTransport(repository_root);
		std::cout << "package HTTP execution tests passed\n";
		return EXIT_SUCCESS;
	} catch (const duckdb_api::ExecutionError &error) {
		std::cerr << "package HTTP execution tests failed at field " << error.Field() << ": " << error.what() << '\n';
		return EXIT_FAILURE;
	} catch (const std::exception &error) {
		std::cerr << "package HTTP execution tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
