#include "duckdb_api/scan_plan.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

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
	    connector.resource_ceilings.max_records > LIVE_RELATION_MAX_RECORDS ||
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

} // namespace

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
	                  connector.resource_ceilings.max_records,
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
