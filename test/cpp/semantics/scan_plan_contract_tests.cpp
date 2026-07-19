#include "duckdb_api/scan_plan.hpp"
#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "query/support/live_scan_request.hpp"
#include "support/require.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api_test::BuildAnonymousScanRequest;
using duckdb_api_test::BuildAuthenticatedScanRequest;
using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelation;
using duckdb_api_test::scan_plan_contract::GroupedDigits;
using duckdb_api_test::scan_plan_contract::RuntimeCredentialCanary;
using duckdb_api_test::scan_plan_contract::ScopedEnvironment;

static_assert(std::is_copy_constructible<duckdb_api::ScanPlan>::value,
              "immutable ScanPlan must support prepared-state copies");
static_assert(std::is_move_constructible<duckdb_api::ScanPlan>::value,
              "immutable ScanPlan must support ownership transfer");
static_assert(!std::is_copy_assignable<duckdb_api::ScanPlan>::value,
              "ScanPlan assignment would permit post-construction replacement");
static_assert(!std::is_move_assignable<duckdb_api::ScanPlan>::value,
              "ScanPlan move assignment would permit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::ScanPlan>::value,
              "only Relational Semantics may construct a ScanPlan");
static_assert(std::is_copy_constructible<duckdb_api::PlannedAuthenticationObligation>::value,
              "plan authorization obligation must follow immutable plan copies");
static_assert(!std::is_copy_assignable<duckdb_api::PlannedAuthenticationObligation>::value,
              "authorization obligation must not admit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::PlannedAuthenticationObligation>::value,
              "consumers must not construct partial authorization obligations");
static_assert(
    std::is_same<decltype(duckdb_api::PlannedRestOperation::response_source), duckdb_api::PlannedResponseSource>::value,
    "response source must remain a typed Runtime handoff");

void RequireColumnsMatch(const duckdb_api::ScanPlan &plan, const duckdb_api::CompiledRelation &relation) {
	Require(plan.OutputColumns().size() == relation.Columns().size(), "plan did not preserve the full selected schema");
	for (std::size_t index = 0; index < relation.Columns().size(); index++) {
		const auto &planned = plan.OutputColumns()[index];
		const auto &compiled = relation.Columns()[index];
		Require(planned.name == compiled.name && planned.logical_type == compiled.logical_type &&
		            planned.nullable == compiled.nullable && planned.extractor == compiled.extractor,
		        "plan output column drifted from selected relation metadata");
	}
}

void RequireOperationMatches(const duckdb_api::ScanPlan &plan, const duckdb_api::CompiledRelation &relation) {
	const auto &planned = plan.Operation();
	const auto &compiled = relation.Operation();
	Require(planned.operation_name == compiled.name && planned.protocol == duckdb_api::PlannedProtocol::REST &&
	            planned.method == duckdb_api::PlannedHttpMethod::GET &&
	            planned.replay_safety == duckdb_api::PlannedReplaySafety::SAFE,
	        "plan operation identity or closed protocol facts drifted");
	const auto expected_scheme = compiled.request.origin.scheme == duckdb_api::CompiledUrlScheme::HTTPS
	                                 ? duckdb_api::PlannedUrlScheme::HTTPS
	                                 : duckdb_api::PlannedUrlScheme::HTTP;
	Require(planned.origin.scheme == expected_scheme && planned.origin.host == compiled.request.origin.host.Value() &&
	            planned.origin.port == compiled.request.origin.port,
	        "plan operation lost its exact typed origin");
	Require(planned.path == compiled.request.path && planned.records_extractor == compiled.records_extractor,
	        "plan operation path or extraction drifted");
	Require(planned.query_parameters.size() == compiled.request.query_parameters.size() &&
	            planned.headers.size() == compiled.request.headers.size(),
	        "plan operation lost structural request fields");
	for (std::size_t index = 0; index < compiled.request.query_parameters.size(); index++) {
		Require(planned.query_parameters[index].name == compiled.request.query_parameters[index].name &&
		            planned.query_parameters[index].encoded_value ==
		                compiled.request.query_parameters[index].encoded_value,
		        "plan operation changed a fixed query field");
	}
	for (std::size_t index = 0; index < compiled.request.headers.size(); index++) {
		Require(planned.headers[index].name == compiled.request.headers[index].name &&
		            planned.headers[index].value == compiled.request.headers[index].value,
		        "plan operation changed a fixed non-sensitive header");
	}
}

