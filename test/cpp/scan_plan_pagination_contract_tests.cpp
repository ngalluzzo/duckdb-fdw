#include "duckdb_api/scan_plan.hpp"
#include "support/connector_catalog_test_fixtures.hpp"
#include "support/live_scan_request.hpp"
#include "support/require.hpp"
#include "support/scan_plan_contract_test_support.hpp"
#include "support/scan_plan_test_fixtures.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api_test::BuildAuthenticatedScanRequest;
using duckdb_api_test::PaginationPlanCounterexample;
using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelation;
using duckdb_api_test::scan_plan_contract::RequireThrows;

static_assert(std::is_copy_constructible<duckdb_api::PaginationPlan>::value,
              "immutable PaginationPlan must follow prepared-state copies");
static_assert(!std::is_copy_assignable<duckdb_api::PaginationPlan>::value,
              "PaginationPlan assignment would permit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::PaginationPlan>::value,
              "consumers must not construct partial pagination plans");

void RequireDuckDbRelationalOwnership(const duckdb_api::ScanPlan &plan) {
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOffset() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Providers() == duckdb_api::FeatureState::DISABLED &&
	            plan.Retry() == duckdb_api::FeatureState::DISABLED &&
	            plan.Cache() == duckdb_api::FeatureState::DISABLED,
	        "pagination moved a relational operator or enabled an excluded execution feature");
}

void RequireDisabledPayloadInaccessible(const duckdb_api::PaginationPlan &pagination) {
	Require(pagination.Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED,
	        "disabled pagination changed its discriminant");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.Dependency(); },
	                                "disabled pagination exposed dependency payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.Consistency(); },
	                                "disabled pagination exposed consistency payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.LinkRelation(); },
	                                "disabled pagination exposed Link-relation payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.TargetScope(); },
	                                "disabled pagination exposed target-scope payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.SupportsTotal(); },
	                                "disabled pagination exposed total payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.SupportsResume(); },
	                                "disabled pagination exposed resume payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.Target(); },
	                                "disabled pagination exposed target payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.PageBudgets(); },
	                                "disabled pagination exposed page-budget payload");
	RequireThrows<std::logic_error>([&pagination]() { (void)pagination.ScanBudgets(); },
	                                "disabled pagination exposed scan-budget payload");
}

bool SameTarget(const duckdb_api::PlannedPaginationTarget &left, const duckdb_api::PlannedPaginationTarget &right) {
	return left.origin.scheme == right.origin.scheme && left.origin.host == right.origin.host &&
	       left.origin.port == right.origin.port && left.path == right.path &&
	       left.page_size_parameter == right.page_size_parameter && left.page_size == right.page_size &&
	       left.page_number_parameter == right.page_number_parameter && left.first_page == right.first_page &&
	       left.page_increment == right.page_increment;
}

bool SamePageBudgets(const duckdb_api::ResourceBudgets &left, const duckdb_api::ResourceBudgets &right) {
	return left.request_attempts == right.request_attempts && left.response_bytes == right.response_bytes &&
	       left.header_bytes == right.header_bytes && left.decompressed_bytes == right.decompressed_bytes &&
	       left.decoded_records == right.decoded_records &&
	       left.extracted_string_bytes == right.extracted_string_bytes && left.json_nesting == right.json_nesting &&
	       left.decoded_memory_bytes == right.decoded_memory_bytes && left.batch_rows == right.batch_rows &&
	       left.wall_milliseconds == right.wall_milliseconds && left.concurrency == right.concurrency;
}

bool SameScanBudgets(const duckdb_api::ScanResourceBudgets &left, const duckdb_api::ScanResourceBudgets &right) {
	return left.request_attempts == right.request_attempts && left.pages == right.pages &&
	       left.response_bytes == right.response_bytes && left.header_bytes == right.header_bytes &&
	       left.decompressed_bytes == right.decompressed_bytes && left.decoded_records == right.decoded_records &&
	       left.extracted_string_bytes == right.extracted_string_bytes && left.json_nesting == right.json_nesting &&
	       left.decoded_memory_bytes == right.decoded_memory_bytes && left.batch_rows == right.batch_rows &&
	       left.wall_milliseconds == right.wall_milliseconds && left.concurrency == right.concurrency;
}

