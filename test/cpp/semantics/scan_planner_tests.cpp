#include "duckdb_api/scan_planner.hpp"
#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "connector/support/package_compiler_test_fixtures.hpp"
#include "duckdb_api/package_bound_scan_planner.hpp"
#include "query/support/live_scan_request.hpp"
#include "support/require.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

void RunPredicateCompositionLawTests();
void RunInputResolutionLawTests();
void RunOperationSelectionLawTests();
void RunPackageBoundScanPlannerTests();
void RunPermanentRestScanPlanFixtureTests();
void RunRuntimeRestPredicatePlanFixtureTests();

namespace {

using duckdb_api_test::BuildAnonymousScanRequest;
using duckdb_api_test::BuildAuthenticatedScanRequest;
using duckdb_api_test::BuildDisabledRootArrayRepositoryCandidate;
using duckdb_api_test::BuildPaginationPlannerCandidate;
using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelation;
using duckdb_api_test::scan_plan_contract::RequireThrows;

void RequireRequestRejected(const duckdb_api::CompiledConnector &connector, const duckdb_api::ScanRequest &request,
                            const std::string &counterexample) {
	RequireThrows<std::logic_error>(
	    [&connector, &request]() { (void)duckdb_api::BuildConservativeScanPlan(connector, request); },
	    "planner accepted " + counterexample);
}

void RequirePlanningErrorCode(const duckdb_api::CompiledConnector &connector, const duckdb_api::ScanRequest &request,
                              duckdb_api::PlanningErrorCode code, const std::string &counterexample) {
	bool rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(connector, request);
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == code;
	}
	Require(rejected, "planner did not reject " + counterexample + " with the required structured code");
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
	const auto &anonymous = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto &authenticated = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION);
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
	RequirePlanningErrorCode(connector, missing, duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
	                         "an unknown relation with an available fallback operation");
	auto case_varied = BuildAuthenticatedScanRequest(connector, authenticated.Name(), "selected_secret");
	case_varied.relation_name[0] = case_varied.relation_name[0] == 'f' ? 'F' : 'f';
	RequirePlanningErrorCode(connector, case_varied, duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
	                         "a case-varied relation identifier");
	auto wrong_connector = BuildAnonymousScanRequest(connector, anonymous.Name());
	wrong_connector.connector_name = "other_connector";
	RequirePlanningErrorCode(connector, wrong_connector, duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
	                         "a connector/request identity mismatch");
}

void TestEqualRankedOperationSelectionFailsBeforePlanConstruction() {
	const auto connector = duckdb_api_test::BuildEqualRankedOperationsCatalogFixture();
	const auto &relation = FindRelation(connector, duckdb_api_test::PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION);
	Require(!relation.HasSingleOperation() && relation.Operations().size() == 2,
	        "equal-ranked operation fixture lost its plural selection problem");
	auto request =
	    duckdb_api::BuildConservativeScanRequest(connector, relation.Name(), duckdb_api::LogicalSecretReference());
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    1, duckdb_api::RequestedPredicateValueKind::VARCHAR, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::Varchar("private"));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	RequirePlanningErrorCode(connector, request, duckdb_api::PlanningErrorCode::OPERATION_SELECTION_FAILED,
	                         "equal-ranked eligible base operations");
}

duckdb_api::ScanRequest BuildVisibilityCandidateRequest(const duckdb_api::CompiledConnector &connector,
                                                        const duckdb_api::CompiledRelation &relation) {
	auto request =
	    duckdb_api::BuildConservativeScanRequest(connector, relation.Name(), duckdb_api::LogicalSecretReference());
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    1, duckdb_api::RequestedPredicateValueKind::VARCHAR, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::Varchar("private"));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	return request;
}

