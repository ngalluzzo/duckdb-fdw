#include "duckdb_api/connector.hpp"

#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsAsciiLowerOrDigit(char value) {
	return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
}

bool IsCanonicalHost(const std::string &host) {
	if (host.empty() || !IsAsciiLowerOrDigit(host.front()) || !IsAsciiLowerOrDigit(host.back())) {
		return false;
	}
	for (std::size_t index = 0; index < host.size(); index++) {
		const auto value = host[index];
		if (!IsAsciiLowerOrDigit(value) && value != '-' && value != '.') {
			return false;
		}
		if (value == '.' && (index == 0 || index + 1 == host.size() || host[index - 1] == '.' ||
		                     host[index - 1] == '-' || host[index + 1] == '-')) {
			return false;
		}
	}
	return true;
}

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

const char *UrlSchemeName(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return "http";
	case CompiledUrlScheme::HTTPS:
		return "https";
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
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

CompiledRestHost::CompiledRestHost(std::string value_p) : value(std::move(value_p)) {
	if (!IsCanonicalHost(value)) {
		throw std::invalid_argument("compiled REST host is not one exact canonical host component");
	}
}

const std::string &CompiledRestHost::Value() const {
	return value;
}

std::string CompiledConnector::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "origin=" << OriginName(origin) << ";connector=" << connector_name << ";version=" << version
	       << ";relation=" << relation_name << ";schema=";
	AppendSchema(result, columns);
	result << ";operation=" << operation.name << ':' << (operation.fallback ? "fallback" : "selected") << ':'
	       << CardinalityName(operation.cardinality) << ':' << ProtocolName(operation.protocol) << ':'
	       << MethodName(operation.method) << ':' << ReplaySafetyName(operation.replay_safety)
	       << ";request=origin:[scheme:" << UrlSchemeName(operation.request.origin.scheme)
	       << ",host:" << operation.request.origin.host.Value() << ",port:" << operation.request.origin.port
	       << "],path:" << operation.request.path << ",query:[";
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
	return {CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA,
	        "github",
	        "0.3.0",
	        "duckdb_login_search_page",
	        {{"id", "BIGINT", false, "$.id"},
	         {"login", "VARCHAR", false, "$.login"},
	         {"site_admin", "BOOLEAN", false, "$.site_admin"}},
	        {"github_search_duckdb_login_page",
	         true,
	         CompiledOperationCardinality::ZERO_TO_MANY,
	         CompiledProtocol::REST,
	         CompiledHttpMethod::GET,
	         CompiledReplaySafety::SAFE,
	         false,
	         false,
	         false,
	         {{CompiledUrlScheme::HTTPS, CompiledRestHost("api.github.com"), 443},
	          "/search/users",
	          {{"q", "duckdb+in%3Alogin"}, {"per_page", "3"}},
	          {{"Accept", "application/vnd.github+json"},
	           {"User-Agent", "duckdb-api/0.3.0"},
	           {"X-GitHub-Api-Version", "2022-11-28"}}},
	         "$.items[*]"},
	        {{"https"}, {"api.github.com"}, false, false, false, false, 65536},
	        {3, 256}};
}

} // namespace duckdb_api