void TestExplicitMetadataDefinesOneBag() {
	const auto connector = duckdb_api_test::BuildPaginationConnectorCatalogFixture();
	const auto &decoy = FindRelation(connector, duckdb_api_test::PAGINATION_DECOY_RELATION);
	const auto &linked = FindRelation(connector, duckdb_api_test::PAGINATION_LINK_RELATION);
	const auto decoy_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, decoy.Name(), "pagination_secret"));
	const auto linked_plan = duckdb_api::BuildConservativeScanPlan(
	    connector, BuildAuthenticatedScanRequest(connector, linked.Name(), "pagination_secret"));
	const auto service_plan = duckdb_api_test::BuildValidPaginatedPlanFixture("pagination_secret");
	Require(service_plan.Snapshot() == linked_plan.Snapshot(),
	        "safe pagination service did not return the exact planner-produced plan");
	Require(
	    decoy.Operation().request.query_parameters.size() == 2 &&
	        linked.Operation().request.query_parameters.size() == 2 &&
	        decoy.Operation().request.query_parameters[0].name == linked.Operation().request.query_parameters[0].name &&
	        decoy.Operation().request.query_parameters[1].name == linked.Operation().request.query_parameters[1].name &&
	        decoy_plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED &&
	        decoy_plan.Domain() == duckdb_api::BaseDomain::JSON_PATH_RECORDS,
	    "planner inferred pagination from page-shaped structural query fields");
	RequireDisabledPayloadInaccessible(decoy_plan.Pagination());

	const auto &pagination = linked_plan.Pagination();
	const auto &target = pagination.Target();
	Require(linked_plan.Domain() == duckdb_api::BaseDomain::PAGINATED_JSON_PATH_RECORDS &&
	            pagination.Strategy() == duckdb_api::PlannedPaginationStrategy::LINK_HEADER &&
	            pagination.Dependency() == duckdb_api::PlannedPageDependency::SEQUENTIAL &&
	            pagination.Consistency() == duckdb_api::PlannedPageConsistency::MUTABLE &&
	            pagination.LinkRelation() == duckdb_api::PlannedLinkRelation::NEXT &&
	            pagination.TargetScope() ==
	                duckdb_api::PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH &&
	            !pagination.SupportsTotal() && !pagination.SupportsResume() &&
	            target.path == linked_plan.Operation().path && target.page_size_parameter == "batch_size" &&
	            target.page_size == 3 && target.page_number_parameter == "cursor_page" && target.first_page == 1 &&
	            target.page_increment == 1,
	        "explicit Link declaration lost its closed relational transition profile");
	Require(pagination.PageBudgets().IsWithinPaginatedPageBounds() &&
	            pagination.ScanBudgets().IsWithinPaginatedScanBounds() && pagination.ScanBudgets().pages == 4 &&
	            pagination.PageBudgets().decoded_records == 3 && pagination.ScanBudgets().decoded_records == 12 &&
	            &linked_plan.Budgets() == &pagination.PageBudgets(),
	        "pagination plan lost explicit page/scan ceilings or the effective page envelope");
	RequireDuckDbRelationalOwnership(linked_plan);
}