void TestCandidateSpecificOperationSelectionAndFallback() {
	const auto winner_connector = duckdb_api_test::BuildUniqueWinnerOperationsCatalogFixture();
	const auto &winner_relation = FindRelation(winner_connector, duckdb_api_test::OPERATION_UNIQUE_WINNER_RELATION);
	const auto winner = duckdb_api::BuildConservativeScanPlan(
	    winner_connector, BuildVisibilityCandidateRequest(winner_connector, winner_relation));
	Require(winner.Operation().Rest().operation_name == "controlled_exact_repositories" &&
	            winner.PredicateCategory() == duckdb_api::PredicateDecisionCategory::EXACT &&
	            winner.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "candidate-specific visibility binding did not select and classify the unique non-fallback operation");

	const auto fallback_connector = duckdb_api_test::BuildFallbackOperationsCatalogFixture();
	const auto &fallback_relation = FindRelation(fallback_connector, duckdb_api_test::OPERATION_FALLBACK_RELATION);
	const auto fallback = duckdb_api::BuildConservativeScanPlan(
	    fallback_connector, duckdb_api::BuildConservativeScanRequest(fallback_connector, fallback_relation.Name(),
	                                                                 duckdb_api::LogicalSecretReference()));
	Require(fallback.Operation().Rest().operation_name == "controlled_selector_fallback_repositories" &&
	            fallback.PredicateCategory() == duckdb_api::PredicateDecisionCategory::UNSUPPORTED &&
	            fallback.PredicateReason() == duckdb_api::PredicateDecisionReason::NO_REMOTE_CANDIDATE &&
	            fallback.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "ineligible non-fallback operation did not yield the sole unrestricted fallback");

	auto unavailable_request = BuildVisibilityCandidateRequest(fallback_connector, fallback_relation);
	unavailable_request.capabilities.retains_predicate = false;
	const auto unavailable = duckdb_api::BuildConservativeScanPlan(fallback_connector, unavailable_request);
	Require(unavailable.Operation().Rest().operation_name == "controlled_selector_fallback_repositories" &&
	            unavailable.PredicateCategory() == duckdb_api::PredicateDecisionCategory::UNSUPPORTED &&
	            unavailable.PredicateReason() == duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE &&
	            unavailable.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "unavailable predicate-retention capability supplied a selector binding or bypassed the fallback");
}