void RequireDuckDbRelationalOwnership(const duckdb_api::ScanPlan &plan) {
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        "plan changed the conservative remote/residual predicate contract");
	Require(plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB,
	        "plan moved a relational operator out of DuckDB");
	Require(plan.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOffset() == duckdb_api::RelationalDelegation::NONE,
	        "plan introduced remote/runtime ordering or bound delegation");
	Require(plan.Providers() == duckdb_api::FeatureState::DISABLED &&
	            plan.Retry() == duckdb_api::FeatureState::DISABLED &&
	            plan.Cache() == duckdb_api::FeatureState::DISABLED,
	        "plan enabled an excluded provider, retry, or cache feature");
}

void RequireDisabledPaginationPayloadInaccessible(const duckdb_api::PaginationPlan &pagination) {
	Require(pagination.Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED,
	        "disabled pagination changed its discriminant");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.Dependency(); }, "disabled pagination exposed dependency payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.Consistency(); }, "disabled pagination exposed consistency payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.LinkRelation(); }, "disabled pagination exposed Link-relation payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.TargetScope(); }, "disabled pagination exposed target-scope payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.SupportsTotal(); }, "disabled pagination exposed total payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.SupportsResume(); }, "disabled pagination exposed resume payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>([&pagination]() { (void)pagination.Target(); },
	                                                                     "disabled pagination exposed target payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.PageBudgets(); }, "disabled pagination exposed page-budget payload");
	duckdb_api_test::scan_plan_contract::RequireThrows<std::logic_error>(
	    [&pagination]() { (void)pagination.ScanBudgets(); }, "disabled pagination exposed scan-budget payload");
}

void RequireSelectedNetworkAndBudgets(const duckdb_api::ScanPlan &plan, const duckdb_api::CompiledRelation &relation) {
	const auto expected_scheme =
	    relation.Operation().request.origin.scheme == duckdb_api::CompiledUrlScheme::HTTPS ? "https" : "http";
	Require(plan.Network().allowed_schemes.size() == 1 && plan.Network().allowed_hosts.size() == 1 &&
	            plan.Network().allowed_schemes[0] == expected_scheme &&
	            plan.Network().allowed_hosts[0] == relation.Operation().request.origin.host.Value() &&
	            !plan.Network().redirects_enabled && !plan.Network().private_addresses_enabled &&
	            !plan.Network().link_local_addresses_enabled,
	        "plan network capability was not narrowed to the selected operation");
	Require(plan.Budgets().IsWithinLiveRestBounds() &&
	            plan.Budgets().decoded_records ==
	                std::min(relation.ResourceCeilings().MaxRecordsPerPage(), duckdb_api::HOST_MAX_DECODED_RECORDS) &&
	            plan.Budgets().extracted_string_bytes == std::min(relation.ResourceCeilings().MaxExtractedStringBytes(),
	                                                              duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES),
	        "plan resource envelope did not intersect relation and host ceilings");
}

void RequireAnonymousPlan(const duckdb_api::ScanPlan &plan, const duckdb_api::CompiledConnector &connector,
                          const duckdb_api::CompiledRelation &relation) {
	Require(plan.ConnectorName() == connector.ConnectorName() && plan.ConnectorVersion() == connector.Version() &&
	            plan.RelationName() == relation.Name() && plan.SourceSnapshot() == relation.Snapshot(),
	        "anonymous plan identity or selected provenance drifted");
	Require(plan.Domain() == duckdb_api::BaseDomain::JSON_PATH_RECORDS &&
	            plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED &&
	            plan.Operation().cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            plan.Operation().response_source == duckdb_api::PlannedResponseSource::JSON_PATH_MANY,
	        "anonymous plan lost its multi-record source semantics");
	Require(!plan.SecretReference().IsPresent() && plan.Authentication() == duckdb_api::FeatureState::DISABLED,
	        "anonymous plan retained a secret reference or enabled authentication");
	const auto &obligation = plan.AuthenticationObligation();
	Require(obligation.Requirement() == duckdb_api::PlannedCredentialRequirement::NONE &&
	            obligation.LogicalCredential().empty() &&
	            obligation.Authenticator() == duckdb_api::PlannedAuthenticator::NONE &&
	            obligation.Placement() == duckdb_api::PlannedCredentialPlacement::NONE &&
	            obligation.Destination() == nullptr,
	        "anonymous plan did not retain the closed no-auth obligation");
	RequireColumnsMatch(plan, relation);
	RequireOperationMatches(plan, relation);
	RequireDisabledPaginationPayloadInaccessible(plan.Pagination());
	RequireDuckDbRelationalOwnership(plan);
	RequireSelectedNetworkAndBudgets(plan, relation);
}

