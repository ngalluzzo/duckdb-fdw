#include "duckdb_api/internal/connector/protocol_operation_declaration.hpp"

#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"
#include "duckdb_api/internal/connector/pagination_declaration.hpp"

#include <ostream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsAsciiDigit(char value) {
	return value >= '0' && value <= '9';
}

bool IsAsciiLower(char value) {
	return value >= 'a' && value <= 'z';
}

bool IsCanonicalHost(const std::string &host) {
	if (host.empty() || (!IsAsciiLower(host.front()) && !IsAsciiDigit(host.front())) ||
	    (!IsAsciiLower(host.back()) && !IsAsciiDigit(host.back()))) {
		return false;
	}
	for (std::size_t index = 0; index < host.size(); index++) {
		const auto value = host[index];
		if (!IsAsciiLower(value) && !IsAsciiDigit(value) && value != '-' && value != '.') {
			return false;
		}
		if (value == '.' && (index == 0 || index + 1 == host.size() || host[index - 1] == '.' ||
		                     host[index - 1] == '-' || host[index + 1] == '-')) {
			return false;
		}
	}
	return true;
}

char AsciiLower(char value) {
	return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool EqualsAsciiIgnoreCase(const std::string &left, const std::string &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (AsciiLower(left[index]) != AsciiLower(right[index])) {
			return false;
		}
	}
	return true;
}

bool IsHeaderName(const std::string &value) {
	if (value.empty()) {
		return false;
	}
	for (const auto character : value) {
		const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
		if (!letter && !IsAsciiDigit(character) && character != '-') {
			return false;
		}
	}
	return true;
}

const char *MethodName(CompiledHttpMethod method) {
	switch (method) {
	case CompiledHttpMethod::GET:
		return "GET";
	}
	throw std::logic_error("compiled REST operation contains an unknown HTTP method");
}

const char *ReplaySafetyName(CompiledReplaySafety safety) {
	switch (safety) {
	case CompiledReplaySafety::SAFE:
		return "replay_safe";
	}
	throw std::logic_error("compiled REST operation contains an unknown replay-safety declaration");
}

const char *ResponseSourceName(CompiledResponseSource source) {
	switch (source) {
	case CompiledResponseSource::JSON_PATH_MANY:
		return "json_path_many";
	case CompiledResponseSource::ROOT_ARRAY:
		return "root_array";
	case CompiledResponseSource::ROOT_OBJECT:
		return "root_object";
	}
	throw std::logic_error("compiled REST operation contains an unknown response source");
}

void ValidateQueryParameters(const std::vector<CompiledQueryParameter> &parameters) {
	for (std::size_t index = 0; index < parameters.size(); index++) {
		const auto &parameter = parameters[index];
		if (parameter.name.empty() || parameter.name.find_first_of("=&?#\r\n") != std::string::npos ||
		    parameter.encoded_value.empty() || parameter.encoded_value.find_first_of("&=?#\r\n") != std::string::npos) {
			throw std::invalid_argument("compiled REST request contains an invalid fixed query field");
		}
		for (std::size_t other = index + 1; other < parameters.size(); other++) {
			if (parameter.name == parameters[other].name) {
				throw std::invalid_argument("compiled REST request contains a duplicate fixed query field");
			}
		}
	}
}

void ValidateHeaders(const std::vector<CompiledHttpHeader> &headers, const char *protocol_name) {
	for (std::size_t index = 0; index < headers.size(); index++) {
		const auto &header = headers[index];
		if (!IsHeaderName(header.name) || header.value.empty() ||
		    header.value.find_first_of("\r\n") != std::string::npos) {
			throw std::invalid_argument(std::string("compiled ") + protocol_name +
			                            " request contains an invalid fixed header");
		}
		if (EqualsAsciiIgnoreCase(header.name, "Authorization")) {
			throw std::invalid_argument(std::string("compiled ") + protocol_name +
			                            " request cannot contain a credential-bearing fixed header");
		}
		for (std::size_t other = index + 1; other < headers.size(); other++) {
			if (EqualsAsciiIgnoreCase(header.name, headers[other].name)) {
				throw std::invalid_argument(std::string("compiled ") + protocol_name +
				                            " request contains a duplicate fixed header");
			}
		}
	}
}