void TestReferenceRequirementMatrix() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &anonymous = FindRelation(connector, "duckdb_login_search_page");
	const auto &authenticated = FindRelation(connector, "authenticated_user");

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
	const auto &anonymous = FindRelation(connector, "duckdb_login_search_page");
	const auto &authenticated = FindRelation(connector, "authenticated_user");

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
	const auto &relation = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto valid = BuildAnonymousScanRequest(connector, relation.Name());

	auto request = valid;
	request.explicit_inputs = duckdb_api::ExplicitInputs({duckdb_api::ExplicitInput::Varchar("secret", "selector")});
	RequireRequestRejected(connector, request, "a logical selector encoded as an explicit input");
	request = valid;
	request.projected_columns.pop_back();
	RequireRequestRejected(connector, request, "an incomplete projection closure");
	request = valid;
	request.projected_columns[0] = relation.Columns().back().name;
	RequireRequestRejected(connector, request, "a mismatched selected schema");
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
	const auto &anonymous = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto &authenticated = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION);
	const auto many =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	const auto one = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, authenticated.Name(), "cardinality_secret"));

	Require(many.Operation().Rest().cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            many.Operation().Rest().response_source == duckdb_api::PlannedResponseSource::JSON_PATH_MANY &&
	            many.Domain() == duckdb_api::BaseDomain::JSON_PATH_RECORDS && many.Budgets().decoded_records == 4,
	        "generic multi-record source retained the native three-row domain");
	Require(one.Operation().Rest().cardinality == duckdb_api::PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	            one.Operation().Rest().response_source == duckdb_api::PlannedResponseSource::ROOT_OBJECT &&
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
	const auto &anonymous = FindRelation(connector, "duckdb_login_search_page");
	const auto plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	Require(plan.Operation().Rest().query_parameters.size() ==
	                anonymous.Operation().Rest().request.query_parameters.size() &&
	            !plan.Operation().Rest().query_parameters.empty(),
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
	const auto &anonymous = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	Require(plan.Budgets().response_bytes == connector.NetworkPolicy().max_response_bytes &&
	            plan.Budgets().decoded_records == anonymous.ResourceCeilings().MaxRecordsPerPage() &&
	            plan.Budgets().extracted_string_bytes == anonymous.ResourceCeilings().MaxExtractedStringBytes(),
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
	invalid.request_attempts = duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP + 1;
	Require(!invalid.IsWithinLiveRestBounds(), "resource envelope exceeded the hard retry-attempt ceiling");
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

void TestPaginationRequiresExplicitSupportedProfile() {
	const auto connector = duckdb_api_test::BuildPaginationConnectorCatalogFixture();
	const auto &decoy = FindRelation(connector, duckdb_api_test::PAGINATION_DECOY_RELATION);
	const auto &linked = FindRelation(connector, duckdb_api_test::PAGINATION_LINK_RELATION);
	const auto decoy_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, decoy.Name(), "pagination_secret"));
	const auto linked_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, linked.Name(), "pagination_secret"));
	Require(decoy_plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED &&
	            decoy_plan.Domain() == duckdb_api::BaseDomain::JSON_PATH_RECORDS,
	        "planner inferred pagination from page-shaped request fields");
	Require(linked_plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::LINK_HEADER &&
	            linked_plan.Domain() == duckdb_api::BaseDomain::PAGINATED_JSON_PATH_RECORDS &&
	            linked_plan.Pagination().ScanBudgets().pages == 4,
	        "planner ignored the explicit supported Link profile");
	const auto disabled_root_array = BuildDisabledRootArrayRepositoryCandidate();
	const auto &disabled_root_array_relation = FindRelation(disabled_root_array, "authenticated_repositories");
	RequireRequestRejected(
	    disabled_root_array,
	    BuildAuthenticatedScanRequest(disabled_root_array, disabled_root_array_relation.Name(), "pagination_secret"),
	    "a legacy native repository root array without its controlled completeness proof");

	const auto too_many_pages = BuildPaginationPlannerCandidate(33, 1024, 33 * 1024, 3, 99, 96);
	const auto &too_many_relation = FindRelation(too_many_pages, "planner_pagination_candidate");
	RequireRequestRejected(
	    too_many_pages, BuildAuthenticatedScanRequest(too_many_pages, too_many_relation.Name(), "pagination_secret"),
	    "a pagination profile wider than the 32-page scan envelope instead of rejecting page-one fallback");

	const auto too_many_records = BuildPaginationPlannerCandidate(4, 1024, 4096, 101, 404, 96);
	const auto &too_many_records_relation = FindRelation(too_many_records, "planner_pagination_candidate");
	const auto too_many_records_plan = duckdb_api::BuildConservativeScanPlan(
	    too_many_records,
	    BuildAuthenticatedScanRequest(too_many_records, too_many_records_relation.Name(), "pagination_secret"));
	Require(too_many_records_plan.Pagination().PageBudgets().decoded_records ==
	                duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE &&
	            too_many_records_plan.Pagination().ScanBudgets().decoded_records == 404,
	        "REST author record declarations were rejected or failed to intersect with host policy");

	const auto too_many_response_bytes =
	    BuildPaginationPlannerCandidate(9, duckdb_api::PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE,
	                                    9 * duckdb_api::PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE, 3, 27, 96);
	const auto &too_many_response_bytes_relation =
	    FindRelation(too_many_response_bytes, "planner_pagination_candidate");
	const auto too_many_response_bytes_plan = duckdb_api::BuildConservativeScanPlan(
	    too_many_response_bytes,
	    BuildAuthenticatedScanRequest(too_many_response_bytes, too_many_response_bytes_relation.Name(),
	                                  "pagination_secret"));
	Require(too_many_response_bytes_plan.Pagination().PageBudgets().response_bytes ==
	                duckdb_api::PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE &&
	            too_many_response_bytes_plan.Pagination().ScanBudgets().response_bytes ==
	                duckdb_api::PAGINATION_MAX_RESPONSE_BYTES_PER_SCAN,
	        "REST author response declarations were rejected or failed to intersect with host policy");

	const auto too_wide_strings = BuildPaginationPlannerCandidate(4, 1024, 4096, 3, 12, 513);
	const auto &too_wide_strings_relation = FindRelation(too_wide_strings, "planner_pagination_candidate");
	const auto too_wide_strings_plan = duckdb_api::BuildConservativeScanPlan(
	    too_wide_strings,
	    BuildAuthenticatedScanRequest(too_wide_strings, too_wide_strings_relation.Name(), "pagination_secret"));
	Require(too_wide_strings_plan.Pagination().PageBudgets().extracted_string_bytes ==
	                duckdb_api::PAGINATION_MAX_EXTRACTED_STRING_BYTES &&
	            too_wide_strings_plan.Pagination().ScanBudgets().extracted_string_bytes ==
	                duckdb_api::PAGINATION_MAX_EXTRACTED_STRING_BYTES,
	        "REST author string declaration was rejected or failed to intersect with host policy");
}

