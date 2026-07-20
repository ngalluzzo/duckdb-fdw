#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "support/require.hpp"

#include <string>

namespace duckdb_api_test {
namespace graphql_semantics {

void TestBaseDomain() {
	const auto plan = BuildProductionPlan();
	Require(plan.Domain() == duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
	            plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "GraphQL plan did not preserve the unrestricted duplicate-occurrence base domain");
	Require(plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().projection == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE,
	        "GraphQL plan delegated a DuckDB-owned relational operator");
	Require(plan.ClassificationReason().find("duplicate-preserving mutable occurrence bag") != std::string::npos &&
	            plan.ClassificationReason().find("grants no DuckDB ordering or snapshot") != std::string::npos,
	        "GraphQL classification omitted duplicate, ordering, or snapshot semantics");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