void RequireAuthenticatedPlan(const duckdb_api::ScanPlan &plan, const duckdb_api::CompiledConnector &connector,
                              const duckdb_api::CompiledRelation &relation, const std::string &secret_name) {
	Require(plan.ConnectorName() == connector.ConnectorName() && plan.ConnectorVersion() == connector.Version() &&
	            plan.RelationName() == relation.Name() && plan.SourceSnapshot() == relation.Snapshot(),
	        "authenticated plan identity or selected provenance drifted");
	Require(plan.Domain() == duckdb_api::BaseDomain::SUCCESSFUL_ROOT_OBJECT &&
	            plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED &&
	            plan.Operation().cardinality == duckdb_api::PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	            plan.Operation().response_source == duckdb_api::PlannedResponseSource::ROOT_OBJECT,
	        "authenticated plan lost its exactly-one root-object semantics");
	Require(plan.SecretReference().IsPresent() && plan.SecretReference().Name() == secret_name &&
	            plan.Authentication() == duckdb_api::FeatureState::ENABLED,
	        "authenticated plan lost its exact logical reference or feature state");
	const auto &obligation = plan.AuthenticationObligation();
	const auto *destination = obligation.Destination();
	Require(obligation.Requirement() == duckdb_api::PlannedCredentialRequirement::REQUIRED &&
	            obligation.LogicalCredential() == relation.Authentication().LogicalCredential() &&
	            obligation.Authenticator() == duckdb_api::PlannedAuthenticator::BEARER &&
	            obligation.Placement() == duckdb_api::PlannedCredentialPlacement::AUTHORIZATION_HEADER &&
	            destination != nullptr && destination->scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            plan.Operation().origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            plan.Network().allowed_schemes == std::vector<std::string>({"https"}) &&
	            destination->host == plan.Operation().origin.host &&
	            destination->port == plan.Operation().origin.port &&
	            destination->scheme == plan.Operation().origin.scheme,
	        "authenticated plan did not normalize the exact bearer destination obligation");
	Require(plan.Budgets().decoded_records == 1 && plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
	        "exactly-one cardinality or its distinct record budget was reclassified as LIMIT 1");
	RequireColumnsMatch(plan, relation);
	RequireOperationMatches(plan, relation);
	RequireDisabledPaginationPayloadInaccessible(plan.Pagination());
	RequireDuckDbRelationalOwnership(plan);
	RequireSelectedNetworkAndBudgets(plan, relation);
}

void RequireCanaryAbsent(const duckdb_api::ScanPlan &plan, const std::string &canary) {
	Require(plan.Snapshot().find(canary) == std::string::npos &&
	            plan.SourceSnapshot().find(canary) == std::string::npos &&
	            plan.ConnectorName().find(canary) == std::string::npos &&
	            plan.ConnectorVersion().find(canary) == std::string::npos &&
	            plan.RelationName().find(canary) == std::string::npos &&
	            plan.ClassificationReason().find(canary) == std::string::npos &&
	            plan.AuthenticationObligation().LogicalCredential().find(canary) == std::string::npos,
	        "credential canary entered scalar plan state or explanation");
	for (const auto &column : plan.OutputColumns()) {
		Require(column.name.find(canary) == std::string::npos &&
		            column.logical_type.find(canary) == std::string::npos &&
		            column.extractor.find(canary) == std::string::npos,
		        "credential canary entered plan schema");
	}
	for (const auto &header : plan.Operation().headers) {
		Require(header.name.find(canary) == std::string::npos && header.value.find(canary) == std::string::npos,
		        "credential canary entered a planned fixed header");
	}
}

