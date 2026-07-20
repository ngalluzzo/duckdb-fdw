#include "input_resolution.hpp"
#include "operation_selection.hpp"

#include "connector/support/package_generation_test_fixtures.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api::CompiledRequiredInputKind;
using duckdb_api::ExplicitInput;
using duckdb_api::ExplicitInputs;
using duckdb_api::ExplicitInputValueKind;
using duckdb_api::LogicalSecretReference;
using duckdb_api::PlanningError;
using duckdb_api::PlanningErrorCode;
using duckdb_api::RequestedPredicate;
using duckdb_api::RequestedPredicateComparisonOperator;
using duckdb_api::RequestedPredicateValue;
using duckdb_api::RequestedPredicateValueKind;
using duckdb_api::ScanRequest;
using duckdb_api::input_resolution::ResolvedInputState;
using duckdb_api_test::Require;

const duckdb_api::CompiledRelation &TypedRelation(const duckdb_api::CompiledPackageGeneration &generation) {
	const auto *relation = generation.Connector().FindRelation(duckdb_api_test::PACKAGE_TYPED_RELATION);
	if (relation == nullptr) {
		throw std::runtime_error("package fixture lost its typed relation");
	}
	return *relation;
}

ScanRequest TypedRequest(const duckdb_api::CompiledPackageGeneration &generation, ExplicitInputs inputs) {
	auto request = duckdb_api::BuildConservativeScanRequest(
	    generation.Connector(), duckdb_api_test::PACKAGE_TYPED_RELATION, LogicalSecretReference());
	request.explicit_inputs = std::move(inputs);
	return request;
}

const duckdb_api::CompiledRelation &PackageRelation(const duckdb_api::CompiledPackageGeneration &generation,
                                                    const std::string &relation_name) {
	const auto *relation = generation.Connector().FindRelation(relation_name);
	if (relation == nullptr) {
		throw std::runtime_error("package fixture lost relation " + relation_name);
	}
	return *relation;
}

ScanRequest PredicateRequest(const duckdb_api::CompiledPackageGeneration &generation, const std::string &relation_name,
                             RequestedPredicate predicate) {
	auto request =
	    duckdb_api::BuildConservativeScanRequest(generation.Connector(), relation_name, LogicalSecretReference());
	request.requested_predicate = std::move(predicate);
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	return request;
}

RequestedPredicate Equality(std::size_t column_index, RequestedPredicateValueKind kind,
                            RequestedPredicateValue literal) {
	return RequestedPredicate::Comparison(column_index, kind, RequestedPredicateComparisonOperator::EQUALS,
	                                      std::move(literal));
}

template <class Callback>
void RequireSelectionFailure(Callback callback, const std::string &counterexample) {
	bool rejected = false;
	try {
		callback();
	} catch (const PlanningError &error) {
		rejected = error.Code() == PlanningErrorCode::OPERATION_SELECTION_FAILED;
	}
	Require(rejected, "operation selection accepted " + counterexample);
}

void TestPackageOriginUsesTaggedRelationInputSelection() {
	const auto generation = duckdb_api_test::BuildTypedFallbackPackageGenerationFixture();
	const auto omitted = duckdb_api::BuildConservativeScanPlan(generation.Connector(), TypedRequest(generation, {}));
	Require(omitted.Operation().Rest().operation_name == "typed_default",
	        "omitted package input did not select the sole fallback");

	const auto selected = duckdb_api::BuildConservativeScanPlan(
	    generation.Connector(), TypedRequest(generation, {ExplicitInput::Varchar("query", "records")}));
	const auto repeated = duckdb_api::BuildConservativeScanPlan(
	    generation.Connector(), TypedRequest(generation, {ExplicitInput::Varchar("query", "records")}));
	Require(selected.Operation().Rest().operation_name == "typed_by_query" &&
	            selected.ConnectorName() == generation.Identity().ConnectorId() &&
	            selected.Snapshot() == repeated.Snapshot(),
	        "concrete package relation input did not select its tagged operation");

	const auto empty = duckdb_api::BuildConservativeScanPlan(
	    generation.Connector(), TypedRequest(generation, {ExplicitInput::Varchar("query", "")}));
	Require(empty.Operation().Rest().operation_name == "typed_by_query",
	        "empty VARCHAR was treated as a missing required relation input");
}

