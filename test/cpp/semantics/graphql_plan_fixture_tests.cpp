#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <string>

namespace duckdb_api_test {
namespace graphql_semantics {

void TestFixtureBoundary() {
	const auto production = BuildProductionPlan();
	const auto fixture = BuildValidGraphqlScanPlanFixture("graphql_semantics_secret");
	Require(fixture.Operation().Protocol() == production.Operation().Protocol() &&
	            fixture.Domain() == production.Domain() &&
	            fixture.Operation().Graphql().document == production.Operation().Graphql().document &&
	            fixture.Operation().Graphql().document_digest == production.Operation().Graphql().document_digest &&
	            fixture.OutputColumns().size() == production.OutputColumns().size() &&
	            fixture.Pagination().GraphqlCursor().page_size == production.Pagination().GraphqlCursor().page_size &&
	            fixture.Pagination().ScanBudgets().serialized_request_body_bytes ==
	                production.Pagination().ScanBudgets().serialized_request_body_bytes,
	        "Semantics GraphQL fixture drifted from the production planned value");

	const auto identity =
	    BuildGraphqlPlanCounterexample("graphql_semantics_secret", GraphqlPlanCounterexample::OTHER_DOCUMENT_IDENTITY);
	const auto digest =
	    BuildGraphqlPlanCounterexample("graphql_semantics_secret", GraphqlPlanCounterexample::OTHER_DOCUMENT_DIGEST);
	const auto domain =
	    BuildGraphqlPlanCounterexample("graphql_semantics_secret", GraphqlPlanCounterexample::OTHER_DOMAIN);
	const auto replay =
	    BuildGraphqlPlanCounterexample("graphql_semantics_secret", GraphqlPlanCounterexample::UNKNOWN_REPLAY_SAFETY);
	const auto cursor =
	    BuildGraphqlPlanCounterexample("graphql_semantics_secret", GraphqlPlanCounterexample::OTHER_CURSOR_PAGE_SIZE);
	const auto resource =
	    BuildGraphqlPlanCounterexample("graphql_semantics_secret", GraphqlPlanCounterexample::ZERO_PAGE_BODY_BUDGET);
	const auto ownership = BuildGraphqlPlanCounterexample("graphql_semantics_secret",
	                                                      GraphqlPlanCounterexample::RUNTIME_ORDERING_DELEGATED);
	const auto nullability = BuildGraphqlPlanCounterexample("graphql_semantics_secret",
	                                                        GraphqlPlanCounterexample::PRIMARY_LANGUAGE_REQUIRED);
	Require(identity.Operation().Graphql().document_identity != fixture.Operation().Graphql().document_identity &&
	            digest.Operation().Graphql().document_digest != fixture.Operation().Graphql().document_digest &&
	            domain.Domain() != fixture.Domain() &&
	            replay.Operation().Graphql().replay_safety != fixture.Operation().Graphql().replay_safety &&
	            cursor.Pagination().GraphqlCursor().page_size != fixture.Pagination().GraphqlCursor().page_size &&
	            resource.Pagination().PageBudgets().serialized_request_body_bytes == 0 &&
	            ownership.RuntimeOrdering() != fixture.RuntimeOrdering() && !nullability.OutputColumns()[4].nullable,
	        "closed GraphQL counterexamples did not isolate operation, domain, replay, cursor, resource, ownership, "
	        "and nullability facts");

	const auto safe = fixture.Snapshot();
	Require(safe.find(fixture.Operation().Graphql().document) == std::string::npos &&
	            safe.find("graphql_semantics_secret") == std::string::npos,
	        "GraphQL fixture explanation exposed document or logical secret bytes");
	const auto copied = fixture;
	Require(copied.Snapshot() == fixture.Snapshot(), "GraphQL fixture copy changed immutable plan facts");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