void TestNativeGoldenPlans() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &anonymous = FindRelation(connector, "duckdb_login_search_page");
	const auto &authenticated = FindRelation(connector, "authenticated_user");
	const auto anonymous_plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	const auto authenticated_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, authenticated.Name(), "github_default"));

	RequireAnonymousPlan(anonymous_plan, connector, anonymous);
	RequireAuthenticatedPlan(authenticated_plan, connector, authenticated, "github_default");
	Require(anonymous_plan.ConnectorName() == "github" && anonymous_plan.ConnectorVersion() == "0.5.0" &&
	            anonymous_plan.RelationName() == "duckdb_login_search_page" &&
	            anonymous_plan.Operation().operation_name == "github_search_duckdb_login_page" &&
	            anonymous_plan.Operation().path == "/search/users" &&
	            anonymous_plan.Operation().query_parameters.size() == 2 &&
	            anonymous_plan.Operation().query_parameters[0].name == "q" &&
	            anonymous_plan.Operation().query_parameters[0].encoded_value == "duckdb+in%3Alogin" &&
	            anonymous_plan.Operation().query_parameters[1].name == "per_page" &&
	            anonymous_plan.Operation().query_parameters[1].encoded_value == "3" &&
	            authenticated_plan.RelationName() == "authenticated_user" &&
	            authenticated_plan.Operation().operation_name == "github_authenticated_user" &&
	            authenticated_plan.Operation().path == "/user" &&
	            authenticated_plan.Operation().query_parameters.empty() &&
	            anonymous_plan.Operation().headers.size() == 3 &&
	            anonymous_plan.Operation().headers[1].value == "duckdb-api/0.5.0" &&
	            authenticated_plan.Operation().headers.size() == 3 &&
	            authenticated_plan.Operation().headers[1].value == "duckdb-api/0.5.0",
	        "existing native plan identity or fixed request changed during pagination planning work");
	Require(anonymous_plan.OutputColumns().size() == 3 && anonymous_plan.OutputColumns()[0].name == "id" &&
	            anonymous_plan.OutputColumns()[0].logical_type == "BIGINT" &&
	            anonymous_plan.OutputColumns()[1].name == "login" &&
	            anonymous_plan.OutputColumns()[1].logical_type == "VARCHAR" &&
	            anonymous_plan.OutputColumns()[2].name == "site_admin" &&
	            anonymous_plan.OutputColumns()[2].logical_type == "BOOLEAN" &&
	            authenticated_plan.OutputColumns().size() == anonymous_plan.OutputColumns().size(),
	        "existing native plan schema changed during pagination planning work");
	for (const auto *plan : {&anonymous_plan, &authenticated_plan}) {
		const auto expected_records = plan == &anonymous_plan ? 3U : 1U;
		Require(plan->Budgets().request_attempts == 1 && plan->Budgets().response_bytes == 65536 &&
		            plan->Budgets().header_bytes == 16384 && plan->Budgets().decompressed_bytes == 65536 &&
		            plan->Budgets().decoded_records == expected_records &&
		            plan->Budgets().extracted_string_bytes == 256 && plan->Budgets().json_nesting == 16 &&
		            plan->Budgets().decoded_memory_bytes == 131072 && plan->Budgets().batch_rows == 2 &&
		            plan->Budgets().wall_milliseconds == 5000 && plan->Budgets().concurrency == 1,
		        "existing native plan resource envelope changed during pagination planning work");
	}
	Require(anonymous_plan.Snapshot().find("domain=json_path_records") != std::string::npos &&
	            anonymous_plan.Snapshot().find("auth-obligation=requirement:none") != std::string::npos,
	        "anonymous golden explanation omitted domain or closed auth state");
	Require(authenticated_plan.Snapshot().find("exactly_one_on_success") != std::string::npos &&
	            authenticated_plan.Snapshot().find("response=source:root_object") != std::string::npos &&
	            authenticated_plan.Snapshot().find("logical_credential:token,authenticator:bearer") !=
	                std::string::npos &&
	            authenticated_plan.Snapshot().find("remote_limit:none") != std::string::npos,
	        "authenticated golden explanation omitted cardinality, response, obligation, or limit ownership");
	Require(authenticated_plan.Snapshot().find("github_default") == std::string::npos &&
	            authenticated_plan.Snapshot().find(authenticated_plan.SecretReference().Snapshot()) !=
	                std::string::npos,
	        "logical reference explanation bypassed Query's safe encoding");
}