void TestMissingAndTiedPackageOperationsFailDeterministically() {
	const auto generation = duckdb_api_test::BuildTypedTiePackageGenerationFixture();
	RequireSelectionFailure(
	    [&generation]() {
		    (void)duckdb_api::BuildConservativeScanPlan(generation.Connector(), TypedRequest(generation, {}));
	    },
	    "a missing required relation input with no fallback");
	RequireSelectionFailure(
	    [&generation]() {
		    (void)duckdb_api::BuildConservativeScanPlan(
		        generation.Connector(), TypedRequest(generation, {ExplicitInput::Varchar("query", "records")}));
	    },
	    "two equally ranked tagged operations by declaration order");
}

void RequireTypedPackageEquality(const duckdb_api::CompiledPackageGeneration &generation,
                                 const std::string &relation_name, RequestedPredicateValueKind kind,
                                 RequestedPredicateValue matching, RequestedPredicateValue nonmatching,
                                 const std::string &encoded_value) {
	const auto &relation = PackageRelation(generation, relation_name);
	const auto resolved = duckdb_api::input_resolution::ResolveRelationInputs(relation, ExplicitInputs());
	const auto matching_request = PredicateRequest(generation, relation_name, Equality(1, kind, std::move(matching)));
	const auto bindings = duckdb_api::predicate_classifier::ResolveCandidateInputBindings(
	    relation, relation.Operations()[0], matching_request);
	Require(!bindings.conflicting && bindings.values.size() == 1 &&
	            bindings.values[0].name == relation.Columns()[1].name &&
	            bindings.values[0].encoded_value == encoded_value,
	        "typed package equality did not derive its exact candidate-local binding for " + relation_name);
	Require(duckdb_api::operation_selection::SelectOperation(relation, matching_request, resolved).name ==
	            relation_name + "_selected",
	        "typed package equality did not select its required-input operation for " + relation_name);

	const auto nonmatching_request =
	    PredicateRequest(generation, relation_name, Equality(1, kind, std::move(nonmatching)));
	const auto nonmatching_bindings = duckdb_api::predicate_classifier::ResolveCandidateInputBindings(
	    relation, relation.Operations()[0], nonmatching_request);
	Require(!nonmatching_bindings.conflicting && nonmatching_bindings.values.empty() &&
	            duckdb_api::operation_selection::SelectOperation(relation, nonmatching_request, resolved).name ==
	                relation_name + "_fallback",
	        "nonmatching package literal fabricated a binding or bypassed the fallback for " + relation_name);
}

void TestPackageTypedEqualitySelectionEvidence() {
	const auto generation = duckdb_api_test::BuildTypedPredicatePackageGenerationFixture();
	RequireTypedPackageEquality(generation, "boolean_predicates", RequestedPredicateValueKind::BOOLEAN,
	                            RequestedPredicateValue::Boolean(true), RequestedPredicateValue::Boolean(false),
	                            "true");
	RequireTypedPackageEquality(generation, "bigint_predicates", RequestedPredicateValueKind::BIGINT,
	                            RequestedPredicateValue::BigInt(42), RequestedPredicateValue::BigInt(41), "42");
	RequireTypedPackageEquality(generation, "varchar_predicates", RequestedPredicateValueKind::VARCHAR,
	                            RequestedPredicateValue::Varchar(""), RequestedPredicateValue::Varchar("other"), "");
}

void TestConflictingPackageConditionalsDisqualifyOnlyTheirCandidate() {
	const auto generation = duckdb_api_test::BuildPredicateConflictPackageGenerationFixture();
	const auto &relation = PackageRelation(generation, duckdb_api_test::PACKAGE_PREDICATE_RELATION);
	const auto resolved = duckdb_api::input_resolution::ResolveRelationInputs(relation, ExplicitInputs());
	const auto private_only = PredicateRequest(
	    generation, relation.Name(),
	    Equality(1, RequestedPredicateValueKind::VARCHAR, RequestedPredicateValue::Varchar("private")));
	const auto private_bindings = duckdb_api::predicate_classifier::ResolveCandidateInputBindings(
	    relation, relation.Operations()[0], private_only);
	Require(!private_bindings.conflicting && private_bindings.values.size() == 1 &&
	            private_bindings.values[0].name == "visibility" &&
	            private_bindings.values[0].encoded_value == "private" &&
	            duckdb_api::operation_selection::SelectOperation(relation, private_only, resolved).name ==
	                "controlled_exact_repositories",
	        "a nonconflicting package conditional did not select its own candidate");

	std::vector<RequestedPredicate> conflicting_leaves;
	conflicting_leaves.push_back(
	    Equality(1, RequestedPredicateValueKind::VARCHAR, RequestedPredicateValue::Varchar("private")));
	conflicting_leaves.push_back(
	    Equality(1, RequestedPredicateValueKind::VARCHAR, RequestedPredicateValue::Varchar("public")));
	const auto conflicting =
	    PredicateRequest(generation, relation.Name(), RequestedPredicate::Conjunction(std::move(conflicting_leaves)));
	const auto conflicting_bindings = duckdb_api::predicate_classifier::ResolveCandidateInputBindings(
	    relation, relation.Operations()[0], conflicting);
	Require(conflicting_bindings.conflicting &&
	            duckdb_api::operation_selection::SelectOperation(relation, conflicting, resolved).name ==
	                "controlled_all_repositories",
	        "conflicting conditionals escaped their candidate or contaminated the empty fallback");

	const auto fallback = duckdb_api::BuildConservativeScanPlan(generation.Connector(), conflicting);
	const auto repeated = duckdb_api::BuildConservativeScanPlan(generation.Connector(), conflicting);
	Require(fallback.Operation().Rest().operation_name == "controlled_all_repositories" &&
	            fallback.PredicateCategory() == duckdb_api::PredicateDecisionCategory::UNSUPPORTED &&
	            fallback.PredicateReason() == duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE &&
	            fallback.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
	            fallback.ResidualPredicate() == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER &&
	            fallback.Snapshot() == repeated.Snapshot(),
	        "end-to-end conflict containment lost fallback, residual ownership, or determinism");
}

