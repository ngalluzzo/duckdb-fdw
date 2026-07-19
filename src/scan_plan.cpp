#include "duckdb_api/scan_plan.hpp"

#include <locale>
#include <sstream>
#include <stdexcept>

namespace duckdb_api {

namespace {

const char *ProtocolName(PlannedProtocol protocol) {
	switch (protocol) {
	case PlannedProtocol::REST:
		return "REST";
	}
	throw std::logic_error("scan plan contains an unknown protocol");
}

const char *MethodName(PlannedHttpMethod method) {
	switch (method) {
	case PlannedHttpMethod::GET:
		return "GET";
	}
	throw std::logic_error("scan plan contains an unknown HTTP method");
}

const char *CardinalityName(PlannedCardinality cardinality) {
	switch (cardinality) {
	case PlannedCardinality::ZERO_TO_MANY:
		return "zero_to_many";
	case PlannedCardinality::EXACTLY_ONE_ON_SUCCESS:
		return "exactly_one_on_success";
	}
	throw std::logic_error("scan plan contains an unknown cardinality");
}

const char *ReplaySafetyName(PlannedReplaySafety replay_safety) {
	switch (replay_safety) {
	case PlannedReplaySafety::SAFE:
		return "safe";
	}
	throw std::logic_error("scan plan contains an unknown replay-safety classification");
}

const char *UrlSchemeName(PlannedUrlScheme scheme) {
	switch (scheme) {
	case PlannedUrlScheme::HTTP:
		return "http";
	case PlannedUrlScheme::HTTPS:
		return "https";
	}
	throw std::logic_error("scan plan contains an unknown URL scheme");
}

const char *ResponseSourceName(PlannedResponseSource source) {
	switch (source) {
	case PlannedResponseSource::JSON_PATH_MANY:
		return "json_path_many";
	case PlannedResponseSource::ROOT_ARRAY:
		return "root_array";
	case PlannedResponseSource::ROOT_OBJECT:
		return "root_object";
	}
	throw std::logic_error("scan plan contains an unknown response source");
}

const char *BaseDomainName(BaseDomain domain) {
	switch (domain) {
	case BaseDomain::JSON_PATH_RECORDS:
		return "json_path_records";
	case BaseDomain::PAGINATED_JSON_PATH_RECORDS:
		return "paginated_json_path_records";
	case BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS:
		return "paginated_root_array_records";
	case BaseDomain::SUCCESSFUL_ROOT_OBJECT:
		return "successful_root_object";
	}
	throw std::logic_error("scan plan contains an unknown base domain");
}

const char *PredicateName(PlannedPredicate predicate, BaseDomain domain) {
	switch (predicate) {
	case PlannedPredicate::TRUE_FOR_BASE_DOMAIN:
		switch (domain) {
		case BaseDomain::JSON_PATH_RECORDS:
			return "TRUE@json_path_records";
		case BaseDomain::PAGINATED_JSON_PATH_RECORDS:
			return "TRUE@paginated_json_path_records";
		case BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS:
			return "TRUE@paginated_root_array_records";
		case BaseDomain::SUCCESSFUL_ROOT_OBJECT:
			return "TRUE@successful_root_object";
		}
	}
	throw std::logic_error("scan plan contains an unknown predicate classification");
}

const char *PaginationStrategyName(PlannedPaginationStrategy strategy) {
	switch (strategy) {
	case PlannedPaginationStrategy::DISABLED:
		return "disabled";
	case PlannedPaginationStrategy::LINK_HEADER:
		return "link_header";
	}
	throw std::logic_error("scan plan contains an unknown pagination strategy");
}

const char *PageDependencyName(PlannedPageDependency dependency) {
	switch (dependency) {
	case PlannedPageDependency::SEQUENTIAL:
		return "sequential";
	}
	throw std::logic_error("scan plan contains an unknown pagination dependency");
}

const char *PageConsistencyName(PlannedPageConsistency consistency) {
	switch (consistency) {
	case PlannedPageConsistency::MUTABLE:
		return "mutable";
	}
	throw std::logic_error("scan plan contains an unknown pagination consistency");
}

const char *LinkRelationName(PlannedLinkRelation relation) {
	switch (relation) {
	case PlannedLinkRelation::NEXT:
		return "next";
	}
	throw std::logic_error("scan plan contains an unknown Link relation");
}

const char *ContinuationTargetScopeName(PlannedContinuationTargetScope scope) {
	switch (scope) {
	case PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH:
		return "exact_operation_origin_and_path";
	}
	throw std::logic_error("scan plan contains an unknown pagination target scope");
}

const char *OwnerName(RelationalOwner owner) {
	switch (owner) {
	case RelationalOwner::DUCKDB:
		return "duckdb";
	}
	throw std::logic_error("scan plan contains an unknown relational owner");
}

const char *DelegationName(RelationalDelegation delegation) {
	switch (delegation) {
	case RelationalDelegation::NONE:
		return "none";
	}
	throw std::logic_error("scan plan contains an unknown relational delegation");
}

const char *FeatureName(FeatureState state) {
	switch (state) {
	case FeatureState::DISABLED:
		return "disabled";
	case FeatureState::ENABLED:
		return "enabled";
	}
	throw std::logic_error("scan plan contains an unknown feature state");
}

const char *RequirementName(PlannedCredentialRequirement requirement) {
	switch (requirement) {
	case PlannedCredentialRequirement::NONE:
		return "none";
	case PlannedCredentialRequirement::REQUIRED:
		return "required";
	}
	throw std::logic_error("scan plan contains an unknown credential requirement");
}

const char *AuthenticatorName(PlannedAuthenticator authenticator) {
	switch (authenticator) {
	case PlannedAuthenticator::NONE:
		return "none";
	case PlannedAuthenticator::BEARER:
		return "bearer";
	}
	throw std::logic_error("scan plan contains an unknown authenticator");
}

const char *PlacementName(PlannedCredentialPlacement placement) {
	switch (placement) {
	case PlannedCredentialPlacement::NONE:
		return "none";
	case PlannedCredentialPlacement::AUTHORIZATION_HEADER:
		return "Authorization";
	}
	throw std::logic_error("scan plan contains an unknown credential placement");
}

const char *PermissionName(bool enabled) {
	return enabled ? "allowed" : "denied";
}

void AppendStrings(std::ostringstream &result, const std::vector<std::string> &values) {
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << values[index];
	}
}

void AppendQuery(std::ostringstream &result, const std::vector<PlannedQueryParameter> &query) {
	for (std::size_t index = 0; index < query.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << query[index].name << '=' << query[index].encoded_value;
	}
}

void AppendHeaders(std::ostringstream &result, const std::vector<PlannedHttpHeader> &headers) {
	for (std::size_t index = 0; index < headers.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << headers[index].name << '=' << headers[index].value;
	}
}

void AppendColumns(std::ostringstream &result, const std::vector<PlannedColumn> &columns) {
	for (std::size_t index = 0; index < columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << columns[index].name << ':' << columns[index].logical_type << (columns[index].nullable ? '?' : '!')
		       << ':' << columns[index].extractor;
	}
}

void AppendOrigin(std::ostringstream &result, const PlannedRestOrigin &origin) {
	result << "[scheme:" << UrlSchemeName(origin.scheme) << ",host:" << origin.host << ",port:" << origin.port << ']';
}

void AppendScanBudgets(std::ostringstream &result, const ScanResourceBudgets &budgets) {
	result << "request_attempts:" << budgets.request_attempts << ",pages:" << budgets.pages
	       << ",response_bytes:" << budgets.response_bytes << ",header_bytes:" << budgets.header_bytes
	       << ",decompressed_bytes:" << budgets.decompressed_bytes << ",records:" << budgets.decoded_records
	       << ",string_bytes:" << budgets.extracted_string_bytes << ",json_nesting:" << budgets.json_nesting
	       << ",decoded_memory_bytes:" << budgets.decoded_memory_bytes << ",batch_rows:" << budgets.batch_rows
	       << ",wall_ms:" << budgets.wall_milliseconds << ",concurrency:" << budgets.concurrency;
}

void AppendPagination(std::ostringstream &result, const PaginationPlan &pagination) {
	result << PaginationStrategyName(pagination.Strategy());
	if (pagination.Strategy() == PlannedPaginationStrategy::DISABLED) {
		return;
	}
	const auto &target = pagination.Target();
	result << "[relation:" << LinkRelationName(pagination.LinkRelation())
	       << ",dependency:" << PageDependencyName(pagination.Dependency())
	       << ",consistency:" << PageConsistencyName(pagination.Consistency())
	       << ",total:" << (pagination.SupportsTotal() ? "supported" : "none")
	       << ",resume:" << (pagination.SupportsResume() ? "supported" : "none")
	       << ",target_scope:" << ContinuationTargetScopeName(pagination.TargetScope()) << ",origin:";
	AppendOrigin(result, target.origin);
	result << ",path:" << target.path << ",page_size:" << target.page_size_parameter << '=' << target.page_size
	       << ",page_number:" << target.page_number_parameter << '=' << target.first_page
	       << ",increment:" << target.page_increment << ",scan_budgets:";
	AppendScanBudgets(result, pagination.ScanBudgets());
	result << ']';
}

} // namespace

