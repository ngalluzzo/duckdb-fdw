#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "semantics/support/graphql_plan_equality.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstddef>
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

	const auto counterexample_count = static_cast<std::size_t>(GraphqlPlanCounterexample::COUNT);
	Require(counterexample_count >= 100, "Runtime-facing GraphQL counterexample surface was unexpectedly narrowed");
	for (std::size_t value = 0; value < counterexample_count; value++) {
		const auto counterexample = static_cast<GraphqlPlanCounterexample>(value);
		const auto candidate = BuildGraphqlPlanCounterexample("graphql_semantics_secret", counterexample);
		const auto differences = CountGraphqlPlanDifferences(fixture, candidate);
		Require(differences == 1, "GraphQL counterexample " + std::to_string(value) + " changed " +
		                              std::to_string(differences) + " public plan facts instead of one");
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
