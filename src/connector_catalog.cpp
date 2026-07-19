#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/internal/connector_pagination.hpp"
#include "duckdb_api/internal/connector_resource_ceilings.hpp"

#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsAsciiLower(char value) {
	return value >= 'a' && value <= 'z';
}

bool IsAsciiDigit(char value) {
	return value >= '0' && value <= '9';
}

bool IsAsciiLowerOrDigit(char value) {
	return IsAsciiLower(value) || IsAsciiDigit(value);
}

char AsciiLower(char value) {
	if (value >= 'A' && value <= 'Z') {
		return static_cast<char>(value - 'A' + 'a');
	}
	return value;
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

bool IsIdentifier(const std::string &value) {
	if (value.empty() || !IsAsciiLower(value.front())) {
		return false;
	}
	for (const auto character : value) {
		if (!IsAsciiLowerOrDigit(character) && character != '_') {
			return false;
		}
	}
	return true;
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
	case CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS:
		return "exactly_one_on_success";
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

const char *ResponseSourceName(CompiledResponseSource source) {
	switch (source) {
	case CompiledResponseSource::JSON_PATH_MANY:
		return "json_path_many";
	case CompiledResponseSource::ROOT_ARRAY:
		return "root_array";
	case CompiledResponseSource::ROOT_OBJECT:
		return "root_object";
	}
	throw std::logic_error("compiled connector contains an unknown response source");
}

const char *RequirementName(CompiledCredentialRequirement requirement) {
	switch (requirement) {
	case CompiledCredentialRequirement::NONE:
		return "none";
	case CompiledCredentialRequirement::REQUIRED:
		return "required";
	}
	throw std::logic_error("compiled connector contains an unknown credential requirement");
}

const char *AuthenticatorName(CompiledAuthenticator authenticator) {
	switch (authenticator) {
	case CompiledAuthenticator::NONE:
		return "none";
	case CompiledAuthenticator::BEARER:
		return "bearer";
	}
	throw std::logic_error("compiled connector contains an unknown authenticator");
}

const char *PlacementName(CompiledCredentialPlacement placement) {
	switch (placement) {
	case CompiledCredentialPlacement::NONE:
		return "none";
	case CompiledCredentialPlacement::AUTHORIZATION_HEADER:
		return "Authorization";
	}
	throw std::logic_error("compiled connector contains an unknown credential placement");
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

bool OriginsEqual(const CompiledRestOrigin &left, const CompiledRestOrigin &right) {
	return left.scheme == right.scheme && left.host.Value() == right.host.Value() && left.port == right.port;
}

bool Contains(const std::vector<std::string> &values, const std::string &expected) {
	for (const auto &value : values) {
		if (value == expected) {
			return true;
		}
	}
	return false;
}

void ValidateColumn(const CompiledColumn &column) {
	if (!IsIdentifier(column.name) || column.extractor.empty() || column.extractor.front() != '$') {
		throw std::invalid_argument("compiled relation contains an invalid column declaration");
	}
	if (column.logical_type != "BIGINT" && column.logical_type != "VARCHAR" && column.logical_type != "BOOLEAN") {
		throw std::invalid_argument("compiled relation contains an unsupported logical type");
	}
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

void ValidateHeaders(const std::vector<CompiledHttpHeader> &headers) {
	for (std::size_t index = 0; index < headers.size(); index++) {
		const auto &header = headers[index];
		if (!IsHeaderName(header.name) || header.value.empty() ||
		    header.value.find_first_of("\r\n") != std::string::npos) {
			throw std::invalid_argument("compiled REST request contains an invalid fixed header");
		}
		if (EqualsAsciiIgnoreCase(header.name, "Authorization")) {
			throw std::invalid_argument("compiled REST request cannot contain a credential-bearing fixed header");
		}
		for (std::size_t other = index + 1; other < headers.size(); other++) {
			if (EqualsAsciiIgnoreCase(header.name, headers[other].name)) {
				throw std::invalid_argument("compiled REST request contains a duplicate fixed header");
			}
		}
	}
}

void ValidateOperation(const CompiledOperation &operation) {
	if (!IsIdentifier(operation.name) || !operation.fallback || operation.retry_enabled) {
		throw std::invalid_argument("compiled relation contains an unsupported base operation");
	}
	(void)ProtocolName(operation.protocol);
	(void)MethodName(operation.method);
	(void)ReplaySafetyName(operation.replay_safety);
	(void)UrlSchemeName(operation.request.origin.scheme);
	if (operation.request.origin.port == 0 || operation.request.path.empty() || operation.request.path.front() != '/' ||
	    operation.request.path.find_first_of("?#\r\n") != std::string::npos) {
		throw std::invalid_argument("compiled REST request contains invalid structural authority or path");
	}
	ValidateQueryParameters(operation.request.query_parameters);
	ValidateHeaders(operation.request.headers);

	if (operation.response_source == CompiledResponseSource::JSON_PATH_MANY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY ||
		    operation.records_extractor.empty() || operation.records_extractor == "$") {
			throw std::invalid_argument("multi-record response source has contradictory cardinality or extraction");
		}
	} else if (operation.response_source == CompiledResponseSource::ROOT_ARRAY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || operation.records_extractor != "$") {
			throw std::invalid_argument("root-array response source has contradictory cardinality or extraction");
		}
	} else if (operation.response_source == CompiledResponseSource::ROOT_OBJECT) {
		if (operation.cardinality != CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS ||
		    operation.records_extractor != "$") {
			throw std::invalid_argument("root-object response source has contradictory cardinality or extraction");
		}
	} else {
		throw std::invalid_argument("compiled relation contains an unknown response source");
	}
	internal::ValidatePagination(operation);
}

void ValidateResourceCeilings(const CompiledOperation &operation, const CompiledResourceCeilings &ceilings) {
	internal::ValidateResourceCeilingsValue(ceilings);

	if (operation.pagination.Strategy() == CompiledPaginationStrategy::DISABLED) {
		if (ceilings.MaxRecordsPerScan() != ceilings.MaxRecordsPerPage() ||
		    (ceilings.HasResponseByteNarrowing() &&
		     ceilings.MaxResponseBytesPerScan() != ceilings.MaxResponseBytesPerPage())) {
			throw std::invalid_argument("unpaginated relation contains different page and scan ceilings");
		}
		return;
	}

	if (!ceilings.HasResponseByteNarrowing() || operation.pagination.PageSize() > ceilings.MaxRecordsPerPage()) {
		throw std::invalid_argument("paginated relation lacks bounded page resources");
	}
	const auto max_pages = operation.pagination.MaxPagesPerScan();
	if (ceilings.MaxRecordsPerPage() > std::numeric_limits<std::uint64_t>::max() / max_pages ||
	    ceilings.MaxResponseBytesPerPage() > std::numeric_limits<std::uint64_t>::max() / max_pages) {
		throw std::invalid_argument("compiled pagination resource envelope overflows");
	}
	if (ceilings.MaxRecordsPerScan() > ceilings.MaxRecordsPerPage() * max_pages ||
	    ceilings.MaxResponseBytesPerScan() > ceilings.MaxResponseBytesPerPage() * max_pages) {
		throw std::invalid_argument("compiled pagination scan ceiling exceeds its bounded page sequence");
	}
}

void ValidateNetworkPolicy(const CompiledNetworkPolicy &policy) {
	if (policy.allowed_schemes.empty() || policy.allowed_hosts.empty() || policy.max_response_bytes == 0) {
		throw std::invalid_argument("compiled connector contains an incomplete network policy");
	}
	for (std::size_t index = 0; index < policy.allowed_schemes.size(); index++) {
		const auto &scheme = policy.allowed_schemes[index];
		if (scheme != "http" && scheme != "https") {
			throw std::invalid_argument("compiled connector contains an unsupported network scheme");
		}
		for (std::size_t other = index + 1; other < policy.allowed_schemes.size(); other++) {
			if (scheme == policy.allowed_schemes[other]) {
				throw std::invalid_argument("compiled connector contains a duplicate network scheme");
			}
		}
	}
	for (std::size_t index = 0; index < policy.allowed_hosts.size(); index++) {
		CompiledRestHost validated(policy.allowed_hosts[index]);
		(void)validated;
		for (std::size_t other = index + 1; other < policy.allowed_hosts.size(); other++) {
			if (policy.allowed_hosts[index] == policy.allowed_hosts[other]) {
				throw std::invalid_argument("compiled connector contains a duplicate network host");
			}
		}
	}
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

void AppendOrigin(std::ostringstream &result, const CompiledRestOrigin &origin) {
	result << "[scheme:" << UrlSchemeName(origin.scheme) << ",host:" << origin.host.Value() << ",port:" << origin.port
	       << ']';
}

void AppendAuthentication(std::ostringstream &result, const CompiledAuthenticationPolicy &authentication) {
	result << "requirement:" << RequirementName(authentication.Requirement()) << ",logical_credential:";
	if (authentication.Requirement() == CompiledCredentialRequirement::NONE) {
		result << "none";
	} else {
		result << authentication.LogicalCredential();
	}
	result << ",authenticator:" << AuthenticatorName(authentication.Authenticator()) << ",destination:";
	const auto destination = authentication.Destination();
	if (destination == nullptr) {
		result << "none";
	} else {
		AppendOrigin(result, *destination);
	}
	result << ",placement:" << PlacementName(authentication.Placement());
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

CompiledAuthenticationPolicy::CompiledAuthenticationPolicy(CompiledCredentialRequirement requirement_p,
                                                           std::string logical_credential_p,
                                                           CompiledAuthenticator authenticator_p,
                                                           CompiledCredentialPlacement placement_p,
                                                           std::vector<CompiledRestOrigin> destinations_p)
    : requirement(requirement_p), logical_credential(std::move(logical_credential_p)), authenticator(authenticator_p),
      placement(placement_p), destinations(std::move(destinations_p)) {
	if (requirement == CompiledCredentialRequirement::NONE) {
		if (!logical_credential.empty() || authenticator != CompiledAuthenticator::NONE ||
		    placement != CompiledCredentialPlacement::NONE || !destinations.empty()) {
			throw std::invalid_argument("anonymous relation contains contradictory credential policy");
		}
		return;
	}
	if (requirement != CompiledCredentialRequirement::REQUIRED || logical_credential != "token" ||
	    authenticator != CompiledAuthenticator::BEARER ||
	    placement != CompiledCredentialPlacement::AUTHORIZATION_HEADER || destinations.size() != 1 ||
	    destinations[0].scheme != CompiledUrlScheme::HTTPS || destinations[0].host.Value() != "api.github.com" ||
	    destinations[0].port != 443) {
		throw std::invalid_argument("required relation contains an unsupported credential policy");
	}
}

CompiledAuthenticationPolicy CompiledAuthenticationPolicy::Anonymous() {
	return CompiledAuthenticationPolicy(CompiledCredentialRequirement::NONE, "", CompiledAuthenticator::NONE,
	                                    CompiledCredentialPlacement::NONE, {});
}

CompiledAuthenticationPolicy CompiledAuthenticationPolicy::RequiredBearer() {
	std::vector<CompiledRestOrigin> destinations;
	destinations.push_back({CompiledUrlScheme::HTTPS, CompiledRestHost("api.github.com"), 443});
	return CompiledAuthenticationPolicy(CompiledCredentialRequirement::REQUIRED, "token", CompiledAuthenticator::BEARER,
	                                    CompiledCredentialPlacement::AUTHORIZATION_HEADER, std::move(destinations));
}

CompiledCredentialRequirement CompiledAuthenticationPolicy::Requirement() const {
	return requirement;
}

const std::string &CompiledAuthenticationPolicy::LogicalCredential() const {
	return logical_credential;
}

CompiledAuthenticator CompiledAuthenticationPolicy::Authenticator() const {
	return authenticator;
}

CompiledCredentialPlacement CompiledAuthenticationPolicy::Placement() const {
	return placement;
}

const CompiledRestOrigin *CompiledAuthenticationPolicy::Destination() const {
	return destinations.empty() ? nullptr : &destinations[0];
}

CompiledRelation::CompiledRelation(std::string name_p, std::vector<CompiledColumn> columns_p,
                                   CompiledOperation operation_p, CompiledAuthenticationPolicy authentication_p,
                                   CompiledResourceCeilings resource_ceilings_p)
    : name(std::move(name_p)), columns(std::move(columns_p)), operation(std::move(operation_p)),
      authentication(std::move(authentication_p)), resource_ceilings(std::move(resource_ceilings_p)) {
	if (!IsIdentifier(name) || columns.empty()) {
		throw std::invalid_argument("compiled relation contains incomplete identity or schema");
	}
	for (std::size_t index = 0; index < columns.size(); index++) {
		ValidateColumn(columns[index]);
		for (std::size_t other = index + 1; other < columns.size(); other++) {
			if (columns[index].name == columns[other].name) {
				throw std::invalid_argument("compiled relation contains a duplicate column");
			}
		}
	}
	ValidateOperation(operation);
	ValidateResourceCeilings(operation, resource_ceilings);
	if (operation.cardinality == CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS &&
	    (resource_ceilings.MaxRecordsPerPage() != 1 || resource_ceilings.MaxRecordsPerScan() != 1)) {
		throw std::invalid_argument("exactly-one relation must carry a distinct one-record ceiling");
	}

	if (authentication.Requirement() == CompiledCredentialRequirement::NONE) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY) {
			throw std::invalid_argument("native anonymous relation has unsupported source cardinality");
		}
		return;
	}
	const auto destination = authentication.Destination();
	if (destination == nullptr || !OriginsEqual(*destination, operation.request.origin) ||
	    (operation.cardinality == CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS &&
	     !operation.request.query_parameters.empty())) {
		throw std::invalid_argument("authenticated relation contains inconsistent operation and credential policy");
	}
}

const std::string &CompiledRelation::Name() const {
	return name;
}

const std::vector<CompiledColumn> &CompiledRelation::Columns() const {
	return columns;
}

const CompiledOperation &CompiledRelation::Operation() const {
	return operation;
}

const CompiledAuthenticationPolicy &CompiledRelation::Authentication() const {
	return authentication;
}

const CompiledResourceCeilings &CompiledRelation::ResourceCeilings() const {
	return resource_ceilings;
}

std::string CompiledRelation::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "relation=" << name << ";schema=";
	AppendSchema(result, columns);
	result << ";operation=" << operation.name << ':' << (operation.fallback ? "fallback" : "selected") << ':'
	       << CardinalityName(operation.cardinality) << ':' << ProtocolName(operation.protocol) << ':'
	       << MethodName(operation.method) << ':' << ReplaySafetyName(operation.replay_safety) << ";request=origin:";
	AppendOrigin(result, operation.request.origin);
	result << ",path:" << operation.request.path << ",query:[";
	AppendQuery(result, operation.request.query_parameters);
	result << "],headers:[";
	AppendHeaders(result, operation.request.headers);
	result << "];response=source:" << ResponseSourceName(operation.response_source)
	       << ",records:" << operation.records_extractor << ";features=retry:" << EnabledState(operation.retry_enabled)
	       << ",pagination:";
	internal::AppendPagination(result, operation.pagination);
	result << ";authentication=";
	AppendAuthentication(result, authentication);
	result << ";ceilings=";
	internal::AppendResourceCeilings(result, resource_ceilings);
	return result.str();
}

CompiledConnector::CompiledConnector(CompiledConnectorOrigin origin_p, std::string connector_name_p,
                                     std::string version_p, std::vector<CompiledRelation> relations_p,
                                     CompiledNetworkPolicy network_policy_p)
    : origin(origin_p), connector_name(std::move(connector_name_p)), version(std::move(version_p)),
      relations(std::move(relations_p)), network_policy(std::move(network_policy_p)) {
	(void)OriginName(origin);
	if (!IsIdentifier(connector_name) || version.empty() || relations.empty()) {
		throw std::invalid_argument("compiled connector contains incomplete identity or relation catalog");
	}
	ValidateNetworkPolicy(network_policy);
	for (std::size_t index = 0; index < relations.size(); index++) {
		for (std::size_t other = index + 1; other < relations.size(); other++) {
			if (relations[index].Name() == relations[other].Name()) {
				throw std::invalid_argument("compiled connector contains a duplicate relation identifier");
			}
		}
		const auto &origin_value = relations[index].Operation().request.origin;
		if (!Contains(network_policy.allowed_schemes, UrlSchemeName(origin_value.scheme)) ||
		    !Contains(network_policy.allowed_hosts, origin_value.host.Value())) {
			throw std::invalid_argument("compiled relation destination is outside connector network policy");
		}
		const auto credential_destination = relations[index].Authentication().Destination();
		if (credential_destination != nullptr &&
		    (!Contains(network_policy.allowed_schemes, UrlSchemeName(credential_destination->scheme)) ||
		     !Contains(network_policy.allowed_hosts, credential_destination->host.Value()))) {
			throw std::invalid_argument("compiled credential destination is outside connector network policy");
		}
		const auto &ceilings = relations[index].ResourceCeilings();
		if (ceilings.HasResponseByteNarrowing() &&
		    ceilings.MaxResponseBytesPerPage() > network_policy.max_response_bytes) {
			throw std::invalid_argument("compiled relation response ceiling widens connector policy");
		}
	}
}

CompiledConnectorOrigin CompiledConnector::Origin() const {
	return origin;
}

const std::string &CompiledConnector::ConnectorName() const {
	return connector_name;
}

const std::string &CompiledConnector::Version() const {
	return version;
}

const std::vector<CompiledRelation> &CompiledConnector::Relations() const {
	return relations;
}

const CompiledRelation *CompiledConnector::FindRelation(const std::string &relation_name) const {
	for (const auto &relation : relations) {
		if (relation.Name() == relation_name) {
			return &relation;
		}
	}
	return nullptr;
}

const CompiledNetworkPolicy &CompiledConnector::NetworkPolicy() const {
	return network_policy;
}

std::string CompiledConnector::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "origin=" << OriginName(origin) << ";connector=" << connector_name << ";version=" << version
	       << ";network=schemes:[";
	AppendStrings(result, network_policy.allowed_schemes);
	result << "],hosts:[";
	AppendStrings(result, network_policy.allowed_hosts);
	result << "],redirects:" << AuthorityState(network_policy.redirects_enabled)
	       << ",private:" << AuthorityState(network_policy.private_addresses_enabled)
	       << ",link_local:" << AuthorityState(network_policy.link_local_addresses_enabled)
	       << ",loopback:" << AuthorityState(network_policy.loopback_addresses_enabled)
	       << ",max_response_bytes:" << network_policy.max_response_bytes << ";relations=[";
	for (std::size_t index = 0; index < relations.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << '{' << relations[index].Snapshot() << '}';
	}
	result << ']';
	return result.str();
}

} // namespace duckdb_api