void ValidateRestOperation(const CompiledOperation &operation) {
	const auto &rest = operation.Rest();
	(void)MethodName(rest.method);
	(void)ReplaySafetyName(rest.replay_safety);
	if (rest.retry_enabled || rest.request.origin.port == 0 || rest.request.path.empty() ||
	    rest.request.path.front() != '/' || rest.request.path.find_first_of("?#\r\n") != std::string::npos) {
		throw std::invalid_argument("compiled REST operation contains unsupported authority, path, or retry behavior");
	}
	ValidateQueryParameters(rest.request.query_parameters);
	ValidateHeaders(rest.request.headers, "REST");
	if (rest.response_source == CompiledResponseSource::JSON_PATH_MANY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || rest.records_extractor.empty() ||
		    rest.records_extractor == "$") {
			throw std::invalid_argument("multi-record response source has contradictory cardinality or extraction");
		}
	} else if (rest.response_source == CompiledResponseSource::ROOT_ARRAY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || rest.records_extractor != "$") {
			throw std::invalid_argument("root-array response source has contradictory cardinality or extraction");
		}
	} else if (rest.response_source == CompiledResponseSource::ROOT_OBJECT) {
		if (operation.cardinality != CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS ||
		    rest.records_extractor != "$") {
			throw std::invalid_argument("root-object response source has contradictory cardinality or extraction");
		}
	} else {
		throw std::invalid_argument("compiled REST operation contains an unknown response source");
	}
	internal::ValidatePagination(operation);
}

void AppendQuery(std::ostream &result, const std::vector<CompiledQueryParameter> &parameters) {
	for (std::size_t index = 0; index < parameters.size(); index++) {
		result << (index == 0 ? "" : ",") << parameters[index].name << '=' << parameters[index].encoded_value;
	}
}

void AppendHeaders(std::ostream &result, const std::vector<CompiledHttpHeader> &headers) {
	for (std::size_t index = 0; index < headers.size(); index++) {
		result << (index == 0 ? "" : ",") << headers[index].name << '=' << headers[index].value;
	}
}

const char *SchemeName(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return "http";
	case CompiledUrlScheme::HTTPS:
		return "https";
	}
	throw std::logic_error("compiled operation contains an unknown URL scheme");
}

void AppendOrigin(std::ostream &result, const CompiledHttpOrigin &origin) {
	result << "[scheme:" << SchemeName(origin.scheme) << ",host:" << origin.host.Value() << ",port:" << origin.port
	       << ']';
}

} // namespace

CompiledHttpHost::CompiledHttpHost(std::string value_p) : value(std::move(value_p)) {
	if (!IsCanonicalHost(value)) {
		throw std::invalid_argument("compiled HTTP host is not one exact canonical host component");
	}
}

const std::string &CompiledHttpHost::Value() const {
	return value;
}

CompiledProtocolOperation::CompiledProtocolOperation(CompiledProtocol protocol_p,
                                                     std::shared_ptr<const CompiledRestOperation> rest_p,
                                                     std::shared_ptr<const CompiledGraphqlOperation> graphql_p)
    : protocol(protocol_p), rest(std::move(rest_p)), graphql(std::move(graphql_p)) {
	const bool valid_rest = protocol == CompiledProtocol::REST && rest != nullptr && graphql == nullptr;
	const bool valid_graphql = protocol == CompiledProtocol::GRAPHQL && graphql != nullptr && rest == nullptr;
	if (!valid_rest && !valid_graphql) {
		throw std::invalid_argument("compiled protocol operation tag and payload disagree");
	}
}

CompiledProtocolOperation CompiledProtocolOperation::FromRest(CompiledRestOperation operation) {
	return CompiledProtocolOperation(CompiledProtocol::REST,
	                                 std::make_shared<const CompiledRestOperation>(std::move(operation)), nullptr);
}

CompiledProtocolOperation CompiledProtocolOperation::FromGraphql(CompiledGraphqlOperation operation) {
	return CompiledProtocolOperation(CompiledProtocol::GRAPHQL, nullptr,
	                                 std::make_shared<const CompiledGraphqlOperation>(std::move(operation)));
}

CompiledProtocol CompiledProtocolOperation::Protocol() const {
	return protocol;
}

