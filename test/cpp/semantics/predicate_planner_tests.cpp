#include "duckdb_api/scan_planner.hpp"
#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "query/support/live_scan_request.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using duckdb_api_test::BuildAuthenticatedScanRequest;
using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelation;

std::size_t FindColumn(const duckdb_api::CompiledRelation &relation, const std::string &name) {
	for (std::size_t index = 0; index < relation.Columns().size(); index++) {
		if (relation.Columns()[index].name == name) {
			return index;
		}
	}
	throw std::logic_error("fixture relation is missing required column: " + name);
}

duckdb_api::RequestedPredicate VisibilityPrivate(const duckdb_api::CompiledRelation &relation) {
	return duckdb_api::RequestedPredicate::Comparison(FindColumn(relation, "visibility"),
	                                                  duckdb_api::RequestedPredicateValueKind::VARCHAR,
	                                                  duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	                                                  duckdb_api::RequestedPredicateValue::Varchar("private"));
}

duckdb_api::ScanRequest BaseRequest(const duckdb_api::CompiledConnector &connector,
                                    const duckdb_api::CompiledRelation &relation) {
	if (relation.Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED) {
		return BuildAuthenticatedScanRequest(connector, relation.Name(), "predicate_secret");
	}
	return duckdb_api::BuildConservativeScanRequest(connector, relation.Name(), duckdb_api::LogicalSecretReference());
}

duckdb_api::ScanRequest CandidateRequest(const duckdb_api::CompiledConnector &connector,
                                         const duckdb_api::CompiledRelation &relation,
                                         duckdb_api::RequestedPredicate candidate,
                                         duckdb_api::RetainedPredicateScope retained_scope) {
	auto request = BaseRequest(connector, relation);
	request.requested_predicate = std::move(candidate);
	request.retained_predicate_scope = retained_scope;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	return request;
}

void RequireUnrestricted(const duckdb_api::ScanPlan &plan, duckdb_api::PredicateDecisionCategory category,
                         duckdb_api::PredicateDecisionReason reason, const std::string &context) {
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
	            plan.PredicateCategory() == category && plan.PredicateReason() == reason &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        context + " did not preserve the classified unrestricted plan");
}

void RequireDuckDbEnvelope(const duckdb_api::ScanPlan &plan, const std::string &context) {
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
	        context + " moved projection, filter, ordering, or bound authority out of DuckDB");
}

void TestUnrestrictedBaseline() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BaseRequest(connector, relation));
	RequireUnrestricted(plan, duckdb_api::PredicateDecisionCategory::UNSUPPORTED,
	                    duckdb_api::PredicateDecisionReason::NO_REMOTE_CANDIDATE, "unrestricted baseline");
	Require(plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        "unrestricted baseline fabricated a residual");
	RequireDuckDbEnvelope(plan, "unrestricted baseline");
}

