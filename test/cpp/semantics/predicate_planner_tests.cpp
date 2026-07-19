#include "duckdb_api/scan_planner.hpp"
#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "query/support/live_scan_request.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <string>

namespace {

using duckdb_api_test::BuildAuthenticatedScanRequest;
using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelation;

duckdb_api::ScanRequest VisibilityRequest(const duckdb_api::CompiledConnector &connector,
                                          const duckdb_api::CompiledRelation &relation, bool selective_capability,
                                          bool residual_retention) {
	auto request = BuildAuthenticatedScanRequest(connector, relation.Name(), "predicate_secret");
	request.requested_predicate = duckdb_api::RequestedPredicate::VisibilityEqualsPrivate();
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = selective_capability;
	request.capabilities.retains_predicate = residual_retention;
	return request;
}

void RequireVisibilityFallback(const duckdb_api::ScanPlan &plan, const std::string &counterexample) {
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "planner did not preserve full traversal and DuckDB residual for " + counterexample);
}

void TestUnrestrictedBaseline() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	const auto plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation.Name(), "predicate_secret"));
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "unrestricted request did not preserve the complete base-domain plan");
}

void TestStructuredUnsupportedReason() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	auto request = BuildAuthenticatedScanRequest(connector, relation.Name(), "predicate_secret");
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
	            plan.ClassificationReason().find("outside the accepted mapping") != std::string::npos,
	        "structured unsupported request lost its conservative plan or safe reason");
}

void TestAcceptedVisibilityMapping() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	const auto plan =
	    duckdb_api::BuildConservativeScanPlan(connector, VisibilityRequest(connector, relation, true, true));

	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "accepted same-field mapping lost classification, residual ownership, or typed input");

	std::size_t visibility_query_fields = 0;
	for (const auto &field : plan.Operation().query_parameters) {
		if (field.name == "visibility") {
			visibility_query_fields++;
		}
	}
	Require(visibility_query_fields == 0,
	        "predicate-derived visibility input was duplicated into the base operation query fields");
	Require(plan.ClassificationReason().find("D=>R") != std::string::npos,
	        "accepted classification omitted its implication reason");

	const auto snapshot = plan.Snapshot();
	Require(snapshot.find(";remote_predicate=visibility_equals_private;remote_accuracy=superset;") !=
	                std::string::npos &&
	            snapshot.find(";residual_predicate=visibility_equals_private;residual_owner=duckdb;") !=
	                std::string::npos &&
	            snapshot.find(";conditional_input=visibility_private;") != std::string::npos,
	        "selected plan snapshot omitted the conservative classification or sole typed input");
}

void TestAcceptedVisibilityMappingPreservesCompleteCompoundResidual() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	auto request = VisibilityRequest(connector, relation, true, true);
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER;
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);

	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE &&
	            plan.ClassificationReason().find("complete structured filter") != std::string::npos &&
	            plan.ClassificationReason().find("complete visibility predicate") == std::string::npos &&
	            plan.Snapshot().find(";residual_predicate=complete_duckdb_filter;residual_owner=duckdb;") !=
	                std::string::npos,
	        "selective compound request did not preserve the complete opaque DuckDB residual");
}

void TestCapabilityFallbackMatrix() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_repositories");
	RequireVisibilityFallback(
	    duckdb_api::BuildConservativeScanPlan(connector, VisibilityRequest(connector, relation, false, false)),
	    "both selective capabilities absent");
	RequireVisibilityFallback(
	    duckdb_api::BuildConservativeScanPlan(connector, VisibilityRequest(connector, relation, false, true)),
	    "structured selective capability absent");
	RequireVisibilityFallback(
	    duckdb_api::BuildConservativeScanPlan(connector, VisibilityRequest(connector, relation, true, false)),
	    "DuckDB residual-retention capability absent");
}

void TestMappingSchemaAndOperationMustAgree() {
	const auto mapping_absent = duckdb_api_test::BuildPredicateMappingAbsentCatalogFixture();
	const auto schema_variation = duckdb_api_test::BuildPredicateSchemaVariationCatalogFixture();
	const auto operation_variation = duckdb_api_test::BuildPredicateOperationVariationCatalogFixture();
	for (const auto *connector : {&mapping_absent, &schema_variation, &operation_variation}) {
		const auto &relation = FindRelation(*connector, "authenticated_repositories");
		RequireVisibilityFallback(
		    duckdb_api::BuildConservativeScanPlan(*connector, VisibilityRequest(*connector, relation, true, true)),
		    "an absent or mismatched mapping, schema, or operation");
		Require(relation.PredicateMappings().empty(),
		        "valid provider counterexample unexpectedly acquired executable predicate authority");
	}
}

void TestPlanOnlySelectiveFixture() {
	const auto fixture = duckdb_api_test::BuildVisibilityPrivatePlanFixture("fixture_predicate_secret");
	Require(fixture.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            fixture.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE &&
	            fixture.SecretReference().Name() == "fixture_predicate_secret",
	        "plan-only selective fixture lost classification, typed input, or logical secret identity");
	const auto copy = fixture;
	Require(copy.Snapshot() == fixture.Snapshot(), "copying an immutable selected plan changed its snapshot");
}

} // namespace

void RunPredicatePlannerTests() {
	TestUnrestrictedBaseline();
	TestStructuredUnsupportedReason();
	TestAcceptedVisibilityMapping();
	TestAcceptedVisibilityMappingPreservesCompleteCompoundResidual();
	TestCapabilityFallbackMatrix();
	TestMappingSchemaAndOperationMustAgree();
	TestPlanOnlySelectiveFixture();
}
