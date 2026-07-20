#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/internal/connector/compiled_model_builder.hpp"

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

bool IsIdentifier(const std::string &value) {
	if (value.empty() || value.size() > 63 || !IsAsciiLower(value.front())) {
		return false;
	}
	for (const auto character : value) {
		if (!IsAsciiLower(character) && !IsAsciiDigit(character) && character != '_') {
			return false;
		}
	}
	return true;
}

bool IsCanonicalVersionComponent(const std::string &component) {
	if (component.empty() || (component.size() > 1 && component.front() == '0')) {
		return false;
	}
	std::uint64_t value = 0;
	for (const auto character : component) {
		if (!IsAsciiDigit(character)) {
			return false;
		}
		const auto digit = static_cast<std::uint64_t>(character - '0');
		if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10) {
			return false;
		}
		value = value * 10 + digit;
	}
	return true;
}

bool IsCanonicalPackageVersion(const std::string &version) {
	std::size_t first = version.find('.');
	if (first == std::string::npos) {
		return false;
	}
	std::size_t second = version.find('.', first + 1);
	if (second == std::string::npos || version.find('.', second + 1) != std::string::npos) {
		return false;
	}
	return IsCanonicalVersionComponent(version.substr(0, first)) &&
	       IsCanonicalVersionComponent(version.substr(first + 1, second - first - 1)) &&
	       IsCanonicalVersionComponent(version.substr(second + 1));
}

bool IsPackageDigest(const std::string &digest) {
	static const std::string prefix = "sha256.";
	if (digest.size() != prefix.size() + 64 || digest.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}
	for (std::size_t index = prefix.size(); index < digest.size(); index++) {
		const auto character = digest[index];
		if (!IsAsciiDigit(character) && (character < 'a' || character > 'f')) {
			return false;
		}
	}
	return true;
}

CompiledRegistrationAuthentication RegistrationAuthentication(const CompiledAuthenticationPolicy &authentication) {
	switch (authentication.Requirement()) {
	case CompiledCredentialRequirement::NONE:
		return CompiledRegistrationAuthentication::ANONYMOUS;
	case CompiledCredentialRequirement::REQUIRED:
		return CompiledRegistrationAuthentication::LOGICAL_SECRET_REQUIRED;
	}
	throw std::logic_error("compiled relation contains an unknown Query authentication shape");
}

} // namespace

namespace internal {

struct CompiledPackageGenerationState {
	CompiledPackageGenerationState(CompiledPackageIdentity identity_p, CompiledConnector connector_p)
	    : identity(std::move(identity_p)), connector(std::move(connector_p)) {
	}

	CompiledPackageIdentity identity;
	CompiledConnector connector;
};

} // namespace internal

CompiledPackageIdentity::CompiledPackageIdentity(std::string spec_identifier_p, std::string connector_id_p,
                                                 std::string package_version_p, std::string package_digest_p)
    : spec_identifier(std::move(spec_identifier_p)), connector_id(std::move(connector_id_p)),
      package_version(std::move(package_version_p)), package_digest(std::move(package_digest_p)) {
	if (spec_identifier != "duckdb_api/v1" || !IsIdentifier(connector_id) ||
	    !IsCanonicalPackageVersion(package_version) || !IsPackageDigest(package_digest)) {
		throw std::invalid_argument("compiled package contains an invalid stable identity");
	}
}

const std::string &CompiledPackageIdentity::SpecIdentifier() const {
	return spec_identifier;
}

const std::string &CompiledPackageIdentity::ConnectorId() const {
	return connector_id;
}

const std::string &CompiledPackageIdentity::PackageVersion() const {
	return package_version;
}

const std::string &CompiledPackageIdentity::PackageDigest() const {
	return package_digest;
}

CompiledGenerationHandle::CompiledGenerationHandle(
    std::shared_ptr<const internal::CompiledPackageGenerationState> state_p)
    : state(std::move(state_p)) {
	if (!state) {
		throw std::invalid_argument("compiled generation handle cannot be empty");
	}
}

bool CompiledGenerationHandle::IsValid() const {
	return static_cast<bool>(state);
}

bool CompiledGenerationHandle::IsSameGeneration(const CompiledGenerationHandle &other) const {
	return state == other.state;
}

CompiledRegistrationColumn::CompiledRegistrationColumn(std::string name_p, CompiledScalarType type_p, bool nullable_p)
    : name(std::move(name_p)), type(type_p), nullable(nullable_p) {
}

const std::string &CompiledRegistrationColumn::Name() const {
	return name;
}

CompiledScalarType CompiledRegistrationColumn::Type() const {
	return type;
}

bool CompiledRegistrationColumn::Nullable() const {
	return nullable;
}

CompiledRegistrationRelation::CompiledRegistrationRelation(std::string name_p,
                                                           std::vector<CompiledRegistrationColumn> columns_p,
                                                           std::vector<CompiledRelationInput> inputs_p,
                                                           CompiledRegistrationAuthentication authentication_p)
    : name(std::move(name_p)), columns(std::move(columns_p)), inputs(std::move(inputs_p)),
      authentication(authentication_p) {
}

const std::string &CompiledRegistrationRelation::Name() const {
	return name;
}

const std::vector<CompiledRegistrationColumn> &CompiledRegistrationRelation::Columns() const {
	return columns;
}

const std::vector<CompiledRelationInput> &CompiledRegistrationRelation::Inputs() const {
	return inputs;
}

CompiledRegistrationAuthentication CompiledRegistrationRelation::Authentication() const {
	return authentication;
}

CompiledQueryRegistrationView::CompiledQueryRegistrationView(CompiledPackageIdentity identity_p,
                                                             std::vector<CompiledRegistrationRelation> relations_p,
                                                             CompiledGenerationHandle generation_handle_p)
    : identity(std::move(identity_p)), relations(std::move(relations_p)),
      generation_handle(std::move(generation_handle_p)) {
}

