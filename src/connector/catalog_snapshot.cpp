#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/internal/connector/predicate_declaration.hpp"
#include "duckdb_api/internal/connector/protocol_operation_declaration.hpp"
#include "duckdb_api/internal/connector/resource_ceiling_declaration.hpp"

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
	case CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS:
		return "exactly_one_on_success";
	}
	throw std::logic_error("compiled connector contains an unknown operation cardinality");
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

const char *AuthorityState(bool enabled) {
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

void AppendSchema(std::ostringstream &result, const std::vector<CompiledColumn> &columns) {
	for (std::size_t index = 0; index < columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		const auto &column = columns[index];
		result << column.name << ':' << column.logical_type << (column.nullable ? '?' : '!') << ':' << column.extractor;
	}
}

void AppendOrigin(std::ostringstream &result, const CompiledHttpOrigin &origin) {
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

void AppendSelector(std::ostringstream &result, const CompiledOperationSelector &selector) {
	result << ";selector=required:[";
	AppendStrings(result, selector.RequiredInputs());
	result << "],any:[";
	for (std::size_t index = 0; index < selector.AnyInputSets().size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << '[';
		AppendStrings(result, selector.AnyInputSets()[index]);
		result << ']';
	}
	result << "],forbidden:[";
	AppendStrings(result, selector.ForbiddenInputs());
	result << "],priority:" << selector.Priority();
}

void AppendOperation(std::ostringstream &result, const CompiledOperation &operation) {
	result << "operation=" << operation.name << ':' << (operation.fallback ? "fallback" : "selected") << ':'
	       << CardinalityName(operation.cardinality) << ':';
	internal::AppendProtocolOperation(result, operation);
	if (!operation.fallback || !operation.selector.RequiredInputs().empty() ||
	    !operation.selector.AnyInputSets().empty() || !operation.selector.ForbiddenInputs().empty() ||
	    operation.selector.Priority() != 0) {
		AppendSelector(result, operation.selector);
	}
}

} // namespace

std::string CompiledRelation::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "relation=" << Name() << ";schema=";
	AppendSchema(result, Columns());
	result << ";predicate_mappings=[";
	internal::AppendPredicateMappings(result, PredicateMappings());
	if (HasSingleOperation()) {
		result << "];";
		AppendOperation(result, Operation());
	} else {
		result << "];operations=[";
		for (std::size_t index = 0; index < Operations().size(); index++) {
			if (index > 0) {
				result << ',';
			}
			result << '{';
			AppendOperation(result, Operations()[index]);
			result << '}';
		}
		result << ']';
	}
	result << ";authentication=";
	AppendAuthentication(result, Authentication());
	result << ";ceilings=";
	internal::AppendResourceCeilings(result, ResourceCeilings());
	return result.str();
}

std::string CompiledConnector::Snapshot() const {
	std::ostringstream result;
	result.imbue(std::locale::classic());
	result << "origin=" << OriginName(Origin()) << ";connector=" << ConnectorName() << ";version=" << Version()
	       << ";network=schemes:[";
	AppendStrings(result, NetworkPolicy().allowed_schemes);
	result << "],hosts:[";
	AppendStrings(result, NetworkPolicy().allowed_hosts);
	result << "],redirects:" << AuthorityState(NetworkPolicy().redirects_enabled)
	       << ",private:" << AuthorityState(NetworkPolicy().private_addresses_enabled)
	       << ",link_local:" << AuthorityState(NetworkPolicy().link_local_addresses_enabled)
	       << ",loopback:" << AuthorityState(NetworkPolicy().loopback_addresses_enabled)
	       << ",max_response_bytes:" << NetworkPolicy().max_response_bytes << ";relations=[";
	for (std::size_t index = 0; index < Relations().size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << '{' << Relations()[index].Snapshot() << '}';
	}
	result << ']';
	return result.str();
}

} // namespace duckdb_api
