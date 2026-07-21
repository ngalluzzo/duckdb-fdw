#pragma once

#include "duckdb_api/connector_catalog.hpp"
#include "duckdb_api/internal/connector/compiled_model_builder.hpp"

#include <algorithm>
#include <memory>
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

	static duckdb_api::CompiledQueryParameter FixedQuery(std::string name, std::string decoded_value) {
		return duckdb_api::internal::CompiledModelBuilder::FixedQueryParameter(
		    std::move(name), duckdb_api::internal::CompiledModelBuilder::Varchar(std::move(decoded_value)));
	}

	static duckdb_api::CompiledQueryParameter PageSizeQuery(std::string name, std::uint64_t value) {
		return duckdb_api::internal::CompiledModelBuilder::PageSizeQueryParameter(std::move(name), value);
	}

	static duckdb_api::CompiledQueryParameter PageNumberQuery(std::string name, std::uint64_t value) {
		return duckdb_api::internal::CompiledModelBuilder::PageNumberQueryParameter(std::move(name), value);
	}

	// Temporary compatibility bridge for Semantics-owned controlled fixtures.
	// Delete this factory with the legacy CompiledOperationSelector accessors
	// after those fixtures consume structural tagged references.
	static duckdb_api::CompiledOperationSelector OperationSelector(std::vector<std::string> required_inputs,
	                                                               std::vector<std::vector<std::string>> any_input_sets,
	                                                               std::vector<std::string> forbidden_inputs,
	                                                               std::int32_t priority = 0) {
		return duckdb_api::CompiledOperationSelector(std::move(required_inputs), std::move(any_input_sets),
		                                             std::move(forbidden_inputs), priority);
	}

	static duckdb_api::CompiledOperation
	GraphqlOperation(std::string name, bool fallback, duckdb_api::CompiledOperationCardinality cardinality,
	                 duckdb_api::CompiledGraphqlOperation operation,
	                 duckdb_api::CompiledOperationSelector selector = duckdb_api::CompiledOperationSelector()) {
		return duckdb_api::CompiledOperation(std::move(name), fallback, cardinality, std::move(operation),
		                                     std::move(selector));
	}

	static duckdb_api::CompiledOperation RestOperation(const duckdb_api::CompiledOperation &common,
	                                                   duckdb_api::CompiledRestOperation rest,
	                                                   duckdb_api::CompiledOperationSelector selector) {
		return duckdb_api::CompiledOperation(
		    common.name, common.fallback, common.cardinality, duckdb_api::CompiledProtocol::REST, rest.method,
		    rest.replay_safety, rest.retry_enabled, std::move(rest.pagination), std::move(rest.request),
		    rest.response_source, std::move(rest.records_extractor), std::move(selector));
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
		    "controlled_predicate", std::move(column_name), predicate_operator, literal, std::move(operation_name),
		    input_placement, std::move(remote_input_name), std::move(encoded_remote_value), accuracy, proof_identity,
		    base_domain, occurrence_preservation, encoding_capability);
	}

	static duckdb_api::CompiledPredicateMapping
	PackagePredicateMapping(std::string column_name, duckdb_api::CompiledScalarValue literal,
	                        std::string operation_name, std::string remote_input_name, std::string encoded_remote_value,
	                        duckdb_api::CompiledPredicateAccuracy accuracy, std::string proof_identity,
	                        std::string base_domain, std::string matching_fixture, std::string false_or_null_fixture,
	                        std::string duplicates_fixture) {
		return duckdb_api::CompiledPredicateMapping(
		    "controlled_predicate", std::move(column_name), std::move(literal), std::move(operation_name),
		    std::move(remote_input_name), std::move(encoded_remote_value), accuracy, std::move(proof_identity),
		    std::move(base_domain), std::move(matching_fixture), std::move(false_or_null_fixture),
		    std::move(duplicates_fixture));
	}

	static duckdb_api::CompiledRelation
	Relation(std::string name, std::vector<duckdb_api::CompiledColumn> columns, duckdb_api::CompiledOperation operation,
	         duckdb_api::CompiledAuthenticationPolicy authentication,
	         duckdb_api::CompiledResourceCeilings resource_ceilings,
	         std::vector<duckdb_api::CompiledPredicateMapping> predicate_mappings = {}) {
		return duckdb_api::CompiledRelation(std::move(name), std::move(columns), std::move(predicate_mappings),
		                                    std::move(operation), std::move(authentication), resource_ceilings);
	}

	static duckdb_api::CompiledRelation Relation(
	    std::string name, std::vector<duckdb_api::CompiledColumn> columns,
	    std::vector<duckdb_api::CompiledRelationInput> inputs, std::vector<duckdb_api::CompiledOperation> operations,
	    duckdb_api::CompiledAuthenticationPolicy authentication, duckdb_api::CompiledResourceCeilings resource_ceilings,
	    std::vector<duckdb_api::CompiledPredicateMapping> predicate_mappings = {}) {
		return duckdb_api::CompiledRelation(std::move(name), std::move(columns), std::move(inputs),
		                                    std::move(predicate_mappings), std::move(operations),
		                                    std::move(authentication), resource_ceilings);
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

	// Replaces one already-validated GraphQL payload after canonical fixture
	// construction. This intentionally creates an invalid test value without
	// making invalid production construction possible.
	static duckdb_api::CompiledConnector WithInvalidGraphqlOperation(duckdb_api::CompiledConnector connector,
	                                                                 duckdb_api::CompiledGraphqlOperation operation) {
		connector.relations.at(0).operations.at(0).protocol_operation.graphql =
		    std::make_shared<const duckdb_api::CompiledGraphqlOperation>(std::move(operation));
		return connector;
	}

	static duckdb_api::CompiledConnector WithInvalidGraphqlOperation(duckdb_api::CompiledConnector connector,
	                                                                 const std::string &relation_name,
	                                                                 const std::string &operation_name,
	                                                                 duckdb_api::CompiledGraphqlOperation operation) {
		for (auto &relation : connector.relations) {
			if (relation.name != relation_name) {
				continue;
			}
			for (auto &candidate : relation.operations) {
				if (candidate.name == operation_name) {
					candidate.protocol_operation.graphql =
					    std::make_shared<const duckdb_api::CompiledGraphqlOperation>(std::move(operation));
					return connector;
				}
			}
		}
		throw std::invalid_argument("GraphQL test mutation names an unknown relation or operation");
	}

	static duckdb_api::CompiledGraphqlOperation
	WithUnknownGraphqlRecipeIdentity(duckdb_api::CompiledGraphqlOperation operation) {
		auto recipe = operation.QueryRecipe();
		recipe.identity = static_cast<duckdb_api::CompiledGraphqlDocumentIdentity>(255);
		operation.query_recipe = std::make_shared<const duckdb_api::CompiledGraphqlQueryRecipe>(std::move(recipe));
		return operation;
	}

	static duckdb_api::CompiledGraphqlLiteral RawGraphqlInteger(std::string scalar) {
		return duckdb_api::CompiledGraphqlLiteral(duckdb_api::CompiledGraphqlLiteralKind::INTEGER, std::move(scalar),
		                                          {}, {});
	}

	static duckdb_api::CompiledGraphqlLiteral NestedGraphqlList(std::size_t wrappers) {
		std::shared_ptr<const duckdb_api::CompiledGraphqlLiteral> value =
		    std::make_shared<const duckdb_api::CompiledGraphqlLiteral>(
		        duckdb_api::internal::CompiledModelBuilder::GraphqlNullLiteral());
		for (std::size_t index = 0; index < wrappers; index++) {
			std::vector<std::shared_ptr<const duckdb_api::CompiledGraphqlLiteral>> items {value};
			value = std::make_shared<const duckdb_api::CompiledGraphqlLiteral>(
			    duckdb_api::CompiledGraphqlLiteral(duckdb_api::CompiledGraphqlLiteralKind::LIST, "", std::move(items),
			                                       std::vector<duckdb_api::CompiledGraphqlObjectField>()));
		}
		return *value;
	}

	static duckdb_api::CompiledGraphqlLiteral FlatGraphqlNullList(std::size_t items_count) {
		std::vector<std::shared_ptr<const duckdb_api::CompiledGraphqlLiteral>> items;
		items.reserve(items_count);
		for (std::size_t index = 0; index < items_count; index++) {
			items.push_back(std::make_shared<const duckdb_api::CompiledGraphqlLiteral>(
			    duckdb_api::internal::CompiledModelBuilder::GraphqlNullLiteral()));
		}
		return duckdb_api::CompiledGraphqlLiteral(duckdb_api::CompiledGraphqlLiteralKind::LIST, "", std::move(items),
		                                          {});
	}

	static duckdb_api::CompiledGraphqlLiteral GraphqlLiteralNodeTree(std::size_t node_count) {
		if (node_count == 0) {
			throw std::invalid_argument("GraphQL literal node tree requires a root node");
		}
		if (node_count == 1) {
			return duckdb_api::internal::CompiledModelBuilder::GraphqlNullLiteral();
		}
		const auto remaining = node_count - 1;
		const auto child_count = (remaining + 4096) / 4097;
		if (child_count > 4096) {
			throw std::invalid_argument("GraphQL literal node tree exceeds its two-level fixture shape");
		}
		auto leaves_remaining = remaining - child_count;
		const auto null_value = std::make_shared<const duckdb_api::CompiledGraphqlLiteral>(
		    duckdb_api::internal::CompiledModelBuilder::GraphqlNullLiteral());
		std::vector<std::shared_ptr<const duckdb_api::CompiledGraphqlLiteral>> children;
		children.reserve(child_count);
		for (std::size_t child = 0; child < child_count; child++) {
			const auto leaves = std::min<std::size_t>(4096, leaves_remaining);
			std::vector<std::shared_ptr<const duckdb_api::CompiledGraphqlLiteral>> items(leaves, null_value);
			children.push_back(std::make_shared<const duckdb_api::CompiledGraphqlLiteral>(
			    duckdb_api::CompiledGraphqlLiteral(duckdb_api::CompiledGraphqlLiteralKind::LIST, "", std::move(items),
			                                       std::vector<duckdb_api::CompiledGraphqlObjectField>())));
			leaves_remaining -= leaves;
		}
		if (leaves_remaining != 0) {
			throw std::logic_error("GraphQL literal node tree fixture did not distribute all nodes");
		}
		return duckdb_api::CompiledGraphqlLiteral(duckdb_api::CompiledGraphqlLiteralKind::LIST, "", std::move(children),
		                                          {});
	}

	static duckdb_api::CompiledGraphqlQueryRecipe
	WithFirstGraphqlFixedArgument(const duckdb_api::CompiledGraphqlQueryRecipe &source,
	                              duckdb_api::CompiledGraphqlLiteral literal) {
		auto result = source;
		result.fixed_arguments.at(0).value =
		    std::make_shared<const duckdb_api::CompiledGraphqlLiteral>(std::move(literal));
		return result;
	}

	// Changes one relation schema fact only after the canonical fixture passed
	// production validation. The resulting value is confined to test targets.
	static duckdb_api::CompiledConnector WithInvalidGraphqlColumnType(duckdb_api::CompiledConnector connector,
	                                                                  std::size_t column_index,
	                                                                  std::string logical_type) {
		connector.relations.at(0).columns.at(column_index).logical_type = std::move(logical_type);
		return connector;
	}

	static duckdb_api::CompiledConnector WithInvalidGraphqlColumnNullability(duckdb_api::CompiledConnector connector,
	                                                                         std::size_t column_index, bool nullable) {
		connector.relations.at(0).columns.at(column_index).nullable = nullable;
		return connector;
	}

	static duckdb_api::CompiledConnector
	WithInvalidRelationResources(duckdb_api::CompiledConnector connector, const std::string &relation_name,
	                             std::uint64_t response_bytes_per_page, std::uint64_t response_bytes_per_scan,
	                             std::uint64_t records_per_page, std::uint64_t records_per_scan,
	                             std::uint64_t extracted_string_bytes) {
		for (auto &relation : connector.relations) {
			if (relation.name != relation_name) {
				continue;
			}
			auto &resources = relation.resource_ceilings;
			resources.has_response_byte_narrowing = true;
			resources.max_response_bytes_per_page = response_bytes_per_page;
			resources.max_response_bytes_per_scan = response_bytes_per_scan;
			resources.max_records_per_page = records_per_page;
			resources.max_records_per_scan = records_per_scan;
			resources.max_extracted_string_bytes = extracted_string_bytes;
			return connector;
		}
		throw std::invalid_argument("resource test mutation names an unknown relation");
	}
};

} // namespace duckdb_api_test