void TestAuthenticatedRepositoriesProfile() {
	const auto plan =
	    duckdb_api_test::BuildValidAuthenticatedRepositoriesPlanFixture("authenticated_repositories_secret");
	const auto &operation = plan.Operation();
	const auto &pagination = plan.Pagination();
	const auto &target = pagination.Target();
	Require(plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.5.0" &&
	            plan.RelationName() == "authenticated_repositories" &&
	            operation.operation_name == "github_authenticated_repositories" &&
	            operation.cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            operation.response_source == duckdb_api::PlannedResponseSource::ROOT_ARRAY &&
	            operation.records_extractor == "$" &&
	            plan.Domain() == duckdb_api::BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS &&
	            operation.origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            operation.origin.host == "api.github.com" && operation.origin.port == 443 &&
	            operation.path == "/user/repos" && operation.query_parameters.size() == 2 &&
	            operation.query_parameters[0].name == "per_page" &&
	            operation.query_parameters[0].encoded_value == "100" && operation.query_parameters[1].name == "page" &&
	            operation.query_parameters[1].encoded_value == "1" && operation.headers.size() == 3 &&
	            operation.headers[0].value == "application/vnd.github+json" &&
	            operation.headers[1].value == "duckdb-api/0.5.0" && operation.headers[2].value == "2022-11-28",
	        "authenticated repositories fixture drifted from its exact identity, source, or request");
	const std::vector<std::string> names = {"id", "full_name", "private", "fork", "archived"};
	const std::vector<std::string> types = {"BIGINT", "VARCHAR", "BOOLEAN", "BOOLEAN", "BOOLEAN"};
	Require(plan.OutputColumns().size() == names.size(), "repository fixture lost its five-column schema");
	for (std::size_t index = 0; index < names.size(); index++) {
		Require(plan.OutputColumns()[index].name == names[index] &&
		            plan.OutputColumns()[index].logical_type == types[index] && !plan.OutputColumns()[index].nullable &&
		            plan.OutputColumns()[index].extractor == "$." + names[index],
		        "repository fixture schema order, type, nullability, or extractor drifted");
	}
	Require(pagination.Dependency() == duckdb_api::PlannedPageDependency::SEQUENTIAL &&
	            pagination.Consistency() == duckdb_api::PlannedPageConsistency::MUTABLE &&
	            pagination.LinkRelation() == duckdb_api::PlannedLinkRelation::NEXT && !pagination.SupportsTotal() &&
	            !pagination.SupportsResume() && target.path == "/user/repos" &&
	            target.page_size_parameter == "per_page" && target.page_size == 100 &&
	            target.page_number_parameter == "page" && target.first_page == 1 && target.page_increment == 1,
	        "repository fixture lost its exact sequential Link profile");
	const auto &page = pagination.PageBudgets();
	const auto &scan = pagination.ScanBudgets();
	Require(page.request_attempts == 1 && page.response_bytes == 8388608 && page.header_bytes == 16384 &&
	            page.decompressed_bytes == 8388608 && page.decoded_records == 100 &&
	            page.extracted_string_bytes == 512 && page.json_nesting == 16 && page.decoded_memory_bytes == 2097152 &&
	            page.batch_rows == 64 && page.wall_milliseconds == 30000 && page.concurrency == 1 &&
	            scan.request_attempts == 32 && scan.pages == 32 && scan.response_bytes == 67108864 &&
	            scan.header_bytes == 524288 && scan.decompressed_bytes == 67108864 && scan.decoded_records == 3200 &&
	            scan.extracted_string_bytes == 512 && scan.json_nesting == 16 && scan.decoded_memory_bytes == 2097152 &&
	            scan.batch_rows == 64 && scan.wall_milliseconds == 30000 && scan.concurrency == 1,
	        "repository fixture lost the literal RFC page or scan envelope");
	Require(plan.Authentication() == duckdb_api::FeatureState::ENABLED && plan.SecretReference().IsPresent() &&
	            plan.AuthenticationObligation().Requirement() == duckdb_api::PlannedCredentialRequirement::REQUIRED &&
	            plan.AuthenticationObligation().Authenticator() == duckdb_api::PlannedAuthenticator::BEARER &&
	            plan.Network().allowed_schemes == std::vector<std::string>({"https"}) &&
	            plan.Network().allowed_hosts == std::vector<std::string>({"api.github.com"}) &&
	            !plan.Network().redirects_enabled && !plan.Network().private_addresses_enabled &&
	            !plan.Network().link_local_addresses_enabled && !plan.Network().loopback_addresses_enabled,
	        "repository fixture lost its authorization or exact network capability");
	RequireDuckDbRelationalOwnership(plan);
}