void TestPackageAcceptsUnpaginatedRootArray() {
	const auto generation = duckdb_api_test::CompileNonGithubGraphqlGenerationFixture(DUCKDB_API_SOURCE_ROOT);
	const auto &relation = FindRelation(generation.Connector(), "public_announcements");
	Require(relation.PredicateMappings().empty(),
	        "compiler-produced root-array fixture unexpectedly gained a predicate proof mapping");
	const duckdb_api::PackageBoundScanPlanningService planning(generation);
	const auto plan = planning.Plan(generation.QueryRegistration().GenerationHandle(),
	                                BuildAnonymousScanRequest(generation.Connector(), relation.Name()));
	const auto &operation = plan.Operation().Rest();
	Require(plan.ConnectorName() == "acme_events" && plan.RelationName() == "public_announcements" &&
	            plan.Domain() == duckdb_api::BaseDomain::ROOT_ARRAY_RECORDS &&
	            plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED &&
	            operation.response_source == duckdb_api::PlannedResponseSource::ROOT_ARRAY &&
	            operation.origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            operation.origin.host == "api.example.com" && operation.origin.port == 8443 &&
	            operation.path == "/v1/public-announcements",
	        "valid non-GitHub package root array did not compile and plan from its declared authority");
}

} // namespace

void RunPredicatePlannerTests();
void RunRelationalPredicateTests();

int main() {
	try {
		RunRelationalPredicateTests();
		RunPredicatePlannerTests();
		RunPredicateCompositionLawTests();
		RunInputResolutionLawTests();
		RunOperationSelectionLawTests();
		RunPackageBoundScanPlannerTests();
		RunPermanentRestScanPlanFixtureTests();
		RunRuntimeRestPredicatePlanFixtureTests();
		TestExactSelectionHasNoFallback();
		TestCandidateSpecificOperationSelectionAndFallback();
		TestEqualRankedOperationSelectionFailsBeforePlanConstruction();
		TestReferenceRequirementMatrix();
		TestSecretManagerCapabilityIsRequirementScoped();
		TestUnavailableRelationalCounterexamples();
		TestResponseSourceCardinalityAndLimitAreIndependent();
		TestFixedSourceInputsRemainNonRelational();
		TestResourceEnvelopeBounds();
		TestPaginationRequiresExplicitSupportedProfile();
		TestPackageAcceptsUnpaginatedRootArray();
		std::cout << "scan planner tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan planner tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