bool ResourceBudgets::IsWithinLiveRestBounds() const {
	return request_attempts == HOST_MAX_REQUEST_ATTEMPTS && response_bytes > 0 &&
	       response_bytes <= HOST_MAX_RESPONSE_BYTES && header_bytes > 0 && header_bytes <= HOST_MAX_HEADER_BYTES &&
	       decompressed_bytes > 0 && decompressed_bytes <= HOST_MAX_DECOMPRESSED_BYTES && decoded_records > 0 &&
	       decoded_records <= HOST_MAX_DECODED_RECORDS && extracted_string_bytes > 0 &&
	       extracted_string_bytes <= HOST_MAX_EXTRACTED_STRING_BYTES && json_nesting > 0 &&
	       json_nesting <= HOST_MAX_JSON_NESTING && decoded_memory_bytes > 0 &&
	       decoded_memory_bytes <= HOST_MAX_DECODED_MEMORY_BYTES && batch_rows > 0 && batch_rows <= OUTPUT_BATCH_ROWS &&
	       wall_milliseconds > 0 && wall_milliseconds <= MAX_EXECUTION_MILLISECONDS &&
	       concurrency == HOST_MAX_CONCURRENCY;
}

bool ResourceBudgets::IsWithinPaginatedPageBounds() const {
	return request_attempts == PAGINATION_MAX_REQUEST_ATTEMPTS_PER_PAGE && response_bytes > 0 &&
	       response_bytes <= PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE && header_bytes > 0 &&
	       header_bytes <= PAGINATION_MAX_HEADER_BYTES_PER_PAGE && decompressed_bytes > 0 &&
	       decompressed_bytes <= PAGINATION_MAX_DECOMPRESSED_BYTES_PER_PAGE && decoded_records > 0 &&
	       decoded_records <= PAGINATION_MAX_DECODED_RECORDS_PER_PAGE && extracted_string_bytes > 0 &&
	       extracted_string_bytes <= PAGINATION_MAX_EXTRACTED_STRING_BYTES && json_nesting > 0 &&
	       json_nesting <= PAGINATION_MAX_JSON_NESTING && decoded_memory_bytes > 0 &&
	       decoded_memory_bytes <= PAGINATION_MAX_DECODED_MEMORY_BYTES && batch_rows > 0 &&
	       batch_rows <= PAGINATION_OUTPUT_BATCH_ROWS && wall_milliseconds > 0 &&
	       wall_milliseconds <= PAGINATION_MAX_EXECUTION_MILLISECONDS && concurrency == PAGINATION_MAX_CONCURRENCY;
}

