#include "duckdb_api/relational_predicate.hpp"
#include "support/require.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::Require;

duckdb_api::RequestedPredicate VisibilityPrivate(std::size_t column_index) {
	return duckdb_api::RequestedPredicate::Comparison(column_index, duckdb_api::RequestedPredicateValueKind::VARCHAR,
	                                                  duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	                                                  duckdb_api::RequestedPredicateValue::Varchar("private"));
}

template <class ACTION>
void RequireInvalid(const ACTION &action, const std::string &message) {
	bool rejected = false;
	try {
		action();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, message);
}

template <class ACTION>
void RequireLogicError(const ACTION &action, const std::string &message) {
	bool rejected = false;
	try {
		action();
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, message);
}

void TestTypedValuesAndSafeSnapshots() {
	const auto bigint = duckdb_api::RequestedPredicateValue::BigInt(-42);
	const auto varchar_value = duckdb_api::RequestedPredicateValue::Varchar("private;\n");
	const auto boolean = duckdb_api::RequestedPredicateValue::Boolean(true);
	Require(bigint.Kind() == duckdb_api::RequestedPredicateValueKind::BIGINT && bigint.BigIntValue() == -42 &&
	            bigint.Snapshot() == "bigint:-42",
	        "BIGINT predicate value lost its typed identity");
	Require(varchar_value.Kind() == duckdb_api::RequestedPredicateValueKind::VARCHAR &&
	            varchar_value.VarcharValue() == "private;\n" &&
	            varchar_value.Snapshot() == "varchar:hex:707269766174653b0a",
	        "VARCHAR predicate value was not preserved and escaped safely");
	Require(boolean.Kind() == duckdb_api::RequestedPredicateValueKind::BOOLEAN && boolean.BooleanValue() &&
	            boolean.Snapshot() == "boolean:true",
	        "BOOLEAN predicate value lost its typed identity");
	RequireLogicError([&]() { (void)bigint.VarcharValue(); }, "BIGINT value exposed a VARCHAR payload");
	RequireInvalid(
	    []() {
		    (void)duckdb_api::RequestedPredicate::Comparison(0, duckdb_api::RequestedPredicateValueKind::BIGINT,
		                                                     duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
		                                                     duckdb_api::RequestedPredicateValue::Varchar("1"));
	    },
	    "comparison admitted mismatched column and literal types");
}

void TestCandidateIdentityStructureAndOpaquePositions() {
	const auto unrestricted = duckdb_api::RequestedPredicate();
	const auto comparison = VisibilityPrivate(5);
	const auto unsupported = duckdb_api::RequestedPredicate::Unsupported(7);
	const auto conjunction = duckdb_api::RequestedPredicate::Conjunction({comparison, unsupported});
	const auto disjunction = duckdb_api::RequestedPredicate::Disjunction({comparison, unsupported});
	const auto negation = duckdb_api::RequestedPredicate::Negation(comparison);

	Require(unrestricted == duckdb_api::RequestedPredicate::Unrestricted() &&
	            unrestricted.Kind() == duckdb_api::RequestedPredicateKind::UNRESTRICTED &&
	            unrestricted.Snapshot() == "true",
	        "requested predicate default was not conservative TRUE");
	Require(comparison.Kind() == duckdb_api::RequestedPredicateKind::COMPARISON && comparison.BoundColumnIndex() == 5 &&
	            comparison.BoundColumnType() == duckdb_api::RequestedPredicateValueKind::VARCHAR &&
	            comparison.Literal().VarcharValue() == "private" && comparison.Depth() == 1 &&
	            comparison.NodeCount() == 1,
	        "comparison leaf lost its bound typed identity");
	Require(unsupported.Kind() == duckdb_api::RequestedPredicateKind::UNSUPPORTED &&
	            unsupported.UnsupportedPosition() == 7 && unsupported.Snapshot() == "unsupported[position:7]",
	        "opaque unsupported leaf lost its deterministic position");
	Require(conjunction.Kind() == duckdb_api::RequestedPredicateKind::CONJUNCTION && conjunction.Depth() == 2 &&
	            conjunction.NodeCount() == 3 && conjunction.Children().size() == 2 &&
	            conjunction.Snapshot().find("and[") == 0,
	        "conjunction lost ordered child structure or accounting");
	Require(disjunction.Kind() == duckdb_api::RequestedPredicateKind::DISJUNCTION &&
	            disjunction.Children()[0] == comparison && disjunction.Children()[1] == unsupported,
	        "disjunction reordered or rewrote its children");
	Require(negation.Kind() == duckdb_api::RequestedPredicateKind::NEGATION && negation.Children().size() == 1 &&
	            negation.Children()[0] == comparison,
	        "negation lost its sole child");
	Require(conjunction != duckdb_api::RequestedPredicate::Conjunction({unsupported, comparison}),
	        "candidate equality ignored deterministic child order");
	for (const auto &forbidden : {"visibility =", "visibility=private", "SELECT", "WHERE"}) {
		Require(conjunction.Snapshot().find(forbidden) == std::string::npos,
		        "candidate snapshot became SQL or request authority: " + std::string(forbidden));
	}
}

void TestDepthAndNodeBounds() {
	std::vector<duckdb_api::RequestedPredicate> leaves;
	for (std::size_t index = 0; index < duckdb_api::MAX_REQUESTED_PREDICATE_NODES - 1; index++) {
		leaves.push_back(duckdb_api::RequestedPredicate::Unsupported(index));
	}
	const auto maximum = duckdb_api::RequestedPredicate::Conjunction(leaves);
	Require(maximum.NodeCount() == duckdb_api::MAX_REQUESTED_PREDICATE_NODES && maximum.Depth() == 2,
	        "candidate did not admit its exact node ceiling");
	leaves.push_back(duckdb_api::RequestedPredicate::Unsupported(999));
	RequireInvalid([&]() { (void)duckdb_api::RequestedPredicate::Conjunction(leaves); },
	               "candidate admitted more than 64 nodes");

	auto depth = VisibilityPrivate(0);
	for (std::size_t level = 1; level < duckdb_api::MAX_REQUESTED_PREDICATE_DEPTH; level++) {
		depth = duckdb_api::RequestedPredicate::Negation(std::move(depth));
	}
	Require(depth.Depth() == duckdb_api::MAX_REQUESTED_PREDICATE_DEPTH,
	        "candidate did not admit its exact depth ceiling");
	RequireInvalid([&]() { (void)duckdb_api::RequestedPredicate::Negation(depth); },
	               "candidate admitted depth beyond 16");
	RequireInvalid([]() { (void)duckdb_api::RequestedPredicate::Conjunction({VisibilityPrivate(0)}); },
	               "conjunction admitted fewer than two children");
}

} // namespace

static_assert(std::is_default_constructible<duckdb_api::RequestedPredicate>::value,
              "candidate must default to conservative TRUE");
static_assert(std::is_copy_constructible<duckdb_api::RequestedPredicate>::value,
              "Query must copy immutable candidates into bind state");
static_assert(!std::is_constructible<duckdb_api::RequestedPredicate, std::string>::value,
              "candidate must not admit SQL or arbitrary predicate text");

void RunRelationalPredicateTests() {
	TestTypedValuesAndSafeSnapshots();
	TestCandidateIdentityStructureAndOpaquePositions();
	TestDepthAndNodeBounds();
}
