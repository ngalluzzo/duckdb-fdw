#include "duckdb_api/scan_plan.hpp"
#include "support/connector_catalog_test_fixtures.hpp"
#include "support/live_scan_request.hpp"
#include "support/require.hpp"
#include "support/scan_plan_contract_test_support.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::BuildAnonymousScanRequest;
using duckdb_api_test::BuildAuthenticatedScanRequest;
using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelationByRequirement;
using duckdb_api_test::scan_plan_contract::RequireThrows;

void RequireRequestRejected(const duckdb_api::CompiledConnector &connector, const duckdb_api::ScanRequest &request,
                            const std::string &counterexample) {
	RequireThrows<std::logic_error>(
	    [&connector, &request]() { (void)duckdb_api::BuildConservativeScanPlan(connector, request); },
	    "planner accepted " + counterexample);
}

void RequireBudgetFieldBounded(const duckdb_api::ResourceBudgets &baseline,
                               std::uint64_t duckdb_api::ResourceBudgets::*field, std::uint64_t host_cap,
                               const std::string &name) {
	auto invalid = baseline;
	invalid.*field = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "zero " + name + " budget was accepted");
	invalid = baseline;
	invalid.*field = host_cap + 1;
	Require(!invalid.IsWithinLiveRestBounds(), name + " budget widened its host cap");
}

void TestExactSelectionHasNoFallback() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto &anonymous = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto &authenticated =
	    FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::REQUIRED);
	const auto anonymous_plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	const auto authenticated_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, authenticated.Name(), "selected_secret"));
	Require(anonymous_plan.RelationName() == anonymous.Name() &&
	            anonymous_plan.OutputColumns()[0].name == anonymous.Columns()[0].name &&
	            authenticated_plan.RelationName() == authenticated.Name() &&
	            authenticated_plan.OutputColumns()[0].name == authenticated.Columns()[0].name,
	        "exact lookup selected another relation's identity or schema");

	auto missing = BuildAnonymousScanRequest(connector, anonymous.Name());
	missing.relation_name = "missing_relation";
	RequireRequestRejected(connector, missing, "an unknown relation with an available fallback operation");
	auto case_varied = BuildAuthenticatedScanRequest(connector, authenticated.Name(), "selected_secret");
	case_varied.relation_name[0] = case_varied.relation_name[0] == 'f' ? 'F' : 'f';
	RequireRequestRejected(connector, case_varied, "a case-varied relation identifier");
	auto wrong_connector = BuildAnonymousScanRequest(connector, anonymous.Name());
	wrong_connector.connector_name = "other_connector";
	RequireRequestRejected(connector, wrong_connector, "a connector/request identity mismatch");
}

void TestReferenceRequirementMatrix() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &anonymous = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto &authenticated =
	    FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::REQUIRED);

	const auto anonymous_request = BuildAnonymousScanRequest(connector, anonymous.Name());
	const auto authenticated_request = BuildAuthenticatedScanRequest(connector, authenticated.Name(), "matrix_secret");
	const auto anonymous_plan = duckdb_api::BuildConservativeScanPlan(connector, anonymous_request);
	const auto authenticated_plan = duckdb_api::BuildConservativeScanPlan(connector, authenticated_request);
	Require(!anonymous_plan.SecretReference().IsPresent() && authenticated_plan.SecretReference().IsPresent(),
	        "valid absent/present reference states did not plan");

	auto surplus = anonymous_request;
	surplus.secret_reference = duckdb_api::LogicalSecretReference::Named("surplus_secret");
	RequireRequestRejected(connector, surplus, "an anonymous request with a surplus reference");
	auto missing = authenticated_request;
	missing.secret_reference = duckdb_api::LogicalSecretReference();
	RequireRequestRejected(connector, missing, "an authenticated request without a reference");

	RequireThrows<std::invalid_argument>(
	    [&connector, &anonymous]() {
		    (void)duckdb_api::BuildConservativeScanRequest(connector, anonymous.Name(),
		                                                   duckdb_api::LogicalSecretReference::Named("surplus_secret"));
	    },
	    "Query request builder accepted a surplus reference");
	RequireThrows<std::invalid_argument>(
	    [&connector, &authenticated]() {
		    (void)duckdb_api::BuildConservativeScanRequest(connector, authenticated.Name(),
		                                                   duckdb_api::LogicalSecretReference());
	    },
	    "Query request builder accepted a missing required reference");
	RequireThrows<std::invalid_argument>([]() { (void)duckdb_api::LogicalSecretReference::Named(""); },
	                                     "logical reference admitted an empty present state");
}

