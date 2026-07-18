#include "duckdb_api/scan_plan.hpp"

#include <algorithm>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

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

const char *UrlSchemeName(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return "http";
	case CompiledUrlScheme::HTTPS:
		return "https";
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
}

PlannedUrlScheme PlanUrlScheme(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return PlannedUrlScheme::HTTP;
	case CompiledUrlScheme::HTTPS:
		return PlannedUrlScheme::HTTPS;
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
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

bool IsSupportedLogicalType(const std::string &logical_type) {
	return logical_type == "BIGINT" || logical_type == "VARCHAR" || logical_type == "BOOLEAN";
}

template <class VALUE>
bool HasDuplicateName(const std::vector<VALUE> &values) {
	for (std::size_t left = 0; left < values.size(); left++) {
		if (values[left].name.empty()) {
			return true;
		}
		for (std::size_t right = left + 1; right < values.size(); right++) {
			if (values[left].name == values[right].name) {
				return true;
			}
		}
	}
	return false;
}

bool ContainsUrlStructure(const std::string &value) {
	return value.find_first_of("?#\r\n") != std::string::npos;
}

bool HasInvalidQueryStructure(const std::vector<CompiledQueryParameter> &query_parameters) {
	for (const auto &parameter : query_parameters) {
		if (parameter.name.find_first_of("=&?#\r\n") != std::string::npos || parameter.encoded_value.empty() ||
		    parameter.encoded_value.find_first_of("&=?#\r\n") != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool HasInvalidHeaderStructure(const std::vector<CompiledHttpHeader> &headers) {
	for (const auto &header : headers) {
		if (header.value.empty() || header.name.find_first_of(": \t\r\n") != std::string::npos ||
		    header.value.find_first_of("\r\n") != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool HasExactPolicyEntry(const std::vector<std::string> &values, const std::string &expected) {
	return values.size() == 1 && values[0] == expected;
}

bool IsSupportedOriginPort(const CompiledRestOrigin &origin, const CompiledNetworkPolicy &policy) {
	if (origin.port == 0) {
		return false;
	}
	if (origin.scheme == CompiledUrlScheme::HTTPS) {
		return origin.port == 443 && !policy.loopback_addresses_enabled;
	}
	return origin.scheme == CompiledUrlScheme::HTTP && policy.loopback_addresses_enabled;
}

std::vector<std::string> ProjectedColumnNames(const std::vector<CompiledColumn> &columns) {
	std::vector<std::string> result;
	result.reserve(columns.size());
	for (const auto &column : columns) {
		result.push_back(column.name);
	}
	return result;
}

void ValidateConnector(const CompiledConnector &connector) {
	if (connector.origin != CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA || connector.connector_name.empty() ||
	    connector.version.empty() || connector.relation_name.empty() || connector.columns.empty()) {
		throw std::logic_error("live planner received incomplete native connector identity");
	}
	if (HasDuplicateName(connector.columns)) {
		throw std::logic_error("live planner received an invalid output schema");
	}
	for (const auto &column : connector.columns) {
		if (column.nullable || !IsSupportedLogicalType(column.logical_type) || column.extractor.empty()) {
			throw std::logic_error("live planner received an unsupported output column");
		}
	}

	const auto &operation = connector.operation;
	if (operation.name.empty() || !operation.fallback ||
	    operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY ||
	    operation.protocol != CompiledProtocol::REST || operation.method != CompiledHttpMethod::GET ||
	    operation.replay_safety != CompiledReplaySafety::SAFE || operation.retry_enabled ||
	    operation.authentication_enabled || operation.pagination_enabled || operation.records_extractor.empty()) {
		throw std::logic_error("live planner received an unsupported base-row operation");
	}
	if (operation.request.path.empty() || operation.request.path[0] != '/' ||
	    ContainsUrlStructure(operation.request.path) || HasDuplicateName(operation.request.query_parameters) ||
	    HasInvalidQueryStructure(operation.request.query_parameters) || HasDuplicateName(operation.request.headers) ||
	    HasInvalidHeaderStructure(operation.request.headers)) {
		throw std::logic_error("live planner received invalid structural REST metadata");
	}

	const auto &origin = operation.request.origin;
	const auto scheme = UrlSchemeName(origin.scheme);
	const auto &host = origin.host.Value();
	if (!HasExactPolicyEntry(connector.network_policy.allowed_schemes, scheme) ||
	    !HasExactPolicyEntry(connector.network_policy.allowed_hosts, host) ||
	    !IsSupportedOriginPort(origin, connector.network_policy) || connector.network_policy.redirects_enabled ||
	    connector.network_policy.private_addresses_enabled || connector.network_policy.link_local_addresses_enabled ||
	    connector.network_policy.max_response_bytes == 0 || connector.resource_ceilings.max_records == 0 ||
	    connector.resource_ceilings.max_extracted_string_bytes == 0) {
		throw std::logic_error("live planner received an unsupported network or resource declaration");
	}
}

void ValidateRequest(const CompiledConnector &connector, const ScanRequest &request) {
	if (request.connector_name != connector.connector_name || request.relation_name != connector.relation_name ||
	    !request.explicit_inputs.empty() || request.projected_columns != ProjectedColumnNames(connector.columns) ||
	    request.predicate != "TRUE" || !request.orderings.empty() || request.has_limit || request.has_offset ||
	    !request.capabilities.IsConservativePreview()) {
		throw std::logic_error("live planner received a non-conservative scan request");
	}
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
	       decoded_records <= HOST_MAX_DECODED_RECORDS && extracted_string_bytes > 0 &&
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

ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request) {
	ValidateConnector(connector);
	ValidateRequest(connector, request);

	ScanPlan result;
	result.connector_name = connector.connector_name;
	result.connector_version = connector.version;
	result.relation_name = connector.relation_name;
	result.source_snapshot = connector.Snapshot();
	result.domain = BaseDomain::SINGLE_RESPONSE_PAGE;

	result.operation.operation_name = connector.operation.name;
	result.operation.protocol = PlannedProtocol::REST;
	result.operation.method = PlannedHttpMethod::GET;
	result.operation.cardinality = PlannedCardinality::ZERO_TO_MANY;
	result.operation.replay_safety = PlannedReplaySafety::SAFE;
	result.operation.origin = {PlanUrlScheme(connector.operation.request.origin.scheme),
	                           connector.operation.request.origin.host.Value(),
	                           connector.operation.request.origin.port};
	result.operation.path = connector.operation.request.path;
	for (const auto &query : connector.operation.request.query_parameters) {
		result.operation.query_parameters.push_back({query.name, query.encoded_value});
	}
	for (const auto &header : connector.operation.request.headers) {
		result.operation.headers.push_back({header.name, header.value});
	}
	result.operation.records_extractor = connector.operation.records_extractor;

	for (const auto &column : connector.columns) {
		result.output_columns.push_back({column.name, column.logical_type, column.nullable, column.extractor});
	}
	result.remote_predicate = PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	result.residual_predicate = PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	result.residual_owner = RelationalOwner::DUCKDB;
	result.ownership = {RelationalOwner::DUCKDB, RelationalOwner::DUCKDB, RelationalOwner::DUCKDB,
	                    RelationalOwner::DUCKDB};
	result.remote_ordering = RelationalDelegation::NONE;
	result.runtime_ordering = RelationalDelegation::NONE;
	result.remote_limit = RelationalDelegation::NONE;
	result.remote_offset = RelationalDelegation::NONE;
	result.runtime_limit = RelationalDelegation::NONE;
	result.runtime_offset = RelationalDelegation::NONE;
	result.pagination = FeatureState::DISABLED;
	result.providers = FeatureState::DISABLED;
	result.retry = FeatureState::DISABLED;
	result.cache = FeatureState::DISABLED;
	result.authentication = FeatureState::DISABLED;
	result.network = {connector.network_policy.allowed_schemes,
	                  connector.network_policy.allowed_hosts,
	                  connector.network_policy.redirects_enabled,
	                  connector.network_policy.private_addresses_enabled,
	                  connector.network_policy.link_local_addresses_enabled,
	                  connector.network_policy.loopback_addresses_enabled};
	result.budgets = {HOST_MAX_REQUEST_ATTEMPTS,
	                  std::min(connector.network_policy.max_response_bytes, HOST_MAX_RESPONSE_BYTES),
	                  HOST_MAX_HEADER_BYTES,
	                  HOST_MAX_DECOMPRESSED_BYTES,
	                  std::min(connector.resource_ceilings.max_records, HOST_MAX_DECODED_RECORDS),
	                  std::min(connector.resource_ceilings.max_extracted_string_bytes, HOST_MAX_EXTRACTED_STRING_BYTES),
	                  HOST_MAX_JSON_NESTING,
	                  HOST_MAX_DECODED_MEMORY_BYTES,
	                  OUTPUT_BATCH_ROWS,
	                  MAX_EXECUTION_MILLISECONDS,
	                  HOST_MAX_CONCURRENCY};
	result.classification_reason =
	    "fixed request defines the complete single-response base domain; DuckDB retains all relational operators";
	return result;
}

} // namespace duckdb_api