const CompiledPackageIdentity &CompiledQueryRegistrationView::Identity() const {
	return identity;
}

const std::vector<CompiledRegistrationRelation> &CompiledQueryRegistrationView::Relations() const {
	return relations;
}

const CompiledGenerationHandle &CompiledQueryRegistrationView::GenerationHandle() const {
	return generation_handle;
}

CompiledPackageGeneration::CompiledPackageGeneration(
    std::shared_ptr<const internal::CompiledPackageGenerationState> state_p)
    : state(std::move(state_p)) {
	if (!state) {
		throw std::invalid_argument("compiled package generation cannot be empty");
	}
}

const CompiledPackageIdentity &CompiledPackageGeneration::Identity() const {
	return state->identity;
}

const CompiledConnector &CompiledPackageGeneration::Connector() const {
	return state->connector;
}

CompiledQueryRegistrationView CompiledPackageGeneration::QueryRegistration() const {
	std::vector<CompiledRegistrationRelation> relations;
	relations.reserve(state->connector.Relations().size());
	for (const auto &relation : state->connector.Relations()) {
		std::vector<CompiledRegistrationColumn> columns;
		columns.reserve(relation.Columns().size());
		for (const auto &column : relation.Columns()) {
			columns.push_back(CompiledRegistrationColumn(column.name, column.ScalarType(), column.nullable));
		}
		relations.push_back(CompiledRegistrationRelation(relation.Name(), std::move(columns), relation.Inputs(),
		                                                 RegistrationAuthentication(relation.Authentication())));
	}
	return CompiledQueryRegistrationView(state->identity, std::move(relations), OpaqueHandle());
}

CompiledGenerationHandle CompiledPackageGeneration::OpaqueHandle() const {
	return CompiledGenerationHandle(state);
}

namespace internal {

CompiledScalarValue CompiledModelBuilder::Null(CompiledScalarType type) {
	return CompiledScalarValue(type, true, false, 0, "");
}

CompiledScalarValue CompiledModelBuilder::Boolean(bool value) {
	return CompiledScalarValue(CompiledScalarType::BOOLEAN, false, value, 0, "");
}

CompiledScalarValue CompiledModelBuilder::Bigint(std::int64_t value) {
	return CompiledScalarValue(CompiledScalarType::BIGINT, false, false, value, "");
}

CompiledScalarValue CompiledModelBuilder::Varchar(std::string value) {
	return CompiledScalarValue(CompiledScalarType::VARCHAR, false, false, 0, std::move(value));
}

CompiledInputDefault CompiledModelBuilder::NoDefault() {
	return CompiledInputDefault();
}

CompiledInputDefault CompiledModelBuilder::Default(CompiledScalarValue value) {
	return CompiledInputDefault(std::move(value));
}

CompiledRelationInput CompiledModelBuilder::Input(std::string name, CompiledScalarType type, bool nullable,
                                                  CompiledInputDefault default_value) {
	return CompiledRelationInput(std::move(name), type, nullable, std::move(default_value));
}

CompiledColumn CompiledModelBuilder::Column(std::string name, CompiledScalarType type, bool nullable,
                                            std::string extractor) {
	return CompiledColumn(std::move(name), type, nullable, std::move(extractor));
}

CompiledPagination CompiledModelBuilder::DisabledPagination() {
	return CompiledPagination::Disabled();
}

CompiledAuthenticationPolicy CompiledModelBuilder::AnonymousAuthentication() {
	return CompiledAuthenticationPolicy::Anonymous();
}

CompiledResourceCeilings CompiledModelBuilder::UnpaginatedResources(std::uint64_t max_records,
                                                                    std::uint64_t max_extracted_string_bytes) {
	return CompiledResourceCeilings(max_records, max_extracted_string_bytes);
}

CompiledRelation CompiledModelBuilder::Relation(std::string name, std::vector<CompiledColumn> columns,
                                                std::vector<CompiledRelationInput> inputs,
                                                std::vector<CompiledPredicateMapping> predicate_mappings,
                                                std::vector<CompiledOperation> operations,
                                                CompiledAuthenticationPolicy authentication,
                                                CompiledResourceCeilings resource_ceilings) {
	return CompiledRelation(std::move(name), std::move(columns), std::move(inputs), std::move(predicate_mappings),
	                        std::move(operations), std::move(authentication), std::move(resource_ceilings));
}

CompiledConnector CompiledModelBuilder::Connector(CompiledConnectorOrigin origin, std::string connector_name,
                                                  std::string version, std::vector<CompiledRelation> relations,
                                                  CompiledNetworkPolicy network_policy) {
	return CompiledConnector(origin, std::move(connector_name), std::move(version), std::move(relations),
	                         std::move(network_policy));
}

CompiledPackageIdentity CompiledModelBuilder::PackageIdentity(std::string spec_identifier, std::string connector_id,
                                                              std::string package_version, std::string package_digest) {
	return CompiledPackageIdentity(std::move(spec_identifier), std::move(connector_id), std::move(package_version),
	                               std::move(package_digest));
}

CompiledPackageGeneration CompiledModelBuilder::PackageGeneration(CompiledPackageIdentity identity,
                                                                  CompiledConnector connector) {
	if (connector.Origin() != CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA ||
	    connector.ConnectorName() != identity.ConnectorId() || connector.Version() != identity.PackageVersion()) {
		throw std::invalid_argument("compiled generation identity disagrees with its connector metadata");
	}
	return CompiledPackageGeneration(
	    std::make_shared<const CompiledPackageGenerationState>(std::move(identity), std::move(connector)));
}

} // namespace internal
} // namespace duckdb_api
