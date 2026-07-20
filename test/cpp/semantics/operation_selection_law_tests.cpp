#include "input_resolution.hpp"
#include "operation_selection.hpp"

#include "connector/support/package_generation_test_fixtures.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <string>

namespace {

using duckdb_api::CompiledRequiredInputKind;
using duckdb_api::ExplicitInput;
using duckdb_api::ExplicitInputs;
using duckdb_api::ExplicitInputValueKind;
using duckdb_api::LogicalSecretReference;
using duckdb_api::PlanningError;
using duckdb_api::PlanningErrorCode;
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
	TestRelationAndConditionalNamespacesNeverCollapse();
}