const CompiledRestOperation &CompiledProtocolOperation::Rest() const {
	if (protocol != CompiledProtocol::REST || rest == nullptr) {
		throw std::logic_error("compiled protocol operation is not REST");
	}
	return *rest;
}

const CompiledGraphqlOperation &CompiledProtocolOperation::Graphql() const {
	if (protocol != CompiledProtocol::GRAPHQL || graphql == nullptr) {
		throw std::logic_error("compiled protocol operation is not GraphQL");
	}
	return *graphql;
}

CompiledOperation::CompiledOperation(std::string name_p, bool fallback_p, CompiledOperationCardinality cardinality_p,
                                     CompiledProtocol protocol_p, CompiledHttpMethod method,
                                     CompiledReplaySafety replay_safety, bool retry_enabled,
                                     CompiledPagination pagination, CompiledRestRequest request,
                                     CompiledResponseSource response_source, std::string records_extractor,
                                     CompiledOperationSelector selector_p)
    : name(std::move(name_p)), fallback(fallback_p), cardinality(cardinality_p), selector(std::move(selector_p)),
      protocol_operation(CompiledProtocolOperation::FromRest(
          CompiledRestOperation {method, replay_safety, retry_enabled, std::move(pagination), std::move(request),
                                 response_source, std::move(records_extractor)})) {
	if (protocol_p != CompiledProtocol::REST) {
		throw std::invalid_argument("REST operation construction requires the REST protocol tag");
	}
}

CompiledOperation::CompiledOperation(std::string name_p, bool fallback_p, CompiledOperationCardinality cardinality_p,
                                     CompiledGraphqlOperation operation, CompiledOperationSelector selector_p)
    : name(std::move(name_p)), fallback(fallback_p), cardinality(cardinality_p), selector(std::move(selector_p)),
      protocol_operation(CompiledProtocolOperation::FromGraphql(std::move(operation))) {
}

CompiledProtocol CompiledOperation::Protocol() const {
	return protocol_operation.Protocol();
}

const CompiledProtocolOperation &CompiledOperation::ProtocolOperation() const {
	return protocol_operation;
}

const CompiledRestOperation &CompiledOperation::Rest() const {
	return protocol_operation.Rest();
}

const CompiledGraphqlOperation &CompiledOperation::Graphql() const {
	return protocol_operation.Graphql();
}

namespace internal {

void ValidateProtocolOperation(const CompiledOperation &operation) {
	switch (operation.Protocol()) {
	case CompiledProtocol::REST:
		ValidateRestOperation(operation);
		return;
	case CompiledProtocol::GRAPHQL:
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY) {
			throw std::invalid_argument("compiled GraphQL operation requires zero-to-many cardinality");
		}
		ValidateHeaders(operation.Graphql().headers, "GraphQL");
		ValidateGraphqlOperationValue(operation.Graphql());
		return;
	}
	throw std::invalid_argument("compiled relation contains an unknown protocol alternative");
}

const CompiledHttpOrigin &OperationOrigin(const CompiledOperation &operation) {
	switch (operation.Protocol()) {
	case CompiledProtocol::REST:
		return operation.Rest().request.origin;
	case CompiledProtocol::GRAPHQL:
		return operation.Graphql().endpoint_origin;
	}
	throw std::logic_error("compiled relation contains an unknown protocol alternative");
}

void AppendProtocolOperation(std::ostream &result, const CompiledOperation &operation) {
	if (operation.Protocol() == CompiledProtocol::GRAPHQL) {
		AppendGraphqlOperation(result, operation.Graphql());
		return;
	}
	const auto &rest = operation.Rest();
	result << "REST:" << MethodName(rest.method) << ':' << ReplaySafetyName(rest.replay_safety) << ";request=origin:";
	AppendOrigin(result, rest.request.origin);
	result << ",path:" << rest.request.path << ",query:[";
	AppendQuery(result, rest.request.query_parameters);
	result << "],headers:[";
	AppendHeaders(result, rest.request.headers);
	result << "];response=source:" << ResponseSourceName(rest.response_source) << ",records:" << rest.records_extractor
	       << ";features=retry:" << (rest.retry_enabled ? "enabled" : "disabled") << ",pagination:";
	AppendPagination(result, rest.pagination);
}

} // namespace internal
} // namespace duckdb_api