void TestSecretManagerCapabilityIsRequirementScoped() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &anonymous = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto &authenticated =
	    FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::REQUIRED);

	auto anonymous_without_capability = BuildAnonymousScanRequest(connector, anonymous.Name());
	anonymous_without_capability.capabilities.secret_manager = false;
	const auto without = duckdb_api::BuildConservativeScanPlan(connector, anonymous_without_capability);
	auto anonymous_with_capability = anonymous_without_capability;
	anonymous_with_capability.capabilities.secret_manager = true;
	const auto with = duckdb_api::BuildConservativeScanPlan(connector, anonymous_with_capability);
	Require(without.Snapshot() == with.Snapshot() && without.Authentication() == duckdb_api::FeatureState::DISABLED,
	        "ambient Secret Manager availability changed anonymous relational meaning");

	auto authenticated_without_capability =
	    BuildAuthenticatedScanRequest(connector, authenticated.Name(), "capability_secret");
	authenticated_without_capability.capabilities.secret_manager = false;
	RequireRequestRejected(connector, authenticated_without_capability,
	                       "a required reference without Secret Manager capability");
}

void TestUnavailableRelationalCounterexamples() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto &relation = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto valid = BuildAnonymousScanRequest(connector, relation.Name());

	auto request = valid;
	request.explicit_inputs.push_back("secret=selector");
	RequireRequestRejected(connector, request, "a logical selector encoded as an explicit input");
	request = valid;
	request.projected_columns.pop_back();
	RequireRequestRejected(connector, request, "an incomplete projection closure");
	request = valid;
	request.projected_columns[0] = relation.Columns().back().name;
	RequireRequestRejected(connector, request, "a mismatched selected schema");
	request = valid;
	request.predicate = "public_id > 1";
	RequireRequestRejected(connector, request, "a predicate unavailable from the adapter");
	request = valid;
	request.orderings.push_back("public_id");
	RequireRequestRejected(connector, request, "ordering unavailable from the adapter");
	request = valid;
	request.has_limit = true;
	RequireRequestRejected(connector, request, "a limit unavailable from the adapter");
	request = valid;
	request.has_offset = true;
	RequireRequestRejected(connector, request, "an offset unavailable from the adapter");
	request = valid;
	request.capabilities.projection = true;
	RequireRequestRejected(connector, request, "unexpected projection delegation");
	request = valid;
	request.capabilities.filter = true;
	RequireRequestRejected(connector, request, "unexpected filter delegation");
	request = valid;
	request.capabilities.ordering = true;
	RequireRequestRejected(connector, request, "unexpected ordering delegation");
	request = valid;
	request.capabilities.limit = true;
	RequireRequestRejected(connector, request, "unexpected limit delegation");
	request = valid;
	request.capabilities.offset = true;
	RequireRequestRejected(connector, request, "unexpected offset delegation");
	request = valid;
	request.capabilities.progress = true;
	RequireRequestRejected(connector, request, "unexpected progress capability");
	request = valid;
	request.capabilities.cancellation = false;
	RequireRequestRejected(connector, request, "unverified cancellation");
}

