#include "duckdb_api/internal/connector/protocol_operation_declaration.hpp"

#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"
#include "duckdb_api/internal/connector/pagination_declaration.hpp"

#include <ostream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace internal {

bool IsCompiledQueryName(const std::string &value) {
	if (value.empty() || value.size() > 63) {
		return false;
	}
	for (const auto character : value) {
		if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
		      (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '~' ||
		      character == '-')) {
			return false;
		}
	}
	return true;
}

} // namespace internal

namespace {

bool IsAsciiDigit(char value) {
	return value >= '0' && value <= '9';
}

bool IsAsciiLower(char value) {
	return value >= 'a' && value <= 'z';
}

bool IsFormUnreserved(unsigned char value) {
	return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
	       value == '-' || value == '.' || value == '_' || value == '~';
}

bool IsUtf8Continuation(unsigned char value) {
	return value >= 0x80 && value <= 0xbf;
}

bool IsValidUtf8Text(const std::string &value) {
	std::size_t index = 0;
	while (index < value.size()) {
		const auto first = static_cast<unsigned char>(value[index]);
		std::uint32_t codepoint = 0;
		std::size_t width = 0;
		if (first <= 0x7f) {
			codepoint = first;
			width = 1;
		} else if (first >= 0xc2 && first <= 0xdf && index + 1 < value.size() &&
		           IsUtf8Continuation(static_cast<unsigned char>(value[index + 1]))) {
			codepoint = static_cast<std::uint32_t>(first & 0x1f) << 6;
			codepoint |= static_cast<unsigned char>(value[index + 1]) & 0x3f;
			width = 2;
		} else if (first >= 0xe0 && first <= 0xef && index + 2 < value.size()) {
			const auto second = static_cast<unsigned char>(value[index + 1]);
			const auto third = static_cast<unsigned char>(value[index + 2]);
			const bool canonical_second =
			    (first == 0xe0 && second >= 0xa0 && second <= 0xbf) ||
			    (first == 0xed && second >= 0x80 && second <= 0x9f) ||
			    (((first >= 0xe1 && first <= 0xec) || (first >= 0xee && first <= 0xef)) && IsUtf8Continuation(second));
			if (!canonical_second || !IsUtf8Continuation(third)) {
				return false;
			}
			codepoint = static_cast<std::uint32_t>(first & 0x0f) << 12;
			codepoint |= static_cast<std::uint32_t>(second & 0x3f) << 6;
			codepoint |= third & 0x3f;
			width = 3;
		} else if (first >= 0xf0 && first <= 0xf4 && index + 3 < value.size()) {
			const auto second = static_cast<unsigned char>(value[index + 1]);
			const auto third = static_cast<unsigned char>(value[index + 2]);
			const auto fourth = static_cast<unsigned char>(value[index + 3]);
			const bool canonical_second = (first == 0xf0 && second >= 0x90 && second <= 0xbf) ||
			                              (first == 0xf4 && second >= 0x80 && second <= 0x8f) ||
			                              (first >= 0xf1 && first <= 0xf3 && IsUtf8Continuation(second));
			if (!canonical_second || !IsUtf8Continuation(third) || !IsUtf8Continuation(fourth)) {
				return false;
			}
			codepoint = static_cast<std::uint32_t>(first & 0x07) << 18;
			codepoint |= static_cast<std::uint32_t>(second & 0x3f) << 12;
			codepoint |= static_cast<std::uint32_t>(third & 0x3f) << 6;
			codepoint |= fourth & 0x3f;
			width = 4;
		} else {
			return false;
		}
		if (codepoint <= 0x1f || (codepoint >= 0x7f && codepoint <= 0x9f)) {
			return false;
		}
		index += width;
	}
	return true;
}

std::string FormUrlEncode(const std::string &value) {
	static const char *hex = "0123456789ABCDEF";
	std::string result;
	for (const auto character : value) {
		const auto byte = static_cast<unsigned char>(character);
		if (IsFormUnreserved(byte)) {
			result.push_back(static_cast<char>(byte));
		} else if (byte == ' ') {
			result.push_back('+');
		} else {
			result.push_back('%');
			result.push_back(hex[(byte >> 4) & 0x0f]);
			result.push_back(hex[byte & 0x0f]);
		}
	}
	return result;
}

std::vector<std::string> StructuralRecordSegments(const std::string &extractor) {
	std::vector<std::string> result;
	if (extractor.size() < 3 || extractor[0] != '$' || extractor[1] != '.') {
		return result;
	}
	std::size_t begin = 2;
	while (begin < extractor.size()) {
		const auto end = extractor.find('.', begin);
		auto segment = extractor.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
		if (end == std::string::npos && segment.size() > 3 && segment.compare(segment.size() - 3, 3, "[*]") == 0) {
			segment.resize(segment.size() - 3);
		}
		if (segment.empty()) {
			return {};
		}
		result.push_back(std::move(segment));
		if (end == std::string::npos) {
			break;
		}
		begin = end + 1;
	}
	return result;
}

bool IsStructuralSegment(const std::string &segment) {
	if (segment.empty() || !((segment.front() >= 'A' && segment.front() <= 'Z') ||
	                         (segment.front() >= 'a' && segment.front() <= 'z') || segment.front() == '_')) {
		return false;
	}
	for (const auto value : segment) {
		if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
		      value == '_')) {
			return false;
		}
	}
	return true;
}

