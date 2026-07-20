#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/internal/connector/operation_selector_declaration.hpp"
#include "duckdb_api/internal/connector/pagination_declaration.hpp"
#include "duckdb_api/internal/connector/predicate_declaration.hpp"
#include "duckdb_api/internal/connector/resource_ceiling_declaration.hpp"

#include <limits>
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
	if (!IsIdentifier(operation.name) || operation.retry_enabled) {
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
                                   std::vector<CompiledPredicateMapping> predicate_mappings_p,
                                   CompiledOperation operation_p, CompiledAuthenticationPolicy authentication_p,
                                   CompiledResourceCeilings resource_ceilings_p)
    : CompiledRelation(std::move(name_p), std::move(columns_p), std::move(predicate_mappings_p),
                       std::vector<CompiledOperation> {std::move(operation_p)}, std::move(authentication_p),
                       std::move(resource_ceilings_p)) {
}

CompiledRelation::CompiledRelation(std::string name_p, std::vector<CompiledColumn> columns_p,
                                   std::vector<CompiledPredicateMapping> predicate_mappings_p,
                                   std::vector<CompiledOperation> operations_p,
                                   CompiledAuthenticationPolicy authentication_p,
                                   CompiledResourceCeilings resource_ceilings_p)
    : name(std::move(name_p)), columns(std::move(columns_p)), predicate_mappings(std::move(predicate_mappings_p)),
      operations(std::move(operations_p)), authentication(std::move(authentication_p)),
      resource_ceilings(std::move(resource_ceilings_p)) {
	if (!IsIdentifier(name) || columns.empty() || operations.empty()) {
		throw std::invalid_argument("compiled relation contains incomplete identity, schema, or operation catalog");
	}
	for (std::size_t index = 0; index < columns.size(); index++) {
		ValidateColumn(columns[index]);
		for (std::size_t other = index + 1; other < columns.size(); other++) {
			if (columns[index].name == columns[other].name) {
				throw std::invalid_argument("compiled relation contains a duplicate column");
			}
		}
	}
	std::size_t fallback_count = 0;
	for (std::size_t index = 0; index < operations.size(); index++) {
		ValidateOperation(operations[index]);
		fallback_count += operations[index].fallback ? 1 : 0;
		for (std::size_t other = index + 1; other < operations.size(); other++) {
			if (operations[index].name == operations[other].name) {
				throw std::invalid_argument("compiled relation contains a duplicate operation identifier");
			}
		}
		ValidateResourceCeilings(operations[index], resource_ceilings);
		if (operations[index].cardinality == CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS &&
		    (resource_ceilings.MaxRecordsPerPage() != 1 || resource_ceilings.MaxRecordsPerScan() != 1)) {
			throw std::invalid_argument("exactly-one relation must carry a distinct one-record ceiling");
		}
	}
	if (fallback_count > 1) {
		throw std::invalid_argument("compiled relation contains multiple fallback operations");
	}
	internal::ValidatePredicateMappings(name, columns, operations, authentication, predicate_mappings);
	for (const auto &operation : operations) {
		internal::ValidateOperationSelectorReferences(operation, predicate_mappings);
	}

	if (authentication.Requirement() == CompiledCredentialRequirement::NONE) {
		for (const auto &operation : operations) {
			if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY) {
				throw std::invalid_argument("native anonymous relation has unsupported source cardinality");
			}
		}
		return;
	}
	const auto destination = authentication.Destination();
	for (const auto &operation : operations) {
		if (destination == nullptr || !OriginsEqual(*destination, operation.request.origin) ||
		    (operation.cardinality == CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS &&
		     !operation.request.query_parameters.empty())) {
			throw std::invalid_argument("authenticated relation contains inconsistent operation and credential policy");
		}
	}
}

const std::string &CompiledRelation::Name() const {
	return name;
}

const std::vector<CompiledColumn> &CompiledRelation::Columns() const {
	return columns;
}

const std::vector<CompiledPredicateMapping> &CompiledRelation::PredicateMappings() const {
	return predicate_mappings;
}

const std::vector<CompiledOperation> &CompiledRelation::Operations() const {
	return operations;
}

bool CompiledRelation::HasSingleOperation() const {
	return operations.size() == 1;
}

const CompiledOperation &CompiledRelation::Operation() const {
	if (!HasSingleOperation()) {
		throw std::logic_error("compiled relation does not contain exactly one operation");
	}
	return operations[0];
}

const CompiledAuthenticationPolicy &CompiledRelation::Authentication() const {
	return authentication;
}

const CompiledResourceCeilings &CompiledRelation::ResourceCeilings() const {
	return resource_ceilings;
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
		for (const auto &operation : relations[index].Operations()) {
			const auto &origin_value = operation.request.origin;
			if (!Contains(network_policy.allowed_schemes, UrlSchemeName(origin_value.scheme)) ||
			    !Contains(network_policy.allowed_hosts, origin_value.host.Value())) {
				throw std::invalid_argument("compiled relation destination is outside connector network policy");
			}
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

} // namespace duckdb_api
