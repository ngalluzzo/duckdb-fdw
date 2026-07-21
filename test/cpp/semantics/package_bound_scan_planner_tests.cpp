#include "duckdb_api/package_bound_scan_planner.hpp"

#include "connector/support/package_generation_test_fixtures.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "support/require.hpp"

#include <string>
#include <type_traits>

namespace {

using duckdb_api_test::Require;

duckdb_api::ScanRequest PredicateRequest(const duckdb_api::CompiledPackageGeneration &generation,
                                         const std::string &relation_name, bool selective_predicate) {
	auto request = duckdb_api::BuildConservativeScanRequest(generation.Connector(), relation_name,
	                                                        duckdb_api::LogicalSecretReference());
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    1, duckdb_api::RequestedPredicateValueKind::BIGINT, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::BigInt(42));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = selective_predicate;
	request.capabilities.retains_predicate = true;
	return request;
}

struct BoundPlanningFixture {
	explicit BoundPlanningFixture(const duckdb_api::CompiledPackageGeneration &generation)
	    : handle(generation.OpaqueHandle()), service(generation),
	      request(PredicateRequest(generation, "bigint_predicates", true)) {
	}

	duckdb_api::CompiledGenerationHandle handle;
	duckdb_api::PackageBoundScanPlanningService service;
	duckdb_api::ScanRequest request;
};

BoundPlanningFixture DetachedExactPlanningFixture() {
	const auto generation = duckdb_api_test::BuildTypedPredicatePackageGenerationFixture();
	return BoundPlanningFixture(generation);
}

template <class Callback>
void RequireInvalidContract(Callback callback, const std::string &counterexample) {
	bool rejected = false;
	try {
		callback();
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == duckdb_api::PlanningErrorCode::INVALID_CONTRACT;
	}
	Require(rejected, "package-bound planner accepted " + counterexample);
}

void TestExactAndCopiedHandlesPlanAfterProviderRelease() {
	auto fixture = DetachedExactPlanningFixture();
	const auto exact = fixture.service.Plan(fixture.handle, fixture.request);
	const duckdb_api::CompiledGenerationHandle copied_handle(fixture.handle);
	const auto repeated = fixture.service.Plan(copied_handle, fixture.request);
	Require(exact.Snapshot() == repeated.Snapshot() &&
	            exact.RemotePredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            exact.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
	            exact.ResidualPredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            exact.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            exact.ConditionalInput() == duckdb_api::PlannedConditionalInput::REST_QUERY_BINDING,
	        "package-bound planner lost deterministic EXACT package semantics or copied-handle ownership");
}

void TestSameIdentityDifferentGenerationIsRejected() {
	const auto bound = duckdb_api_test::BuildTypedPredicatePackageGenerationFixture();
	const auto same_identity = duckdb_api_test::BuildTypedPredicatePackageGenerationFixture();
	Require(bound.Identity().SpecIdentifier() == same_identity.Identity().SpecIdentifier() &&
	            bound.Identity().ConnectorId() == same_identity.Identity().ConnectorId() &&
	            bound.Identity().PackageVersion() == same_identity.Identity().PackageVersion() &&
	            bound.Identity().PackageDigest() == same_identity.Identity().PackageDigest() &&
	            !bound.OpaqueHandle().IsSameGeneration(same_identity.OpaqueHandle()),
	        "same-identity counterexample did not create distinct immutable generations");
	const duckdb_api::PackageBoundScanPlanningService service(bound);
	const auto request = PredicateRequest(bound, "bigint_predicates", true);
	RequireInvalidContract(
	    [&service, &same_identity, &request]() { (void)service.Plan(same_identity.OpaqueHandle(), request); },
	    "the same package identity backed by a different generation");
}

void TestResidualOnlyPackagePlanning() {
	const auto generation = duckdb_api_test::BuildResidualPredicatePackageGenerationFixture();
	const duckdb_api::PackageBoundScanPlanningService service(generation);
	const auto request = PredicateRequest(generation, duckdb_api_test::PACKAGE_RESIDUAL_PREDICATE_RELATION, false);
	const auto plan = service.Plan(generation.OpaqueHandle(), request);
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE && plan.TypedEquality() != nullptr,
	        "package-bound planner lost residual-only typed fallback semantics");
	for (const auto &binding : plan.Operation().Rest().query_bindings) {
		Require(binding.Source() != duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT,
		        "package-bound residual plan emitted conditional request authority");
	}
}

void TestNativePlanningRemainsOutsidePackageBinding() {
	static_assert(!std::is_default_constructible<duckdb_api::PackageBoundScanPlanningService>::value,
	              "package-bound planning must require an immutable package generation");
	static_assert(
	    !std::is_constructible<duckdb_api::PackageBoundScanPlanningService, duckdb_api::CompiledConnector>::value,
	    "package-bound planning must not accept native Connector values");

	const auto native = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = native.FindRelation("authenticated_repositories");
	Require(relation != nullptr, "native isolation oracle lost its repository relation");
	auto request = duckdb_api::BuildConservativeScanRequest(
	    native, relation->Name(), duckdb_api::LogicalSecretReference::Named("package_bound_native_isolation"));
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    5, duckdb_api::RequestedPredicateValueKind::VARCHAR, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::Varchar("private"));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	const auto plan = duckdb_api::BuildConservativeScanPlan(native, request);
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE &&
	            plan.TypedEquality() == nullptr,
	        "package-bound service changed or reinterpreted native planning");
}

} // namespace

void RunPackageBoundScanPlannerTests() {
	TestExactAndCopiedHandlesPlanAfterProviderRelease();
	TestSameIdentityDifferentGenerationIsRejected();
	TestResidualOnlyPackagePlanning();
	TestNativePlanningRemainsOutsidePackageBinding();
}
