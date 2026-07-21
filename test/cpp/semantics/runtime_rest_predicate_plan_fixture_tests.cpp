#include "semantics/support/runtime_rest_predicate_plan_test_fixtures.hpp"

#include "support/require.hpp"

#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

const duckdb_api::PlannedRestQueryBinding &ConditionalBinding(const duckdb_api::ScanPlan &plan) {
	const duckdb_api::PlannedRestQueryBinding *result = nullptr;
	for (const auto &binding : plan.Operation().Rest().query_bindings) {
		if (binding.Source() != duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT) {
			continue;
		}
		Require(result == nullptr, "runtime REST predicate fixture contains duplicate conditional bindings");
		result = &binding;
	}
	Require(result != nullptr, "runtime REST predicate fixture lacks its conditional binding");
	return *result;
}

void RequireDuckDbOwnership(const duckdb_api::ScanPlan &plan) {
	Require(plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().projection == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB,
	        "runtime REST predicate fixture transferred a relational operator out of DuckDB");
}

void TestExactPackagePlanIsRealAndComplete() {
	const auto plan = duckdb_api_test::BuildRuntimeExactRestPredicatePlanFixture();
	const auto repeated = duckdb_api_test::BuildRuntimeExactRestPredicatePlanFixture();
	Require(plan.Snapshot() == repeated.Snapshot() && plan.ConnectorName() == "typed_predicate_package" &&
	            plan.RelationName() == "bigint_predicates" &&
	            plan.Operation().Rest().operation_name == "bigint_predicates_selected" &&
	            plan.RemotePredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::REST_QUERY_BINDING &&
	            plan.PredicateCategory() == duckdb_api::PredicateDecisionCategory::EXACT,
	        "real planner did not produce the bounded EXACT package REST fixture");
	RequireDuckDbOwnership(plan);

	const auto *equality = plan.TypedEquality();
	Require(equality != nullptr && equality->ColumnName() == "rank" &&
	            equality->Kind() == duckdb_api::PlannedRestScalarKind::BIGINT && equality->BigintValue() == 42 &&
	            equality->ConditionalInputId() == "rank" &&
	            equality->OccurrencePreservation() ==
	                duckdb_api::PlannedOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
	        "EXACT fixture lost its typed value, source ID, or exact occurrence proof");
	const auto &binding = ConditionalBinding(plan);
	Require(binding.Name() == "rank_filter" && binding.SourceId() == "rank" &&
	            binding.Kind() == duckdb_api::PlannedRestScalarKind::BIGINT && binding.BigintValue() == 42 &&
	            binding.EncodedValue() == "42",
	        "EXACT fixture lost its sole canonical conditional binding");
	plan.ValidatePredicateMaterialization();
}

void TestResidualOnlyFallbackHasNoRequestAuthority() {
	const auto plan = duckdb_api_test::BuildRuntimeResidualOnlyRestPredicatePlanFixture();
	Require(plan.ConnectorName() == "residual_predicate_package" && plan.RelationName() == "residual_predicates" &&
	            plan.Operation().Rest().operation_name == "residual_predicates_default" &&
	            plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
	            plan.PredicateCategory() == duckdb_api::PredicateDecisionCategory::UNSUPPORTED &&
	            plan.TypedEquality() != nullptr,
	        "capability fallback did not retain the typed residual-only decision: " + plan.Snapshot());
	for (const auto &binding : plan.Operation().Rest().query_bindings) {
		Require(binding.Source() != duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT,
		        "residual-only fallback emitted conditional request authority");
	}
	RequireDuckDbOwnership(plan);
	plan.ValidatePredicateMaterialization();
}

void TestClosedMalformedCounterexamples() {
	using duckdb_api_test::RuntimeRestPredicatePlanCounterexample;
	for (int value = 0; value < static_cast<int>(RuntimeRestPredicatePlanCounterexample::COUNT); value++) {
		const auto counterexample = static_cast<RuntimeRestPredicatePlanCounterexample>(value);
		const auto plan = duckdb_api_test::BuildRuntimeRestPredicatePlanCounterexample(counterexample);
		const auto &bindings = plan.Operation().Rest().query_bindings;
		if (counterexample == RuntimeRestPredicatePlanCounterexample::DUPLICATE_CONDITIONAL_BINDING) {
			std::size_t conditional_count = 0;
			for (const auto &binding : bindings) {
				conditional_count +=
				    binding.Source() == duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT ? 1 : 0;
			}
			Require(conditional_count == 2, "duplicate-conditional counterexample changed the wrong field");
		} else {
			const auto &binding = ConditionalBinding(plan);
			switch (counterexample) {
			case RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SOURCE_ID:
				Require(binding.SourceId() == "other_rank", "source-ID counterexample changed the wrong field");
				break;
			case RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SCALAR_KIND:
				Require(binding.Kind() == duckdb_api::PlannedRestScalarKind::VARCHAR &&
				            binding.VarcharValue() == "42" && binding.EncodedValue() == "42",
				        "scalar-kind counterexample is not a coherent different scalar");
				break;
			case RuntimeRestPredicatePlanCounterexample::CONDITIONAL_TYPED_VALUE:
				Require(binding.BigintValue() == 41 && binding.EncodedValue() == "41",
				        "typed-value counterexample is not a coherent different BIGINT");
				break;
			case RuntimeRestPredicatePlanCounterexample::NONCANONICAL_ENCODED_VALUE:
				Require(binding.BigintValue() == 42 && binding.EncodedValue() == "0042",
				        "encoded-value counterexample changed decoded authority");
				break;
			case RuntimeRestPredicatePlanCounterexample::DUPLICATE_CONDITIONAL_BINDING:
			case RuntimeRestPredicatePlanCounterexample::COUNT:
				throw std::logic_error("unreachable runtime REST predicate fixture branch");
			}
		}

		if (counterexample != RuntimeRestPredicatePlanCounterexample::NONCANONICAL_ENCODED_VALUE) {
			bool rejected = false;
			try {
				plan.ValidatePredicateMaterialization();
			} catch (const std::logic_error &) {
				rejected = true;
			}
			Require(rejected, "Semantics accepted a malformed runtime REST predicate fixture");
		}
	}

	bool rejected_unknown = false;
	try {
		(void)duckdb_api_test::BuildRuntimeRestPredicatePlanCounterexample(
		    RuntimeRestPredicatePlanCounterexample::COUNT);
	} catch (const std::invalid_argument &) {
		rejected_unknown = true;
	}
	Require(rejected_unknown, "runtime REST predicate fixture accepted an open-ended variant");
}

void TestNativeVisibilityRemainsIsolated() {
	const auto plan = duckdb_api_test::BuildRuntimeNativePredicateIsolationPlanFixture();
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE &&
	            plan.TypedEquality() == nullptr &&
	            plan.PredicateCategory() == duckdb_api::PredicateDecisionCategory::SUPERSET,
	        "generic package fixture handling reinterpreted native 0.7 visibility authority");
	for (const auto &binding : plan.Operation().Rest().query_bindings) {
		Require(binding.Source() != duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT,
		        "native 0.7 visibility acquired a package conditional binding");
	}
	RequireDuckDbOwnership(plan);
	plan.ValidatePredicateMaterialization();
}

} // namespace

void RunRuntimeRestPredicatePlanFixtureTests() {
	TestExactPackagePlanIsRealAndComplete();
	TestResidualOnlyFallbackHasNoRequestAuthority();
	TestClosedMalformedCounterexamples();
	TestNativeVisibilityRemainsIsolated();
}
