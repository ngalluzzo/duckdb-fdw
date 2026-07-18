#include "duckdb_api/connector.hpp"

#include <locale>
#include <sstream>
#include <stdexcept>

namespace duckdb_api {

namespace {

const char *OriginName(CompiledConnectorOrigin origin) {
	switch (origin) {
	case CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA:
		return "native_product_metadata";
	}
	throw std::logic_error("compiled connector contains an unknown origin");
}

const char *CardinalityName(CompiledOperationCardinality cardinality) {
	switch (cardinality) {
	case CompiledOperationCardinality::ZERO_TO_MANY:
		return "zero_to_many";
	}
	throw std::logic_error("compiled connector contains an unknown operation cardinality");
}

const char *ProtocolName(CompiledProtocol protocol) {
	switch (protocol) {
	case CompiledProtocol::REST:
		return "REST";
	}
	throw std::logic_error("compiled connector contains an unknown protocol");
}

const char *MethodName(CompiledHttpMethod method) {
	switch (method) {
	case CompiledHttpMethod::GET:
		return "GET";
	}
	throw std::logic_error("compiled connector contains an unknown HTTP method");
}

const char *ReplaySafetyName(CompiledReplaySafety replay_safety) {
	switch (replay_safety) {
	case CompiledReplaySafety::SAFE:
		return "replay_safe";
	}
	throw std::logic_error("compiled connector contains an unknown replay-safety declaration");
}

const char *EnabledState(bool enabled) {
	return enabled ? "enabled" : "disabled";
}

const char *AuthorityState(bool enabled) {
	return enabled ? "allowed" : "denied";
}

void AppendSchema(std::ostringstream &result, const std::vector<CompiledColumn> &columns) {
	for (std::size_t index = 0; index < columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		const auto &column = columns[index];
		result << column.name << ':' << column.logical_type << (column.nullable ? '?' : '!') << ':' << column.extractor;
	}
}

void AppendQuery(std::ostringstream &result, const std::vector<CompiledQueryParameter> &query_parameters) {
	for (std::size_t index = 0; index < query_parameters.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << query_parameters[index].name << '=' << query_parameters[index].encoded_value;
	}
}

void AppendHeaders(std::ostringstream &result, const std::vector<CompiledHttpHeader> &headers) {
	for (std::size_t index = 0; index < headers.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << headers[index].name << '=' << headers[index].value;
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

} // namespace

std::string CompiledConnector::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "origin=" << OriginName(origin) << ";connector=" << connector_name << ";version=" << version
	       << ";relation=" << relation_name << ";schema=";
	AppendSchema(result, columns);
	result << ";operation=" << operation.name << ':' << (operation.fallback ? "fallback" : "selected") << ':'
	       << CardinalityName(operation.cardinality) << ':' << ProtocolName(operation.protocol) << ':'
	       << MethodName(operation.method) << ':' << ReplaySafetyName(operation.replay_safety)
	       << ";request=base:" << operation.request.base_url << ",path:" << operation.request.path << ",query:[";
	AppendQuery(result, operation.request.query_parameters);
	result << "],headers:[";
	AppendHeaders(result, operation.request.headers);
	result << "];response_records=" << operation.records_extractor
	       << ";features=retry:" << EnabledState(operation.retry_enabled)
	       << ",authentication:" << EnabledState(operation.authentication_enabled)
	       << ",pagination:" << EnabledState(operation.pagination_enabled) << ";network=schemes:[";
	AppendStrings(result, network_policy.allowed_schemes);
	result << "],hosts:[";
	AppendStrings(result, network_policy.allowed_hosts);
	result << "],redirects:" << AuthorityState(network_policy.redirects_enabled)
	       << ",private:" << AuthorityState(network_policy.private_addresses_enabled)
	       << ",link_local:" << AuthorityState(network_policy.link_local_addresses_enabled)
	       << ",loopback:" << AuthorityState(network_policy.loopback_addresses_enabled)
	       << ",max_response_bytes:" << network_policy.max_response_bytes
	       << ";ceilings=records:" << resource_ceilings.max_records
	       << ",extracted_string_bytes:" << resource_ceilings.max_extracted_string_bytes;
	return result.str();
}

CompiledConnector BuildNativeGithubConnector() {
	CompiledConnector result;
	result.origin = CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA;
	result.connector_name = "github";
	result.version = "0.3.0";
	result.relation_name = "duckdb_login_search_page";
	result.columns = {{"id", "BIGINT", false, "$.id"},
	                  {"login", "VARCHAR", false, "$.login"},
	                  {"site_admin", "BOOLEAN", false, "$.site_admin"}};

	result.operation.name = "github_search_duckdb_login_page";
	result.operation.fallback = true;
	result.operation.cardinality = CompiledOperationCardinality::ZERO_TO_MANY;
	result.operation.protocol = CompiledProtocol::REST;
	result.operation.method = CompiledHttpMethod::GET;
	result.operation.replay_safety = CompiledReplaySafety::SAFE;
	result.operation.retry_enabled = false;
	result.operation.authentication_enabled = false;
	result.operation.pagination_enabled = false;
	result.operation.request.base_url = "https://api.github.com";
	result.operation.request.path = "/search/users";
	result.operation.request.query_parameters = {{"q", "duckdb+in%3Alogin"}, {"per_page", "3"}};
	result.operation.request.headers = {{"Accept", "application/vnd.github+json"},
	                                    {"User-Agent", "duckdb-api/0.3.0"},
	                                    {"X-GitHub-Api-Version", "2022-11-28"}};
	result.operation.records_extractor = "$.items[*]";

	result.network_policy.allowed_schemes = {"https"};
	result.network_policy.allowed_hosts = {"api.github.com"};
	result.network_policy.redirects_enabled = false;
	result.network_policy.private_addresses_enabled = false;
	result.network_policy.link_local_addresses_enabled = false;
	result.network_policy.loopback_addresses_enabled = false;
	result.network_policy.max_response_bytes = 65536;

	result.resource_ceilings.max_records = 3;
	result.resource_ceilings.max_extracted_string_bytes = 256;
	return result;
}

} // namespace duckdb_api