bool ScanResourceBudgets::IsWithinPaginatedScanBounds() const {
	return request_attempts > 0 && request_attempts == pages &&
	       request_attempts <= PAGINATION_MAX_REQUEST_ATTEMPTS_PER_SCAN && pages <= PAGINATION_MAX_PAGES_PER_SCAN &&
	       response_bytes > 0 && response_bytes <= PAGINATION_MAX_RESPONSE_BYTES_PER_SCAN && header_bytes > 0 &&
	       header_bytes <= PAGINATION_MAX_HEADER_BYTES_PER_SCAN && decompressed_bytes > 0 &&
	       decompressed_bytes <= PAGINATION_MAX_DECOMPRESSED_BYTES_PER_SCAN && decoded_records > 0 &&
	       decoded_records <= PAGINATION_MAX_DECODED_RECORDS_PER_SCAN && extracted_string_bytes > 0 &&
	       extracted_string_bytes <= PAGINATION_MAX_EXTRACTED_STRING_BYTES && json_nesting > 0 &&
	       json_nesting <= PAGINATION_MAX_JSON_NESTING && decoded_memory_bytes > 0 &&
	       decoded_memory_bytes <= PAGINATION_MAX_DECODED_MEMORY_BYTES && batch_rows > 0 &&
	       batch_rows <= PAGINATION_OUTPUT_BATCH_ROWS && wall_milliseconds > 0 &&
	       wall_milliseconds <= PAGINATION_MAX_EXECUTION_MILLISECONDS && concurrency == PAGINATION_MAX_CONCURRENCY;
}

