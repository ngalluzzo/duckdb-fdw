#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "semantics/support/graphql_plan_equality.hpp"
#include "semantics/support/graphql_plan_test_variations.hpp"
#include "semantics/support/graphql_protocol_test_inspection.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace duckdb_api_test {
namespace graphql_semantics {

void TestFixtureBoundary() {
	const auto production = BuildProductionPlan();
	const auto fixture = BuildValidGraphqlScanPlanFixture("graphql_semantics_secret");
	Require(CountGraphqlPlanDifferences(fixture, production) == 0,
	        "Semantics GraphQL fixture drifted from a public production plan fact");
	Require(fixture.SourceSnapshot() != production.SourceSnapshot() && fixture.Snapshot() == production.Snapshot(),
	        "typed GraphQL fixture agreement improperly depends on Connector source prose");

	const auto admission_count = static_cast<std::size_t>(GraphqlRuntimeAdmissionCounterexample::COUNT);
	Require(admission_count == 142, "closed Runtime-facing GraphQL admission catalog changed without self-test review");
	for (std::size_t value = 0; value < admission_count; value++) {
		const auto counterexample = static_cast<GraphqlRuntimeAdmissionCounterexample>(value);
		const auto candidate = BuildGraphqlRuntimeAdmissionCounterexample("graphql_semantics_secret", counterexample);
		const auto differences = CountGraphqlPlanDifferences(fixture, candidate);
		Require(differences == 1, "GraphQL Runtime admission counterexample " + std::to_string(value) + " changed " +
		                              std::to_string(differences) + " structured facts instead of one");
	}
	bool sentinel_rejected = false;
	try {
		(void)BuildGraphqlRuntimeAdmissionCounterexample("graphql_semantics_secret",
		                                                 GraphqlRuntimeAdmissionCounterexample::COUNT);
	} catch (const std::invalid_argument &) {
		sentinel_rejected = true;
	}
	Require(sentinel_rejected, "GraphQL Runtime admission fixture accepted a value outside its closed enum");

	struct ExpectedMalformedProtocol {
		GraphqlRuntimeAdmissionCounterexample counterexample;
		GraphqlProtocolEnvelopeShape shape;
	};
	const ExpectedMalformedProtocol malformed_protocols[] = {
	    {GraphqlRuntimeAdmissionCounterexample::UNKNOWN_PROTOCOL,
	     {static_cast<duckdb_api::PlannedProtocol>(127), false, true}},
	    {GraphqlRuntimeAdmissionCounterexample::GRAPHQL_CONTRADICTORY_PROTOCOL_PAYLOADS,
	     {duckdb_api::PlannedProtocol::GRAPHQL, true, true}},
	    {GraphqlRuntimeAdmissionCounterexample::GRAPHQL_MISSING_ACTIVE_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::GRAPHQL, false, false}},
	    {GraphqlRuntimeAdmissionCounterexample::GRAPHQL_WRONG_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::GRAPHQL, true, false}},
	    {GraphqlRuntimeAdmissionCounterexample::REST_CONTRADICTORY_PROTOCOL_PAYLOADS,
	     {duckdb_api::PlannedProtocol::REST, true, true}},
	    {GraphqlRuntimeAdmissionCounterexample::REST_MISSING_ACTIVE_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::REST, false, false}},
	    {GraphqlRuntimeAdmissionCounterexample::REST_WRONG_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::REST, false, true}}};
	for (const auto &malformed_protocol : malformed_protocols) {
		const auto candidate =
		    BuildGraphqlRuntimeAdmissionCounterexample("graphql_semantics_secret", malformed_protocol.counterexample);
		const auto actual_shape = InspectGraphqlProtocolEnvelope(candidate);
		Require(actual_shape.protocol == malformed_protocol.shape.protocol &&
		            actual_shape.rest_present == malformed_protocol.shape.rest_present &&
		            actual_shape.graphql_present == malformed_protocol.shape.graphql_present,
		        "malformed protocol fixture did not preserve its distinct closed-envelope shape");
		bool graphql_failed = false;
		bool rest_failed = false;
		try {
			(void)candidate.Operation().Graphql();
		} catch (const std::logic_error &) {
			graphql_failed = true;
		}
		try {
			(void)candidate.Operation().Rest();
		} catch (const std::logic_error &) {
			rest_failed = true;
		}
		Require(graphql_failed && rest_failed,
		        "malformed protocol envelope exposed an executable payload through the public sum API");
	}

	const auto variation_count = static_cast<std::size_t>(GraphqlPlanVariation::COUNT);
	Require(variation_count == 5, "internal GraphQL equality variation catalog changed without self-test review");
	for (std::size_t value = 0; value < variation_count; value++) {
		const auto variation = static_cast<GraphqlPlanVariation>(value);
		const auto candidate = BuildGraphqlPlanVariation("graphql_semantics_secret", variation);
		Require(CountGraphqlPlanDifferences(fixture, candidate) == 1,
		        "internal GraphQL equality variation did not isolate one non-admission fact");
	}

	const auto safe = fixture.Snapshot();
	Require(safe.find(fixture.Operation().Graphql().document) == std::string::npos &&
	            safe.find("graphql_semantics_secret") == std::string::npos,
	        "GraphQL fixture explanation exposed document or logical secret bytes");
	const auto copied = fixture;
	Require(copied.Snapshot() == fixture.Snapshot(), "GraphQL fixture copy changed immutable plan facts");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
