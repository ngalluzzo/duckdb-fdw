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

	static duckdb_api::CompiledOperationSelector OperationSelector(std::vector<std::string> required_inputs,
	                                                               std::vector<std::vector<std::string>> any_input_sets,
	                                                               std::vector<std::string> forbidden_inputs,
	                                                               std::int32_t priority = 0) {
		return duckdb_api::CompiledOperationSelector(std::move(required_inputs), std::move(any_input_sets),
		                                             std::move(forbidden_inputs), priority);
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

	static duckdb_api::CompiledPredicateMapping
	PredicateMapping(std::string column_name, duckdb_api::CompiledPredicateOperator predicate_operator,
	                 duckdb_api::CompiledPredicateLiteral literal, std::string operation_name,
	                 duckdb_api::CompiledPredicateInputPlacement input_placement, std::string remote_input_name,
	                 std::string encoded_remote_value, duckdb_api::CompiledPredicateAccuracy accuracy,
	                 duckdb_api::CompiledPredicateProofIdentity proof_identity,
	                 duckdb_api::CompiledPredicateBaseDomain base_domain,
	                 duckdb_api::CompiledPredicateOccurrencePreservation occurrence_preservation,
	                 duckdb_api::CompiledPredicateEncodingCapability encoding_capability) {
		return duckdb_api::CompiledPredicateMapping(
		    std::move(column_name), predicate_operator, literal, std::move(operation_name), input_placement,
		    std::move(remote_input_name), std::move(encoded_remote_value), accuracy, proof_identity, base_domain,
		    occurrence_preservation, encoding_capability);
	}

	static duckdb_api::CompiledRelation
	Relation(std::string name, std::vector<duckdb_api::CompiledColumn> columns, duckdb_api::CompiledOperation operation,
	         duckdb_api::CompiledAuthenticationPolicy authentication,
	         duckdb_api::CompiledResourceCeilings resource_ceilings,
	         std::vector<duckdb_api::CompiledPredicateMapping> predicate_mappings = {}) {
		return duckdb_api::CompiledRelation(std::move(name), std::move(columns), std::move(predicate_mappings),
		                                    std::move(operation), std::move(authentication), resource_ceilings);
	}

	static duckdb_api::CompiledRelation
	Relation(std::string name, std::vector<duckdb_api::CompiledColumn> columns,
	         std::vector<duckdb_api::CompiledOperation> operations,
	         duckdb_api::CompiledAuthenticationPolicy authentication,
	         duckdb_api::CompiledResourceCeilings resource_ceilings,
	         std::vector<duckdb_api::CompiledPredicateMapping> predicate_mappings = {}) {
		return duckdb_api::CompiledRelation(std::move(name), std::move(columns), std::move(predicate_mappings),
		                                    std::move(operations), std::move(authentication), resource_ceilings);
	}

	static duckdb_api::CompiledConnector Catalog(duckdb_api::CompiledConnectorOrigin origin, std::string connector_name,
	                                             std::string version,
	                                             std::vector<duckdb_api::CompiledRelation> relations,
	                                             duckdb_api::CompiledNetworkPolicy network_policy) {
		return duckdb_api::CompiledConnector(origin, std::move(connector_name), std::move(version),
		                                     std::move(relations), std::move(network_policy));
	}

	// Produces the exact native catalog with predicate declarations removed.
	// This private test-only composition proves capability absence without
	// changing any other provider or Runtime authority.
	static duckdb_api::CompiledConnector WithoutPredicateMappings(duckdb_api::CompiledConnector connector) {
		for (auto &relation : connector.relations) {
			relation.predicate_mappings.clear();
		}
		return connector;
	}
};

} // namespace duckdb_api_test
