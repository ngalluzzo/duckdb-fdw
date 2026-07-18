#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {

// Connector-owned test access for constructor/type counterexamples. Production
// callers receive no construction API: BuildNativeGithubConnector is the sole
// production friend, while this definition lives only in non-installable test
// support and emits no product symbol.
class ConnectorCatalogTestAccess final {
public:
	static duckdb_api::CompiledAuthenticationPolicy Anonymous() {
		return duckdb_api::CompiledAuthenticationPolicy::Anonymous();
	}

	static duckdb_api::CompiledAuthenticationPolicy RequiredBearer() {
		return duckdb_api::CompiledAuthenticationPolicy::RequiredBearer();
	}

	static duckdb_api::CompiledAuthenticationPolicy ValidateRequiredBearer(std::string logical_credential,
	                                                                       duckdb_api::CompiledRestOrigin destination) {
		std::vector<duckdb_api::CompiledRestOrigin> destinations;
		destinations.push_back(std::move(destination));
		return duckdb_api::CompiledAuthenticationPolicy(
		    duckdb_api::CompiledCredentialRequirement::REQUIRED, std::move(logical_credential),
		    duckdb_api::CompiledAuthenticator::BEARER, duckdb_api::CompiledCredentialPlacement::AUTHORIZATION_HEADER,
		    std::move(destinations));
	}

	static duckdb_api::CompiledRelation Relation(std::string name, std::vector<duckdb_api::CompiledColumn> columns,
	                                             duckdb_api::CompiledOperation operation,
	                                             duckdb_api::CompiledAuthenticationPolicy authentication,
	                                             duckdb_api::CompiledResourceCeilings resource_ceilings) {
		return duckdb_api::CompiledRelation(std::move(name), std::move(columns), std::move(operation),
		                                    std::move(authentication), resource_ceilings);
	}

	static duckdb_api::CompiledConnector Catalog(duckdb_api::CompiledConnectorOrigin origin, std::string connector_name,
	                                             std::string version,
	                                             std::vector<duckdb_api::CompiledRelation> relations,
	                                             duckdb_api::CompiledNetworkPolicy network_policy) {
		return duckdb_api::CompiledConnector(origin, std::move(connector_name), std::move(version),
		                                     std::move(relations), std::move(network_policy));
	}
};

} // namespace duckdb_api_test
