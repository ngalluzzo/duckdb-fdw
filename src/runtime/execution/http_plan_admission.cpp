#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"

#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {
namespace {

bool HasUserColumns(const std::vector<PlannedColumn> &columns) {
	return columns.size() == 3 && columns[0].name == "id" && columns[0].logical_type == "BIGINT" &&
	       !columns[0].nullable && columns[0].extractor == "$.id" && columns[1].name == "login" &&
	       columns[1].logical_type == "VARCHAR" && !columns[1].nullable && columns[1].extractor == "$.login" &&
	       columns[2].name == "site_admin" && columns[2].logical_type == "BOOLEAN" && !columns[2].nullable &&
	       columns[2].extractor == "$.site_admin";
}

bool HasRepositoryColumns(const std::vector<PlannedColumn> &columns) {
	return columns.size() == 5 && columns[0].name == "id" && columns[0].logical_type == "BIGINT" &&
	       !columns[0].nullable && columns[0].extractor == "$.id" && columns[1].name == "full_name" &&
	       columns[1].logical_type == "VARCHAR" && !columns[1].nullable && columns[1].extractor == "$.full_name" &&
	       columns[2].name == "private" && columns[2].logical_type == "BOOLEAN" && !columns[2].nullable &&
	       columns[2].extractor == "$.private" && columns[3].name == "fork" && columns[3].logical_type == "BOOLEAN" &&
	       !columns[3].nullable && columns[3].extractor == "$.fork" && columns[4].name == "archived" &&
	       columns[4].logical_type == "BOOLEAN" && !columns[4].nullable && columns[4].extractor == "$.archived";
}

const char *SchemeName(PlannedUrlScheme scheme) {
	switch (scheme) {
	case PlannedUrlScheme::HTTP:
		return "http";
	case PlannedUrlScheme::HTTPS:
		return "https";
	}
	throw ExecutionError(ErrorStage::POLICY, "", "execution profile contains an unknown URL scheme");
}

bool HasFixedHeaders(const std::vector<PlannedHttpHeader> &headers) {
	return headers.size() == 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.5.0" &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool HasCommonOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return operation.protocol == PlannedProtocol::REST && operation.method == PlannedHttpMethod::GET &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.origin.scheme == profile.scheme &&
	       operation.origin.host == profile.host && operation.origin.port == profile.port &&
	       HasFixedHeaders(operation.headers);
}

bool HasExpectedAnonymousOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return HasCommonOperation(operation, profile) && operation.operation_name == "github_search_duckdb_login_page" &&
	       operation.cardinality == PlannedCardinality::ZERO_TO_MANY && operation.path == "/search/users" &&
	       operation.query_parameters.size() == 2 && operation.query_parameters[0].name == "q" &&
	       operation.query_parameters[0].encoded_value == "duckdb+in%3Alogin" &&
	       operation.query_parameters[1].name == "per_page" && operation.query_parameters[1].encoded_value == "3" &&
	       operation.response_source == PlannedResponseSource::JSON_PATH_MANY &&
	       operation.records_extractor == "$.items[*]";
}

bool HasExpectedAuthenticatedOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return HasCommonOperation(operation, profile) && operation.operation_name == "github_authenticated_user" &&
	       operation.cardinality == PlannedCardinality::EXACTLY_ONE_ON_SUCCESS && operation.path == "/user" &&
	       operation.query_parameters.empty() && operation.response_source == PlannedResponseSource::ROOT_OBJECT &&
	       operation.records_extractor == "$";
}

bool HasExpectedRepositoryOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return HasCommonOperation(operation, profile) && operation.cardinality == PlannedCardinality::ZERO_TO_MANY &&
	       operation.operation_name == "github_authenticated_repositories" && operation.path == "/user/repos" &&
	       operation.query_parameters.size() == 2 && operation.query_parameters[0].name == "per_page" &&
	       operation.query_parameters[0].encoded_value == "100" && operation.query_parameters[1].name == "page" &&
	       operation.query_parameters[1].encoded_value == "1" &&
	       operation.response_source == PlannedResponseSource::ROOT_ARRAY && operation.records_extractor == "$";
}

bool HasAnonymousObligation(const PlannedAuthenticationObligation &obligation) {
	return obligation.Requirement() == PlannedCredentialRequirement::NONE && obligation.LogicalCredential().empty() &&
	       obligation.Authenticator() == PlannedAuthenticator::NONE &&
	       obligation.Placement() == PlannedCredentialPlacement::NONE && obligation.Destination() == nullptr;
}

bool HasAuthenticatedObligation(const PlannedAuthenticationObligation &obligation,
                                const HttpExecutionProfile &profile) {
	const auto *destination = obligation.Destination();
	return obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       destination->scheme == profile.scheme && destination->host == profile.host &&
	       destination->port == profile.port;
}