void TestResponseSourceCardinalityAndLimitAreIndependent() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto &anonymous = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto &authenticated =
	    FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::REQUIRED);
	const auto many =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	const auto one = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, authenticated.Name(), "cardinality_secret"));

	Require(many.Operation().cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            many.Operation().response_source == duckdb_api::PlannedResponseSource::JSON_PATH_MANY &&
	            many.Domain() == duckdb_api::BaseDomain::JSON_PATH_RECORDS && many.Budgets().decoded_records == 4,
	        "generic multi-record source retained the native three-row domain");
	Require(one.Operation().cardinality == duckdb_api::PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	            one.Operation().response_source == duckdb_api::PlannedResponseSource::ROOT_OBJECT &&
	            one.Domain() == duckdb_api::BaseDomain::SUCCESSFUL_ROOT_OBJECT && one.Budgets().decoded_records == 1,
	        "single-success source lost cardinality, response source, domain, or separate record ceiling");
	for (const auto *plan : {&many, &one}) {
		Require(plan->RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
		            plan->RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
		            plan->RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
		            plan->RuntimeOffset() == duckdb_api::RelationalDelegation::NONE &&
		            plan->Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
		        "source cardinality or record budget granted early row-removal authority");
	}
}

void TestFixedSourceInputsRemainNonRelational() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &anonymous = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	Require(plan.Operation().query_parameters.size() == anonymous.Operation().request.query_parameters.size() &&
	            !plan.Operation().query_parameters.empty(),
	        "fixed source query fields disappeared from the selected operation");
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
	        "fixed source query fields were reclassified as predicate or limit pushdown");
}

void TestResourceEnvelopeBounds() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto &anonymous = FindRelationByRequirement(connector, duckdb_api::CompiledCredentialRequirement::NONE);
	const auto plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	Require(plan.Budgets().response_bytes == connector.NetworkPolicy().max_response_bytes &&
	            plan.Budgets().decoded_records == anonymous.ResourceCeilings().max_records &&
	            plan.Budgets().extracted_string_bytes == anonymous.ResourceCeilings().max_extracted_string_bytes,
	        "smaller provider ceilings did not narrow host budgets");

	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::response_bytes,
	                          duckdb_api::HOST_MAX_RESPONSE_BYTES, "response-byte");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::header_bytes,
	                          duckdb_api::HOST_MAX_HEADER_BYTES, "header-byte");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::decompressed_bytes,
	                          duckdb_api::HOST_MAX_DECOMPRESSED_BYTES, "decompressed-byte");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::decoded_records,
	                          duckdb_api::HOST_MAX_DECODED_RECORDS, "decoded-record");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::extracted_string_bytes,
	                          duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES, "extracted-string-byte");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::json_nesting,
	                          duckdb_api::HOST_MAX_JSON_NESTING, "JSON-nesting");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::decoded_memory_bytes,
	                          duckdb_api::HOST_MAX_DECODED_MEMORY_BYTES, "decoded-memory-byte");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::batch_rows, duckdb_api::OUTPUT_BATCH_ROWS,
	                          "batch-row");
	RequireBudgetFieldBounded(plan.Budgets(), &duckdb_api::ResourceBudgets::wall_milliseconds,
	                          duckdb_api::MAX_EXECUTION_MILLISECONDS, "wall-time");

	auto invalid = plan.Budgets();
	invalid.request_attempts = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "resource envelope removed the one required request attempt");
	invalid = plan.Budgets();
	invalid.request_attempts = 2;
	Require(!invalid.IsWithinLiveRestBounds(), "resource envelope enabled a retry attempt");
	invalid = plan.Budgets();
	invalid.concurrency = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "resource envelope removed its one concurrency slot");
	invalid = plan.Budgets();
	invalid.concurrency = 2;
	Require(!invalid.IsWithinLiveRestBounds(), "resource envelope enabled parallel transfers");
	invalid = plan.Budgets();
	invalid.decoded_records = duckdb_api::HOST_MAX_DECODED_RECORDS;
	Require(invalid.IsWithinLiveRestBounds(), "generic host decoder ceiling retained a native relation-specific cap");
}

} // namespace

int main() {
	try {
		TestExactSelectionHasNoFallback();
		TestReferenceRequirementMatrix();
		TestSecretManagerCapabilityIsRequirementScoped();
		TestUnavailableRelationalCounterexamples();
		TestResponseSourceCardinalityAndLimitAreIndependent();
		TestFixedSourceInputsRemainNonRelational();
		TestResourceEnvelopeBounds();
		std::cout << "scan planner tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan planner tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
