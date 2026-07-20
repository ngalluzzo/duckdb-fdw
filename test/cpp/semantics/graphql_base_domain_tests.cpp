#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "support/require.hpp"

#include <string>

namespace duckdb_api_test {
namespace graphql_semantics {

namespace {

void RequireDuckdbOnlyRelationalAuthority(const duckdb_api::ScanPlan &plan, const std::string &profile_name) {
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        profile_name + " local residual changed remote predicate or conditional-input authority");
	Require(plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().projection == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOffset() == duckdb_api::RelationalDelegation::NONE,
	        profile_name + " local residual conferred remote or Runtime relational delegation");
}

} // namespace

void TestBaseDomain() {
	const auto plan = BuildProductionPlan();
	Require(plan.Domain() == duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
	            plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "GraphQL plan did not preserve the unrestricted duplicate-occurrence base domain");
	RequireDuckdbOnlyRelationalAuthority(plan, "unrestricted");
	Require(plan.ClassificationReason().find("duplicate-preserving mutable occurrence bag") != std::string::npos &&
	            plan.ClassificationReason().find("grants no DuckDB ordering or snapshot") != std::string::npos,
	        "GraphQL classification omitted duplicate, ordering, or snapshot semantics");

	struct ExpectedLocalResidual {
		GraphqlLocalResidualProfile profile;
		const char *name;
		duckdb_api::PlannedPredicate residual;
		duckdb_api::PredicateDecisionReason reason;
	};
	const ExpectedLocalResidual cases[] = {
	    {GraphqlLocalResidualProfile::UNRESTRICTED, "unrestricted", duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	     duckdb_api::PredicateDecisionReason::NO_REMOTE_CANDIDATE},
	    {GraphqlLocalResidualProfile::MAPPING_UNAVAILABLE, "archived=false",
	     duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER,
	     duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE},
	    {GraphqlLocalResidualProfile::STRUCTURE_UNSUPPORTED, "unsupported structure",
	     duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER,
	     duckdb_api::PredicateDecisionReason::STRUCTURE_UNSUPPORTED},
	    {GraphqlLocalResidualProfile::CAPABILITY_UNAVAILABLE, "capability unavailable",
	     duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER,
	     duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE}};
	for (const auto &test_case : cases) {
		const auto candidate = BuildProductionPlan(test_case.profile);
		Require(candidate.Domain() == duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
		            candidate.ResidualPredicate() == test_case.residual &&
		            candidate.PredicateCategory() == duckdb_api::PredicateDecisionCategory::UNSUPPORTED &&
		            candidate.PredicateReason() == test_case.reason,
		        std::string(test_case.name) + " did not retain its coherent local-only classification");
		RequireDuckdbOnlyRelationalAuthority(candidate, test_case.name);
	}
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