void TestRelationAndConditionalNamespacesNeverCollapse() {
	const auto generation = duckdb_api_test::BuildTypedFallbackPackageGenerationFixture();
	const auto &relation = TypedRelation(generation);
	const auto omitted = duckdb_api::input_resolution::ResolveRelationInputs(relation, ExplicitInputs());
	const duckdb_api::predicate_classifier::CandidateInputBindings conditional_query {{{"query", "encoded"}}, false};
	Require(!duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::RELATION_INPUT,
	                                                                   "query", omitted, conditional_query) &&
	            duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::CONDITIONAL_INPUT,
	                                                                      "query", omitted, conditional_query),
	        "a conditional binding satisfied the same-ID relation-input namespace");

	const auto explicit_query = duckdb_api::input_resolution::ResolveRelationInputs(
	    relation, ExplicitInputs({ExplicitInput::Varchar("query", "records")}));
	const duckdb_api::predicate_classifier::CandidateInputBindings no_conditionals {{}, false};
	Require(duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::RELATION_INPUT,
	                                                                  "query", explicit_query, no_conditionals) &&
	            !duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::CONDITIONAL_INPUT,
	                                                                       "query", explicit_query, no_conditionals),
	        "a concrete relation input satisfied the same-ID conditional namespace");

	const auto null_cursor = duckdb_api::input_resolution::ResolveRelationInputs(
	    relation, ExplicitInputs({ExplicitInput::Null("cursor", ExplicitInputValueKind::VARCHAR)}));
	Require(null_cursor.Find("cursor")->State() == ResolvedInputState::BOUND_NULL &&
	            !duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::RELATION_INPUT,
	                                                                       "cursor", null_cursor, no_conditionals),
	        "BOUND_NULL satisfied a required relation-input reference");
	Require(duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::RELATION_INPUT,
	                                                                  "limit", omitted, no_conditionals),
	        "a concrete compiled default did not satisfy its relation-input reference");

	const duckdb_api::predicate_classifier::CandidateInputBindings conflicting {{{"query", "one"}}, true};
	Require(!duckdb_api::operation_selection::RequiredInputIsSatisfied(CompiledRequiredInputKind::CONDITIONAL_INPUT,
	                                                                   "query", explicit_query, conflicting),
	        "conflicting candidate-local conditionals satisfied an operation selector");

	bool unknown_rejected = false;
	try {
		(void)duckdb_api::operation_selection::RequiredInputIsSatisfied(static_cast<CompiledRequiredInputKind>(99),
		                                                                "query", explicit_query, no_conditionals);
	} catch (const PlanningError &error) {
		unknown_rejected = error.Code() == PlanningErrorCode::INVALID_CONTRACT;
	}
	Require(unknown_rejected, "an unknown required-input namespace did not fail closed");
}

} // namespace

void RunOperationSelectionLawTests() {
	TestPackageOriginUsesTaggedRelationInputSelection();
	TestMissingAndTiedPackageOperationsFailDeterministically();
	TestPackageTypedEqualitySelectionEvidence();
	TestConflictingPackageConditionalsDisqualifyOnlyTheirCandidate();
	TestRelationAndConditionalNamespacesNeverCollapse();
}