PaginationPlan::PaginationPlan()
    : strategy(PlannedPaginationStrategy::DISABLED), dependency(PlannedPageDependency::SEQUENTIAL),
      consistency(PlannedPageConsistency::MUTABLE), link_relation(PlannedLinkRelation::NEXT),
      target_scope(PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH), supports_total(false),
      supports_resume(false), target {{PlannedUrlScheme::HTTPS, "", 0}, "", "", 0, "", 0, 0},
      page_budgets {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, scan_budgets {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} {
}

void PaginationPlan::RequireLinkHeader() const {
	if (strategy != PlannedPaginationStrategy::LINK_HEADER) {
		throw std::logic_error("disabled pagination has no Link plan payload");
	}
}

PlannedPaginationStrategy PaginationPlan::Strategy() const {
	return strategy;
}

PlannedPageDependency PaginationPlan::Dependency() const {
	RequireLinkHeader();
	return dependency;
}

PlannedPageConsistency PaginationPlan::Consistency() const {
	RequireLinkHeader();
	return consistency;
}

PlannedLinkRelation PaginationPlan::LinkRelation() const {
	RequireLinkHeader();
	return link_relation;
}

PlannedContinuationTargetScope PaginationPlan::TargetScope() const {
	RequireLinkHeader();
	return target_scope;
}

bool PaginationPlan::SupportsTotal() const {
	RequireLinkHeader();
	return supports_total;
}

bool PaginationPlan::SupportsResume() const {
	RequireLinkHeader();
	return supports_resume;
}

const PlannedPaginationTarget &PaginationPlan::Target() const {
	RequireLinkHeader();
	return target;
}

const ResourceBudgets &PaginationPlan::PageBudgets() const {
	RequireLinkHeader();
	return page_budgets;
}

const ScanResourceBudgets &PaginationPlan::ScanBudgets() const {
	RequireLinkHeader();
	return scan_budgets;
}

PlannedAuthenticationObligation::PlannedAuthenticationObligation()
    : requirement(PlannedCredentialRequirement::NONE), logical_credential(), authenticator(PlannedAuthenticator::NONE),
      placement(PlannedCredentialPlacement::NONE), has_destination(false),
      destination {PlannedUrlScheme::HTTPS, "", 0} {
}

PlannedCredentialRequirement PlannedAuthenticationObligation::Requirement() const {
	return requirement;
}

const std::string &PlannedAuthenticationObligation::LogicalCredential() const {
	return logical_credential;
}

PlannedAuthenticator PlannedAuthenticationObligation::Authenticator() const {
	return authenticator;
}

PlannedCredentialPlacement PlannedAuthenticationObligation::Placement() const {
	return placement;
}

const PlannedRestOrigin *PlannedAuthenticationObligation::Destination() const {
	return has_destination ? &destination : nullptr;
}

ScanPlan::ScanPlan() {
}

const std::string &ScanPlan::ConnectorName() const {
	return connector_name;
}

const std::string &ScanPlan::ConnectorVersion() const {
	return connector_version;
}

const std::string &ScanPlan::RelationName() const {
	return relation_name;
}

const std::string &ScanPlan::SourceSnapshot() const {
	return source_snapshot;
}

BaseDomain ScanPlan::Domain() const {
	return domain;
}

const PlannedRestOperation &ScanPlan::Operation() const {
	return operation;
}

const std::vector<PlannedColumn> &ScanPlan::OutputColumns() const {
	return output_columns;
}

PlannedPredicate ScanPlan::RemotePredicate() const {
	return remote_predicate;
}

PlannedPredicate ScanPlan::ResidualPredicate() const {
	return residual_predicate;
}

RelationalOwner ScanPlan::ResidualOwner() const {
	return residual_owner;
}

const RelationalOwnership &ScanPlan::Ownership() const {
	return ownership;
}

RelationalDelegation ScanPlan::RemoteOrdering() const {
	return remote_ordering;
}

RelationalDelegation ScanPlan::RuntimeOrdering() const {
	return runtime_ordering;
}

RelationalDelegation ScanPlan::RemoteLimit() const {
	return remote_limit;
}

RelationalDelegation ScanPlan::RemoteOffset() const {
	return remote_offset;
}

RelationalDelegation ScanPlan::RuntimeLimit() const {
	return runtime_limit;
}

RelationalDelegation ScanPlan::RuntimeOffset() const {
	return runtime_offset;
}

const PaginationPlan &ScanPlan::Pagination() const {
	return pagination;
}

FeatureState ScanPlan::Providers() const {
	return providers;
}

FeatureState ScanPlan::Retry() const {
	return retry;
}

FeatureState ScanPlan::Cache() const {
	return cache;
}

FeatureState ScanPlan::Authentication() const {
	return authentication;
}

const LogicalSecretReference &ScanPlan::SecretReference() const {
	return secret_reference;
}

const PlannedAuthenticationObligation &ScanPlan::AuthenticationObligation() const {
	return authentication_obligation;
}

const NetworkCapability &ScanPlan::Network() const {
	return network;
}

const ResourceBudgets &ScanPlan::Budgets() const {
	return pagination.Strategy() == PlannedPaginationStrategy::LINK_HEADER ? pagination.PageBudgets() : budgets;
}

const std::string &ScanPlan::ClassificationReason() const {
	return classification_reason;
}

std::string ScanPlan::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "connector=" << connector_name << ";version=" << connector_version << ";relation=" << relation_name
	       << ";source_snapshot=[" << source_snapshot << "];domain=" << BaseDomainName(domain)
	       << ";operation=" << operation.operation_name << ':' << CardinalityName(operation.cardinality) << ':'
	       << ProtocolName(operation.protocol) << ':' << MethodName(operation.method) << ':'
	       << ReplaySafetyName(operation.replay_safety) << ";request=origin:";
	AppendOrigin(result, operation.origin);
	result << ",path:" << operation.path << ",query:[";
	AppendQuery(result, operation.query_parameters);
	result << "],headers:[";
	AppendHeaders(result, operation.headers);
	result << "];response=source:" << ResponseSourceName(operation.response_source)
	       << ",records:" << operation.records_extractor << ";projection=";
	AppendColumns(result, output_columns);
	result << ";remote_predicate=" << PredicateName(remote_predicate, domain)
	       << ";residual_predicate=" << PredicateName(residual_predicate, domain)
	       << ";residual_owner=" << OwnerName(residual_owner) << ";owners=filter:" << OwnerName(ownership.filter)
	       << ",ordering:" << OwnerName(ownership.ordering) << ",limit:" << OwnerName(ownership.limit)
	       << ",offset:" << OwnerName(ownership.offset)
	       << ";delegation=remote_ordering:" << DelegationName(remote_ordering)
	       << ",runtime_ordering:" << DelegationName(runtime_ordering)
	       << ",remote_limit:" << DelegationName(remote_limit) << ",remote_offset:" << DelegationName(remote_offset)
	       << ",runtime_limit:" << DelegationName(runtime_limit) << ",runtime_offset:" << DelegationName(runtime_offset)
	       << ";features=pagination:";
	AppendPagination(result, pagination);
	result << ",providers:" << FeatureName(providers) << ",retry:" << FeatureName(retry)
	       << ",cache:" << FeatureName(cache) << ",authentication:" << FeatureName(authentication)
	       << ";secret-reference=" << secret_reference.Snapshot()
	       << ";auth-obligation=requirement:" << RequirementName(authentication_obligation.Requirement())
	       << ",logical_credential:"
	       << (authentication_obligation.LogicalCredential().empty() ? "none"
	                                                                 : authentication_obligation.LogicalCredential())
	       << ",authenticator:" << AuthenticatorName(authentication_obligation.Authenticator())
	       << ",placement:" << PlacementName(authentication_obligation.Placement()) << ",destination:";
	if (authentication_obligation.Destination() == nullptr) {
		result << "none";
	} else {
		AppendOrigin(result, *authentication_obligation.Destination());
	}
	result << ";network=schemes:[";
	AppendStrings(result, network.allowed_schemes);
	result << "],hosts:[";
	AppendStrings(result, network.allowed_hosts);
	result << "],redirects:" << PermissionName(network.redirects_enabled)
	       << ",private:" << PermissionName(network.private_addresses_enabled)
	       << ",link_local:" << PermissionName(network.link_local_addresses_enabled)
	       << ",loopback:" << PermissionName(network.loopback_addresses_enabled);
	const auto &effective_budgets = Budgets();
	result << ";budgets=request_attempts:" << effective_budgets.request_attempts
	       << ",response_bytes:" << effective_budgets.response_bytes
	       << ",header_bytes:" << effective_budgets.header_bytes
	       << ",decompressed_bytes:" << effective_budgets.decompressed_bytes
	       << ",records:" << effective_budgets.decoded_records
	       << ",string_bytes:" << effective_budgets.extracted_string_bytes
	       << ",json_nesting:" << effective_budgets.json_nesting
	       << ",decoded_memory_bytes:" << effective_budgets.decoded_memory_bytes
	       << ",batch_rows:" << effective_budgets.batch_rows << ",wall_ms:" << effective_budgets.wall_milliseconds
	       << ",concurrency:" << effective_budgets.concurrency << ";reason=" << classification_reason;
	return result.str();
}

} // namespace duckdb_api