void TestNativeSupersetAndControlledExact() {
	const auto native = duckdb_api::BuildNativeGithubConnector();
	const auto &native_relation = FindRelation(native, "authenticated_repositories");
	const auto superset = duckdb_api::BuildConservativeScanPlan(
	    native, CandidateRequest(native, native_relation, VisibilityPrivate(native_relation),
	                             duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	Require(superset.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            superset.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            superset.PredicateCategory() == duckdb_api::PredicateDecisionCategory::SUPERSET &&
	            superset.PredicateReason() == duckdb_api::PredicateDecisionReason::SELECTED_SUPERSET_MAPPING &&
	            superset.ResidualPredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            superset.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "native validated mapping did not produce the Superset decision");
	RequireDuckDbEnvelope(superset, "native Superset decision");

	const auto controlled = duckdb_api_test::BuildExactPredicateCatalogFixture();
	const auto &controlled_relation = FindRelation(controlled, duckdb_api_test::PREDICATE_EXACT_RELATION);
	const auto exact = duckdb_api::BuildConservativeScanPlan(
	    controlled, CandidateRequest(controlled, controlled_relation, VisibilityPrivate(controlled_relation),
	                                 duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	Require(exact.RelationName() == duckdb_api_test::PREDICATE_EXACT_RELATION &&
	            exact.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            exact.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
	            exact.PredicateCategory() == duckdb_api::PredicateDecisionCategory::EXACT &&
	            exact.PredicateReason() == duckdb_api::PredicateDecisionReason::SELECTED_EXACT_MAPPING &&
	            exact.ResidualPredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            exact.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            exact.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "controlled validated mapping did not produce Exact with retained DuckDB ownership");
	Require(exact.OutputColumns().size() == controlled_relation.Columns().size(),
	        "Exact decision lost its complete output closure");
	RequireDuckDbEnvelope(exact, "controlled Exact decision");
	Require(exact.Snapshot().find("predicate_decision=category:exact,reason:selected_exact_mapping") !=
	            std::string::npos,
	        "Exact plan snapshot omitted its structured decision");

	const auto exact_leaf = VisibilityPrivate(controlled_relation);
	const auto repeated_exact = duckdb_api::BuildConservativeScanPlan(
	    controlled, CandidateRequest(controlled, controlled_relation,
	                                 duckdb_api::RequestedPredicate::Conjunction({exact_leaf, exact_leaf}),
	                                 duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	Require(repeated_exact.PredicateCategory() == duckdb_api::PredicateDecisionCategory::EXACT &&
	            repeated_exact.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
	            repeated_exact.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE &&
	            repeated_exact.ResidualPredicate() == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER,
	        "identical repeated Exact leaves did not resolve to one equivalent typed restriction");
}

void TestBooleanCompositionFallbackAndAmbiguity() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	const auto visibility = VisibilityPrivate(relation);
	const auto unsupported = duckdb_api::RequestedPredicate::Unsupported(1);

	const auto conjunction = duckdb_api::BuildConservativeScanPlan(
	    connector,
	    CandidateRequest(connector, relation, duckdb_api::RequestedPredicate::Conjunction({visibility, unsupported}),
	                     duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER));
	Require(conjunction.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            conjunction.PredicateCategory() == duckdb_api::PredicateDecisionCategory::SUPERSET &&
	            conjunction.ResidualPredicate() == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER &&
	            conjunction.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "safe AND conjunct did not narrow remotely while preserving the complete DuckDB residual");

	const auto disjunction = duckdb_api::BuildConservativeScanPlan(
	    connector,
	    CandidateRequest(connector, relation, duckdb_api::RequestedPredicate::Disjunction({visibility, unsupported}),
	                     duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER));
	RequireUnrestricted(disjunction, duckdb_api::PredicateDecisionCategory::UNSUPPORTED,
	                    duckdb_api::PredicateDecisionReason::DISJUNCTION_ENCODING_UNAVAILABLE,
	                    "unencodable disjunction");

	const auto negation = duckdb_api::BuildConservativeScanPlan(
	    connector, CandidateRequest(connector, relation, duckdb_api::RequestedPredicate::Negation(visibility),
	                                duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	RequireUnrestricted(negation, duckdb_api::PredicateDecisionCategory::UNSUPPORTED,
	                    duckdb_api::PredicateDecisionReason::COMPLEMENT_ENCODING_UNAVAILABLE, "unencodable negation");

	const auto ambiguous_connector = duckdb_api_test::BuildAmbiguousPredicateMappingsCatalogFixture();
	const auto &ambiguous_relation =
	    FindRelation(ambiguous_connector, duckdb_api_test::PREDICATE_AMBIGUOUS_MAPPINGS_RELATION);
	const auto ambiguous = duckdb_api::BuildConservativeScanPlan(
	    ambiguous_connector,
	    CandidateRequest(ambiguous_connector, ambiguous_relation, VisibilityPrivate(ambiguous_relation),
	                     duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	RequireUnrestricted(ambiguous, duckdb_api::PredicateDecisionCategory::AMBIGUOUS,
	                    duckdb_api::PredicateDecisionReason::AMBIGUOUS_CONDITIONAL_INPUT,
	                    "controlled incompatible predicate mappings");
	Require(ambiguous.Operation().Rest().operation_name == "controlled_exact_repositories" &&
	            ambiguous.ResidualPredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE,
	        "predicate ambiguity changed the selected base operation or lost the DuckDB residual");
}

void TestCapabilityMappingAndInvalidBindingMatrix() {
	const auto native = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(native, "authenticated_repositories");
	auto missing_capability = CandidateRequest(native, relation, VisibilityPrivate(relation),
	                                           duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	missing_capability.capabilities.retains_predicate = false;
	RequireUnrestricted(duckdb_api::BuildConservativeScanPlan(native, missing_capability),
	                    duckdb_api::PredicateDecisionCategory::UNSUPPORTED,
	                    duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE,
	                    "missing residual-retention capability");

	const auto absent = duckdb_api_test::BuildPredicateMappingAbsentCatalogFixture();
	const auto &absent_relation = FindRelation(absent, "authenticated_repositories");
	RequireUnrestricted(duckdb_api::BuildConservativeScanPlan(
	                        absent, CandidateRequest(absent, absent_relation, VisibilityPrivate(absent_relation),
	                                                 duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE)),
	                    duckdb_api::PredicateDecisionCategory::UNSUPPORTED,
	                    duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE, "absent mapping");

	auto invalid = CandidateRequest(native, relation,
	                                duckdb_api::RequestedPredicate::Comparison(
	                                    relation.Columns().size(), duckdb_api::RequestedPredicateValueKind::VARCHAR,
	                                    duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	                                    duckdb_api::RequestedPredicateValue::Varchar("private")),
	                                duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	bool rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(native, invalid);
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == duckdb_api::PlanningErrorCode::INVALID_CONTRACT;
	}
	Require(rejected, "invalid bound candidate was relabeled as ordinary fallback");

	auto partial_or = CandidateRequest(native, relation, VisibilityPrivate(relation),
	                                   duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER);
	rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(native, partial_or);
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == duckdb_api::PlanningErrorCode::INVALID_CONTRACT;
	}
	Require(rejected, "comparison-only candidate narrowed remotely while claiming an unseen complete DuckDB filter");

	auto compound_partial_or = CandidateRequest(
	    native, relation,
	    duckdb_api::RequestedPredicate::Conjunction({VisibilityPrivate(relation), VisibilityPrivate(relation)}),
	    duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER);
	rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(native, compound_partial_or);
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == duckdb_api::PlanningErrorCode::INVALID_CONTRACT;
	}
	Require(rejected, "fully represented compound candidate narrowed an unseen outer disjunction");
}

void TestPublicPlanFixturesExposeStructuredCategories() {
	const auto superset = duckdb_api_test::BuildVisibilityPrivatePlanFixture("fixture_predicate_secret");
	const auto invalid_exact_category = duckdb_api_test::BuildRepositoryPlanCounterexample(
	    "fixture_predicate_secret", duckdb_api_test::RepositoryPlanCounterexample::EXACT_CATEGORY_SUPERSET_ACCURACY);
	const auto invalid_exact_accuracy = duckdb_api_test::BuildRepositoryPlanCounterexample(
	    "fixture_predicate_secret", duckdb_api_test::RepositoryPlanCounterexample::SUPERSET_CATEGORY_EXACT_ACCURACY);
	const auto ambiguous = duckdb_api_test::BuildAmbiguousPredicateFallbackPlanFixture("fixture_predicate_secret");
	const auto invalid_ambiguous = duckdb_api_test::BuildRepositoryPlanCounterexample(
	    "fixture_predicate_secret", duckdb_api_test::RepositoryPlanCounterexample::AMBIGUOUS_RESIDUAL_TRUE);
	const auto invalid_mapping = duckdb_api_test::BuildRepositoryPlanCounterexample(
	    "fixture_predicate_secret", duckdb_api_test::RepositoryPlanCounterexample::MAPPING_UNAVAILABLE_RESIDUAL_TRUE);
	Require(superset.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            superset.PredicateCategory() == duckdb_api::PredicateDecisionCategory::SUPERSET &&
	            invalid_exact_category.PredicateCategory() == duckdb_api::PredicateDecisionCategory::EXACT &&
	            invalid_exact_category.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            invalid_exact_accuracy.PredicateCategory() == duckdb_api::PredicateDecisionCategory::SUPERSET &&
	            invalid_exact_accuracy.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
	            ambiguous.PredicateCategory() == duckdb_api::PredicateDecisionCategory::AMBIGUOUS &&
	            ambiguous.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
	            invalid_ambiguous.PredicateCategory() == duckdb_api::PredicateDecisionCategory::AMBIGUOUS &&
	            invalid_ambiguous.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            invalid_mapping.PredicateCategory() == duckdb_api::PredicateDecisionCategory::UNSUPPORTED &&
	            invalid_mapping.PredicateReason() == duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE &&
	            invalid_mapping.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        "public Semantics plan fixtures lost structured decision states");
}

} // namespace

void RunPredicatePlannerTests() {
	TestUnrestrictedBaseline();
	TestNativeSupersetAndControlledExact();
	TestBooleanCompositionFallbackAndAmbiguity();
	TestCapabilityMappingAndInvalidBindingMatrix();
	TestPublicPlanFixturesExposeStructuredCategories();
}
