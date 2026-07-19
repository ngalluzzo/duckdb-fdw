#include "duckdb_api/relational_predicate.hpp"
#include "support/require.hpp"

#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api_test::Require;

void TestClosedValueIdentityAndSnapshot() {
	const auto default_value = duckdb_api::RequestedPredicate();
	const auto unrestricted = duckdb_api::RequestedPredicate::Unrestricted();
	const auto selective = duckdb_api::RequestedPredicate::VisibilityEqualsPrivate();

	Require(default_value == unrestricted && unrestricted != selective,
	        "requested predicate default or closed identity is not conservative");
	Require(unrestricted.Kind() == duckdb_api::RequestedPredicateKind::UNRESTRICTED &&
	            selective.Kind() == duckdb_api::RequestedPredicateKind::VISIBILITY_EQUALS_PRIVATE,
	        "requested predicate kind did not preserve the closed states");
	Require(unrestricted.Snapshot() == "unrestricted" && selective.Snapshot() == "visibility_equals_private",
	        "requested predicate snapshot is unstable");
	Require(selective.Snapshot().find("=") == std::string::npos &&
	            selective.Snapshot().find("visibility=private") == std::string::npos,
	        "requested predicate snapshot became SQL or request authority");
}

enum class VisibilityValue { PUBLIC, PRIVATE, INTERNAL, SQL_NULL };

bool DuckDbVisibilityPredicate(VisibilityValue visibility) {
	return visibility == VisibilityValue::PRIVATE;
}

bool RemotePrivateVisibilityDomain(VisibilityValue visibility) {
	return visibility == VisibilityValue::PRIVATE;
}

void TestSameFieldImplicationAndResidualBag() {
	const std::vector<VisibilityValue> base = {VisibilityValue::PUBLIC, VisibilityValue::PRIVATE,
	                                           VisibilityValue::INTERNAL, VisibilityValue::PRIVATE,
	                                           VisibilityValue::SQL_NULL};
	std::vector<VisibilityValue> local_only;
	std::vector<VisibilityValue> remote_then_residual;
	for (const auto &visibility : base) {
		const auto duckdb_true = DuckDbVisibilityPredicate(visibility);
		const auto remotely_retained = RemotePrivateVisibilityDomain(visibility);
		Require(!duckdb_true || remotely_retained, "same-field visibility mapping violated D=>R");
		if (duckdb_true) {
			local_only.push_back(visibility);
		}
		if (remotely_retained && duckdb_true) {
			remote_then_residual.push_back(visibility);
		}
	}
	Require(local_only == remote_then_residual,
	        "remote visibility restriction plus DuckDB residual changed the duplicate-preserving row bag");
}

void TestBroaderPrivateBooleanCounterexampleRemainsRejected() {
	const bool internal_private_boolean = true;
	const auto internal_visibility = VisibilityValue::INTERNAL;
	Require(internal_private_boolean && !RemotePrivateVisibilityDomain(internal_visibility),
	        "test no longer exhibits the broader private-boolean counterexample");
	Require(duckdb_api::RequestedPredicate::VisibilityEqualsPrivate().Snapshot().find("boolean") == std::string::npos,
	        "closed requested predicate accidentally names the rejected broader Boolean");
}

} // namespace

static_assert(std::is_default_constructible<duckdb_api::RequestedPredicate>::value,
              "closed request value must default to unrestricted");
static_assert(std::is_copy_constructible<duckdb_api::RequestedPredicate>::value,
              "Query must be able to copy the closed request value into immutable bind state");
static_assert(!std::is_constructible<duckdb_api::RequestedPredicate, std::string>::value,
              "closed request value must not admit SQL or arbitrary predicate text");

void RunRelationalPredicateTests() {
	TestClosedValueIdentityAndSnapshot();
	TestSameFieldImplicationAndResidualBag();
	TestBroaderPrivateBooleanCounterexampleRemainsRejected();
}