std::string ExtractorFromSegments(const std::vector<std::string> &segments) {
	std::string result = "$";
	for (const auto &segment : segments) {
		if (!IsStructuralSegment(segment)) {
			return "";
		}
		result += '.';
		result += segment;
	}
	return result;
}

bool MatchesRecordSegments(const std::string &extractor, const std::vector<std::string> &segments) {
	const auto structural = ExtractorFromSegments(segments);
	return !segments.empty() && (extractor == structural || extractor == structural + "[*]");
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
	if (parameters.size() > 64) {
		throw std::invalid_argument("compiled REST request exceeds the query field limit");
	}
	for (std::size_t index = 0; index < parameters.size(); index++) {
		const auto &parameter = parameters[index];
		if (!internal::IsCompiledQueryName(parameter.name)) {
			throw std::invalid_argument("compiled REST request contains an invalid query field name");
		}
		switch (parameter.encoding) {
		case CompiledQueryEncoding::FORM_URLENCODED:
			break;
		default:
			throw std::invalid_argument("compiled REST query field has an unknown encoding");
		}
		switch (parameter.source) {
		case CompiledQueryValueSource::FIXED:
			if (parameter.encoded_value.find_first_of("&=?#\r\n") != std::string::npos ||
			    !parameter.source_id.empty() || parameter.omit_when_unbound || parameter.omit_when_null ||
			    !parameter.HasDecodedValue()) {
				throw std::invalid_argument("compiled REST fixed query field is contradictory");
			}
			if (parameter.encoded_value != EncodeCompiledQueryScalar(parameter.DecodedValue(), parameter.encoding)) {
				throw std::invalid_argument("compiled REST fixed query field disagrees with its decoded value");
			}
			break;
		case CompiledQueryValueSource::RELATION_INPUT:
			if (!parameter.encoded_value.empty() || parameter.source_id.empty() || !parameter.omit_when_unbound ||
			    !parameter.omit_when_null || parameter.HasDecodedValue()) {
				throw std::invalid_argument("compiled REST relation-input query field is contradictory");
			}
			break;
		case CompiledQueryValueSource::CONDITIONAL_INPUT:
			if (!parameter.encoded_value.empty() || parameter.source_id.empty() || !parameter.omit_when_unbound ||
			    parameter.omit_when_null || parameter.HasDecodedValue()) {
				throw std::invalid_argument("compiled REST conditional query field is contradictory");
			}
			break;
		case CompiledQueryValueSource::PAGE_SIZE:
		case CompiledQueryValueSource::PAGE_NUMBER:
			if (!parameter.HasDecodedValue() || parameter.DecodedValue().Type() != CompiledScalarType::BIGINT ||
			    parameter.DecodedValue().IsNull() || parameter.DecodedValue().Bigint() <= 0 ||
			    parameter.encoded_value != EncodeCompiledQueryScalar(parameter.DecodedValue(), parameter.encoding) ||
			    !parameter.source_id.empty() || parameter.omit_when_unbound || parameter.omit_when_null) {
				throw std::invalid_argument("compiled REST page query field is contradictory");
			}
			break;
		default:
			throw std::invalid_argument("compiled REST query field has an unknown value source");
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
		    rest.records_extractor == "$" ||
		    !MatchesRecordSegments(rest.records_extractor, rest.records_extractor_segments)) {
			throw std::invalid_argument("multi-record response source has contradictory cardinality or extraction");
		}
	} else if (rest.response_source == CompiledResponseSource::ROOT_ARRAY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || rest.records_extractor != "$" ||
		    !rest.records_extractor_segments.empty()) {
			throw std::invalid_argument("root-array response source has contradictory cardinality or extraction");
		}
	} else if (rest.response_source == CompiledResponseSource::ROOT_OBJECT) {
		if (operation.cardinality != CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS ||
		    rest.records_extractor != "$" || !rest.records_extractor_segments.empty()) {
			throw std::invalid_argument("root-object response source has contradictory cardinality or extraction");
		}
	} else {
		throw std::invalid_argument("compiled REST operation contains an unknown response source");
	}
	internal::ValidatePagination(operation);
}

