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
	static duckdb_api::CompiledPagination DisabledPagination() {
		return duckdb_api::CompiledPagination::Disabled();
	}

	static duckdb_api::CompiledPagination SequentialLink(std::string page_size_parameter, std::uint64_t page_size,
	                                                     std::string page_number_parameter, std::uint64_t first_page,
	                                                     std::uint64_t page_increment,
	                                                     std::uint64_t max_pages_per_scan) {
		return duckdb_api::CompiledPagination(std::move(page_size_parameter), page_size,
		                                      std::move(page_number_parameter), first_page, page_increment,
		                                      max_pages_per_scan);
	}

	static duckdb_api::CompiledResourceCeilings UnpaginatedResources(std::uint64_t max_records,
	                                                                 std::uint64_t max_extracted_string_bytes) {
		return duckdb_api::CompiledResourceCeilings(max_records, max_extracted_string_bytes);
	}

	static duckdb_api::CompiledResourceCeilings PaginatedResources(std::uint64_t max_response_bytes_per_page,
	                                                               std::uint64_t max_response_bytes_per_scan,
	                                                               std::uint64_t max_records_per_page,
	                                                               std::uint64_t max_records_per_scan,
	                                                               std::uint64_t max_extracted_string_bytes) {
		return duckdb_api::CompiledResourceCeilings(max_response_bytes_per_page, max_response_bytes_per_scan,
		                                            max_records_per_page, max_records_per_scan,
		                                            max_extracted_string_bytes);
	}

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