void TestProviderOwnedDistinctSchemaPlans() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto &anonymous = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto &authenticated = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION);
	Require(anonymous.Columns().size() != authenticated.Columns().size() ||
	            anonymous.Columns()[0].name != authenticated.Columns()[0].name,
	        "provider fixture does not prove distinct schema selection");

	const auto anonymous_plan =
	    duckdb_api::BuildConservativeScanPlan(connector, BuildAnonymousScanRequest(connector, anonymous.Name()));
	const auto authenticated_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, authenticated.Name(), "fixture_named_secret"));
	RequireAnonymousPlan(anonymous_plan, connector, anonymous);
	RequireAuthenticatedPlan(authenticated_plan, connector, authenticated, "fixture_named_secret");
	Require(anonymous_plan.Budgets().decoded_records == 4,
	        "planner retained the native anonymous three-record ceiling in generic provider planning");
	Require(anonymous_plan.Operation().path != authenticated_plan.Operation().path &&
	            anonymous_plan.OutputColumns()[0].name != authenticated_plan.OutputColumns()[0].name,
	        "planner selected a native or cross-relation fallback instead of provider metadata");
}

void TestReferenceIdentityChangesOnlyReferenceAndExplanation() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto &relation = FindRelation(connector, duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION);
	const auto first = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation.Name(), "named_secret_a"));
	const auto second = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, relation.Name(), "named_secret_b"));

	Require(first.RelationName() == second.RelationName() && first.SourceSnapshot() == second.SourceSnapshot() &&
	            first.Domain() == second.Domain() &&
	            first.Operation().operation_name == second.Operation().operation_name &&
	            first.Operation().cardinality == second.Operation().cardinality &&
	            first.Operation().response_source == second.Operation().response_source &&
	            first.OutputColumns().size() == second.OutputColumns().size() &&
	            first.Budgets().decoded_records == second.Budgets().decoded_records &&
	            first.AuthenticationObligation().LogicalCredential() ==
	                second.AuthenticationObligation().LogicalCredential(),
	        "logical secret identity changed selected relational or authorization policy facts");
	std::string expected = first.Snapshot();
	const auto first_reference = first.SecretReference().Snapshot();
	const auto position = expected.find(first_reference);
	Require(position != std::string::npos, "first plan explanation omitted its logical reference");
	expected.replace(position, first_reference.size(), second.SecretReference().Snapshot());
	Require(expected == second.Snapshot(), "changing only the logical secret name changed more than plan explanation");
}

void TestDeterministicCopyAndRedactedEnvironment() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(connector, "authenticated_user");
	const auto request = BuildAuthenticatedScanRequest(connector, relation.Name(), "prepared_secret");
	const auto baseline = duckdb_api::BuildConservativeScanPlan(connector, request);
	const auto copy = baseline;
	Require(copy.Snapshot() == baseline.Snapshot() && copy.SecretReference().Name() == "prepared_secret",
	        "copy construction changed immutable plan or prepared reference meaning");

	const auto canary = RuntimeCredentialCanary();
	ScopedEnvironment hostile;
	hostile.Set("DUCKDB_API_TOKEN", canary);
	hostile.Set("DUCKDB_API_SECRET", canary);
	hostile.Set("GITHUB_TOKEN", canary);
	hostile.Set("HTTP_PROXY", canary);
	hostile.Set("HTTPS_PROXY", canary);
	hostile.Set("HOME", canary);
	const auto hostile_plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	Require(hostile_plan.Snapshot() == baseline.Snapshot(), "planner depends on ambient credential environment");
	RequireCanaryAbsent(hostile_plan, canary);
	auto rejected_request = request;
	rejected_request.capabilities.secret_manager = false;
	bool rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(connector, rejected_request);
	} catch (const std::logic_error &error) {
		rejected = true;
		Require(std::string(error.what()).find(canary) == std::string::npos,
		        "credential canary entered a planner rejection diagnostic");
	}
	Require(rejected, "hostile-environment diagnostic oracle did not exercise a planner rejection");

	const std::locale original = std::locale();
	std::string localized_snapshot;
	try {
		std::locale::global(std::locale(std::locale::classic(), new GroupedDigits()));
		localized_snapshot = hostile_plan.Snapshot();
		std::locale::global(original);
	} catch (...) {
		std::locale::global(original);
		throw;
	}
	Require(localized_snapshot == baseline.Snapshot(), "plan explanation depends on process-global locale");
}

} // namespace

int main() {
	try {
		TestNativeGoldenPlans();
		TestProviderOwnedDistinctSchemaPlans();
		TestReferenceIdentityChangesOnlyReferenceAndExplanation();
		TestDeterministicCopyAndRedactedEnvironment();
		std::cout << "scan plan contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan plan contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
