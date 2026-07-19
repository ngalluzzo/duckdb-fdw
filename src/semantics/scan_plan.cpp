#include "duckdb_api/scan_plan.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api {

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

PlannedSecretReference::PlannedSecretReference() : exact_duckdb_secret_name() {
}

PlannedSecretReference::PlannedSecretReference(std::string exact_duckdb_secret_name_p)
    : exact_duckdb_secret_name(std::move(exact_duckdb_secret_name_p)) {
}

bool PlannedSecretReference::IsPresent() const noexcept {
	return !exact_duckdb_secret_name.empty();
}

const std::string &PlannedSecretReference::Name() const {
	if (!IsPresent()) {
		throw std::logic_error("planned secret reference is absent");
	}
	return exact_duckdb_secret_name;
}

std::string PlannedSecretReference::Snapshot() const {
	if (!IsPresent()) {
		return "none";
	}
	static const char HEX_DIGITS[] = "0123456789abcdef";
	std::string result = "named-hex:";
	result.reserve(result.size() + exact_duckdb_secret_name.size() * 2);
	for (const char character : exact_duckdb_secret_name) {
		const auto byte = static_cast<unsigned char>(character);
		result.push_back(HEX_DIGITS[byte >> 4]);
		result.push_back(HEX_DIGITS[byte & 0x0f]);
	}
	return result;
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

const PlannedSecretReference &ScanPlan::SecretReference() const {
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

} // namespace duckdb_api
