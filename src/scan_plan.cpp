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

const char *BaseDomainName(BaseDomain domain) {
	switch (domain) {
	case BaseDomain::SINGLE_RESPONSE_PAGE:
		return "single_response_page";
	}
	throw std::logic_error("scan plan contains an unknown base domain");
}

const char *PredicateName(PlannedPredicate predicate) {
	switch (predicate) {
	case PlannedPredicate::TRUE_FOR_BASE_DOMAIN:
		return "TRUE@single_response_page";
	}
	throw std::logic_error("scan plan contains an unknown predicate classification");
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
	}
	throw std::logic_error("scan plan contains an unknown feature state");
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

} // namespace

bool ResourceBudgets::IsWithinLiveRestBounds() const {
	return request_attempts == HOST_MAX_REQUEST_ATTEMPTS && response_bytes > 0 &&
	       response_bytes <= HOST_MAX_RESPONSE_BYTES && header_bytes > 0 && header_bytes <= HOST_MAX_HEADER_BYTES &&
	       decompressed_bytes > 0 && decompressed_bytes <= HOST_MAX_DECOMPRESSED_BYTES && decoded_records > 0 &&
	       decoded_records <= LIVE_RELATION_MAX_RECORDS && extracted_string_bytes > 0 &&
	       extracted_string_bytes <= HOST_MAX_EXTRACTED_STRING_BYTES && json_nesting > 0 &&
	       json_nesting <= HOST_MAX_JSON_NESTING && decoded_memory_bytes > 0 &&
	       decoded_memory_bytes <= HOST_MAX_DECODED_MEMORY_BYTES && batch_rows > 0 && batch_rows <= OUTPUT_BATCH_ROWS &&
	       wall_milliseconds > 0 && wall_milliseconds <= MAX_EXECUTION_MILLISECONDS &&
	       concurrency == HOST_MAX_CONCURRENCY;
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

FeatureState ScanPlan::Pagination() const {
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

const NetworkCapability &ScanPlan::Network() const {
	return network;
}

const ResourceBudgets &ScanPlan::Budgets() const {
	return budgets;
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
	       << ReplaySafetyName(operation.replay_safety)
	       << ";request=origin:[scheme:" << UrlSchemeName(operation.origin.scheme) << ",host:" << operation.origin.host
	       << ",port:" << operation.origin.port << "],path:" << operation.path << ",query:[";
	AppendQuery(result, operation.query_parameters);
	result << "],headers:[";
	AppendHeaders(result, operation.headers);
	result << "];response_records=" << operation.records_extractor << ";projection=";
	AppendColumns(result, output_columns);
	result << ";remote_predicate=" << PredicateName(remote_predicate)
	       << ";residual_predicate=" << PredicateName(residual_predicate)
	       << ";residual_owner=" << OwnerName(residual_owner) << ";owners=filter:" << OwnerName(ownership.filter)
	       << ",ordering:" << OwnerName(ownership.ordering) << ",limit:" << OwnerName(ownership.limit)
	       << ",offset:" << OwnerName(ownership.offset)
	       << ";delegation=remote_ordering:" << DelegationName(remote_ordering)
	       << ",runtime_ordering:" << DelegationName(runtime_ordering)
	       << ",remote_limit:" << DelegationName(remote_limit) << ",remote_offset:" << DelegationName(remote_offset)
	       << ",runtime_limit:" << DelegationName(runtime_limit) << ",runtime_offset:" << DelegationName(runtime_offset)
	       << ";features=pagination:" << FeatureName(pagination) << ",providers:" << FeatureName(providers)
	       << ",retry:" << FeatureName(retry) << ",cache:" << FeatureName(cache)
	       << ",authentication:" << FeatureName(authentication) << ";network=schemes:[";
	AppendStrings(result, network.allowed_schemes);
	result << "],hosts:[";
	AppendStrings(result, network.allowed_hosts);
	result << "],redirects:" << PermissionName(network.redirects_enabled)
	       << ",private:" << PermissionName(network.private_addresses_enabled)
	       << ",link_local:" << PermissionName(network.link_local_addresses_enabled)
	       << ",loopback:" << PermissionName(network.loopback_addresses_enabled)
	       << ";budgets=request_attempts:" << budgets.request_attempts << ",response_bytes:" << budgets.response_bytes
	       << ",header_bytes:" << budgets.header_bytes << ",decompressed_bytes:" << budgets.decompressed_bytes
	       << ",records:" << budgets.decoded_records << ",string_bytes:" << budgets.extracted_string_bytes
	       << ",json_nesting:" << budgets.json_nesting << ",decoded_memory_bytes:" << budgets.decoded_memory_bytes
	       << ",batch_rows:" << budgets.batch_rows << ",wall_ms:" << budgets.wall_milliseconds
	       << ",concurrency:" << budgets.concurrency << ";reason=" << classification_reason;
	return result.str();
}

} // namespace duckdb_api