bool HasExpectedNetwork(const NetworkCapability &network, const HttpExecutionProfile &profile) {
	return network.allowed_schemes.size() == 1 && network.allowed_schemes[0] == SchemeName(profile.scheme) &&
	       network.allowed_hosts.size() == 1 && network.allowed_hosts[0] == profile.host &&
	       !network.redirects_enabled && network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool HasExpectedRepositoryNetwork(const NetworkCapability &network, const HttpExecutionProfile &profile) {
	return profile.scheme == PlannedUrlScheme::HTTPS && network.allowed_schemes.size() == 1 &&
	       network.allowed_schemes[0] == "https" && network.allowed_hosts.size() == 1 &&
	       network.allowed_hosts[0] == profile.host && !network.redirects_enabled &&
	       network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool TryAdmitSingleResponsePlan(const ScanPlan &plan, const HttpExecutionProfile &profile,
                                AdmittedHttpOperation &operation) {
	if (plan.ConnectorName() != "github" || plan.ConnectorVersion() != "0.5.0" ||
	    !HasUserColumns(plan.OutputColumns()) || plan.Pagination().Strategy() != PlannedPaginationStrategy::DISABLED ||
	    plan.Providers() != FeatureState::DISABLED || plan.Retry() != FeatureState::DISABLED ||
	    plan.Cache() != FeatureState::DISABLED || !HasExpectedNetwork(plan.Network(), profile) ||
	    !plan.Budgets().IsWithinLiveRestBounds() || plan.Budgets().decoded_records > profile.max_decoded_records) {
		return false;
	}
	if (plan.RelationName() == "duckdb_login_search_page" && plan.Domain() == BaseDomain::JSON_PATH_RECORDS &&
	    HasExpectedAnonymousOperation(plan.Operation(), profile) && plan.Authentication() == FeatureState::DISABLED &&
	    HasAnonymousObligation(plan.AuthenticationObligation()) && !plan.SecretReference().IsPresent()) {
		operation = AdmittedHttpOperation::ANONYMOUS_SEARCH;
		return true;
	}
	if (plan.RelationName() == "authenticated_user" && plan.Domain() == BaseDomain::SUCCESSFUL_ROOT_OBJECT &&
	    HasExpectedAuthenticatedOperation(plan.Operation(), profile) &&
	    plan.Authentication() == FeatureState::ENABLED &&
	    HasAuthenticatedObligation(plan.AuthenticationObligation(), profile) && plan.SecretReference().IsPresent()) {
		operation = AdmittedHttpOperation::AUTHENTICATED_USER;
		return true;
	}
	return false;
}

bool SamePageBudgets(const ResourceBudgets &left, const ResourceBudgets &right) {
	return left.request_attempts == right.request_attempts && left.response_bytes == right.response_bytes &&
	       left.header_bytes == right.header_bytes && left.decompressed_bytes == right.decompressed_bytes &&
	       left.decoded_records == right.decoded_records &&
	       left.extracted_string_bytes == right.extracted_string_bytes && left.json_nesting == right.json_nesting &&
	       left.decoded_memory_bytes == right.decoded_memory_bytes && left.batch_rows == right.batch_rows &&
	       left.wall_milliseconds == right.wall_milliseconds && left.concurrency == right.concurrency;
}

bool HasExpectedPagination(const ScanPlan &plan) {
	const auto &pagination = plan.Pagination();
	if (pagination.Strategy() != PlannedPaginationStrategy::LINK_HEADER ||
	    pagination.Dependency() != PlannedPageDependency::SEQUENTIAL ||
	    pagination.Consistency() != PlannedPageConsistency::MUTABLE ||
	    pagination.LinkRelation() != PlannedLinkRelation::NEXT ||
	    pagination.TargetScope() != PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH ||
	    pagination.SupportsTotal() || pagination.SupportsResume() ||
	    !pagination.PageBudgets().IsWithinPaginatedPageBounds() ||
	    !pagination.ScanBudgets().IsWithinPaginatedScanBounds() ||
	    !SamePageBudgets(plan.Budgets(), pagination.PageBudgets())) {
		return false;
	}
	const auto &target = pagination.Target();
	const auto &page = pagination.PageBudgets();
	const auto &scan = pagination.ScanBudgets();
	return target.origin.scheme == PlannedUrlScheme::HTTPS && target.origin.host == "api.github.com" &&
	       target.origin.port == 443 && target.path == "/user/repos" && target.page_size_parameter == "per_page" &&
	       target.page_size == 100 && target.page_number_parameter == "page" && target.first_page == 1 &&
	       target.page_increment == 1 && scan.request_attempts == scan.pages &&
	       scan.response_bytes >= page.response_bytes && scan.header_bytes >= page.header_bytes &&
	       scan.decompressed_bytes >= page.decompressed_bytes && scan.decoded_records >= page.decoded_records &&
	       scan.decoded_memory_bytes >= page.decoded_memory_bytes;
}

bool IsAuthenticatedRepositoriesPlan(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	return plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.5.0" &&
	       plan.RelationName() == "authenticated_repositories" &&
	       plan.Domain() == BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS && HasRepositoryColumns(plan.OutputColumns()) &&
	       HasExpectedRepositoryOperation(plan.Operation(), profile) && HasExpectedPagination(plan) &&
	       plan.Budgets().decoded_records <= profile.max_decoded_records &&
	       plan.Providers() == FeatureState::DISABLED && plan.Retry() == FeatureState::DISABLED &&
	       plan.Cache() == FeatureState::DISABLED && plan.Authentication() == FeatureState::ENABLED &&
	       plan.SecretReference().IsPresent() && HasAuthenticatedObligation(plan.AuthenticationObligation(), profile) &&
	       HasExpectedRepositoryNetwork(plan.Network(), profile);
}

} // namespace

bool TryAdmitHttpPlan(const ScanPlan &plan, const HttpExecutionProfile &profile, AdmittedHttpOperation &operation) {
	if (TryAdmitSingleResponsePlan(plan, profile, operation)) {
		return true;
	}
	if (IsAuthenticatedRepositoriesPlan(plan, profile)) {
		operation = AdmittedHttpOperation::AUTHENTICATED_REPOSITORIES;
		return true;
	}
	return false;
}

} // namespace internal
} // namespace duckdb_api