void TestPaginationCounterexamplesAreIsolated() {
	const auto baseline =
	    duckdb_api_test::BuildValidAuthenticatedRepositoriesPlanFixture("pagination_counterexample_secret");
	const std::vector<PaginationPlanCounterexample> variants = {
	    PaginationPlanCounterexample::STRATEGY_DISABLED,
	    PaginationPlanCounterexample::UNKNOWN_DEPENDENCY,
	    PaginationPlanCounterexample::UNKNOWN_CONSISTENCY,
	    PaginationPlanCounterexample::UNKNOWN_LINK_RELATION,
	    PaginationPlanCounterexample::UNKNOWN_TARGET_SCOPE,
	    PaginationPlanCounterexample::SUPPORTS_TOTAL,
	    PaginationPlanCounterexample::SUPPORTS_RESUME,
	    PaginationPlanCounterexample::EMPTY_TARGET_PATH,
	    PaginationPlanCounterexample::PAGE_REQUEST_ATTEMPTS_WIDENED,
	    PaginationPlanCounterexample::SCAN_REQUEST_ATTEMPTS_MISMATCH,
	    PaginationPlanCounterexample::SCAN_RESPONSE_BYTES_BELOW_PAGE,
	    PaginationPlanCounterexample::SCAN_DECODED_RECORDS_BELOW_PAGE};
	for (const auto variant : variants) {
		const auto plan =
		    duckdb_api_test::BuildPaginationPlanCounterexample("pagination_counterexample_secret", variant);
		Require(plan.ConnectorName() == baseline.ConnectorName() && plan.RelationName() == baseline.RelationName() &&
		            plan.SourceSnapshot() == baseline.SourceSnapshot() && plan.Domain() == baseline.Domain() &&
		            plan.Operation().path == baseline.Operation().path &&
		            plan.OutputColumns().size() == baseline.OutputColumns().size() &&
		            plan.SecretReference().Name() == baseline.SecretReference().Name() &&
		            plan.Providers() == baseline.Providers() && plan.Retry() == baseline.Retry() &&
		            plan.Cache() == baseline.Cache(),
		        "pagination counterexample changed a non-pagination plan fact");
		if (variant == PaginationPlanCounterexample::STRATEGY_DISABLED) {
			RequireDisabledPayloadInaccessible(plan.Pagination());
			continue;
		}

		auto expected_target = baseline.Pagination().Target();
		auto expected_page = baseline.Pagination().PageBudgets();
		auto expected_scan = baseline.Pagination().ScanBudgets();
		auto expected_dependency = baseline.Pagination().Dependency();
		auto expected_consistency = baseline.Pagination().Consistency();
		auto expected_relation = baseline.Pagination().LinkRelation();
		auto expected_scope = baseline.Pagination().TargetScope();
		bool expected_total = false;
		bool expected_resume = false;
		switch (variant) {
		case PaginationPlanCounterexample::UNKNOWN_DEPENDENCY:
			expected_dependency = static_cast<duckdb_api::PlannedPageDependency>(255);
			break;
		case PaginationPlanCounterexample::UNKNOWN_CONSISTENCY:
			expected_consistency = static_cast<duckdb_api::PlannedPageConsistency>(255);
			break;
		case PaginationPlanCounterexample::UNKNOWN_LINK_RELATION:
			expected_relation = static_cast<duckdb_api::PlannedLinkRelation>(255);
			break;
		case PaginationPlanCounterexample::UNKNOWN_TARGET_SCOPE:
			expected_scope = static_cast<duckdb_api::PlannedContinuationTargetScope>(255);
			break;
		case PaginationPlanCounterexample::SUPPORTS_TOTAL:
			expected_total = true;
			break;
		case PaginationPlanCounterexample::SUPPORTS_RESUME:
			expected_resume = true;
			break;
		case PaginationPlanCounterexample::EMPTY_TARGET_PATH:
			expected_target.path.clear();
			break;
		case PaginationPlanCounterexample::PAGE_REQUEST_ATTEMPTS_WIDENED:
			expected_page.request_attempts = 2;
			break;
		case PaginationPlanCounterexample::SCAN_REQUEST_ATTEMPTS_MISMATCH:
			expected_scan.request_attempts = expected_scan.pages - 1;
			break;
		case PaginationPlanCounterexample::SCAN_RESPONSE_BYTES_BELOW_PAGE:
			expected_scan.response_bytes = expected_page.response_bytes - 1;
			break;
		case PaginationPlanCounterexample::SCAN_DECODED_RECORDS_BELOW_PAGE:
			expected_scan.decoded_records = expected_page.decoded_records - 1;
			break;
		case PaginationPlanCounterexample::STRATEGY_DISABLED:
			break;
		}
		Require(plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::LINK_HEADER &&
		            plan.Pagination().Dependency() == expected_dependency &&
		            plan.Pagination().Consistency() == expected_consistency &&
		            plan.Pagination().LinkRelation() == expected_relation &&
		            plan.Pagination().TargetScope() == expected_scope &&
		            plan.Pagination().SupportsTotal() == expected_total &&
		            plan.Pagination().SupportsResume() == expected_resume &&
		            SameTarget(plan.Pagination().Target(), expected_target) &&
		            SamePageBudgets(plan.Pagination().PageBudgets(), expected_page) &&
		            SameScanBudgets(plan.Pagination().ScanBudgets(), expected_scan),
		        "pagination counterexample changed more than its one named fact");
	}
	RequireThrows<std::invalid_argument>(
	    []() {
		    (void)duckdb_api_test::BuildPaginationPlanCounterexample("pagination_counterexample_secret",
		                                                             static_cast<PaginationPlanCounterexample>(255));
	    },
	    "pagination fixture accepted an unknown counterexample");
}

} // namespace

int main() {
	try {
		TestExplicitMetadataDefinesOneBag();
		TestAuthenticatedRepositoriesProfile();
		TestPaginationCounterexamplesAreIsolated();
		std::cout << "scan plan pagination contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan plan pagination contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