void AppendQuery(std::ostream &result, const std::vector<CompiledQueryParameter> &parameters) {
	for (std::size_t index = 0; index < parameters.size(); index++) {
		const auto &parameter = parameters[index];
		result << (index == 0 ? "" : ",") << parameter.name << '=';
		switch (parameter.source) {
		case CompiledQueryValueSource::FIXED:
			result << "fixed." << CompiledScalarTypeName(parameter.DecodedValue().Type()) << ':'
			       << parameter.encoded_value;
			break;
		case CompiledQueryValueSource::RELATION_INPUT:
			result << "input." << parameter.source_id << ":omit_unbound_null";
			break;
		case CompiledQueryValueSource::CONDITIONAL_INPUT:
			result << "conditional." << parameter.source_id << ":omit_unbound";
			break;
		case CompiledQueryValueSource::PAGE_SIZE:
			result << "page_size.BIGINT:" << parameter.encoded_value;
			break;
		case CompiledQueryValueSource::PAGE_NUMBER:
			result << "page_number.BIGINT:" << parameter.encoded_value;
			break;
		}
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

std::string EncodeCompiledQueryScalar(const CompiledScalarValue &value, CompiledQueryEncoding encoding) {
	if (value.IsNull()) {
		throw std::invalid_argument("compiled query scalar encoding requires a concrete value");
	}
	std::string decoded;
	switch (value.Type()) {
	case CompiledScalarType::BOOLEAN:
		decoded = value.Boolean() ? "true" : "false";
		break;
	case CompiledScalarType::BIGINT:
		decoded = std::to_string(value.Bigint());
		break;
	case CompiledScalarType::VARCHAR:
		decoded = value.Varchar();
		if (!IsValidUtf8Text(decoded)) {
			throw std::invalid_argument("compiled query VARCHAR is not valid control-free UTF-8");
		}
		break;
	default:
		throw std::invalid_argument("compiled query scalar has an unknown type");
	}
	switch (encoding) {
	case CompiledQueryEncoding::FORM_URLENCODED:
		return FormUrlEncode(decoded);
	}
	throw std::invalid_argument("compiled query scalar has an unknown encoding");
}

CompiledQueryParameter::CompiledQueryParameter(std::string name_p, std::string encoded_value_p)
    : name(std::move(name_p)), encoded_value(std::move(encoded_value_p)), source(CompiledQueryValueSource::FIXED),
      source_id(), encoding(CompiledQueryEncoding::FORM_URLENCODED), omit_when_unbound(false), omit_when_null(false),
      decoded_value() {
}

CompiledQueryParameter::CompiledQueryParameter(std::string name_p, CompiledQueryValueSource source_p,
                                               std::string source_id_p, bool omit_when_unbound_p, bool omit_when_null_p)
    : name(std::move(name_p)), encoded_value(), source(source_p), source_id(std::move(source_id_p)),
      encoding(CompiledQueryEncoding::FORM_URLENCODED), omit_when_unbound(omit_when_unbound_p),
      omit_when_null(omit_when_null_p), decoded_value() {
}

CompiledQueryParameter::CompiledQueryParameter(std::string name_p, CompiledQueryValueSource source_p,
                                               CompiledScalarValue decoded_value_p)
    : name(std::move(name_p)), encoded_value(), source(source_p), source_id(),
      encoding(CompiledQueryEncoding::FORM_URLENCODED), omit_when_unbound(false), omit_when_null(false),
      decoded_value(std::make_shared<const CompiledScalarValue>(std::move(decoded_value_p))) {
	encoded_value = EncodeCompiledQueryScalar(*decoded_value, encoding);
}

bool CompiledQueryParameter::HasDecodedValue() const {
	return static_cast<bool>(decoded_value);
}

const CompiledScalarValue &CompiledQueryParameter::DecodedValue() const {
	if (!decoded_value) {
		throw std::logic_error("compiled query field has no decoded scalar value");
	}
	return *decoded_value;
}

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
                                 response_source, records_extractor, StructuralRecordSegments(records_extractor)})) {
	if (protocol_p != CompiledProtocol::REST) {
		throw std::invalid_argument("REST operation construction requires the REST protocol tag");
	}
}

CompiledOperation::CompiledOperation(std::string name_p, bool fallback_p, CompiledOperationCardinality cardinality_p,
                                     CompiledPagination pagination, CompiledRestRequest request,
                                     CompiledResponseSource response_source, std::string records_extractor,
                                     std::vector<std::string> records_extractor_segments,
                                     CompiledOperationSelector selector_p)
    : name(std::move(name_p)), fallback(fallback_p), cardinality(cardinality_p), selector(std::move(selector_p)),
      protocol_operation(CompiledProtocolOperation::FromRest(CompiledRestOperation {
          CompiledHttpMethod::GET, CompiledReplaySafety::SAFE, false, std::move(pagination), std::move(request),
          response_source, std::move(records_extractor), std::move(records_extractor_segments)})) {
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

std::vector<std::string> ParseLegacyJsonExtractorSegments(const std::string &extractor) {
	return StructuralRecordSegments(extractor);
}

bool MatchesStructuralFieldExtractor(const std::string &extractor, const std::vector<std::string> &segments) {
	return !segments.empty() && extractor == ExtractorFromSegments(segments);
}

bool MatchesStructuralCollectionExtractor(const std::string &extractor, const std::vector<std::string> &segments) {
	return MatchesRecordSegments(extractor, segments);
}

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
