#pragma once

#include "duckdb_api/compiled_package_generation.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

// Connector-private validated construction boundary shared by native metadata
// and the future package compiler. It produces complete immutable values; it
// exposes no setters, partial state, source parser, or consumer construction
// API. Every method is deterministic and performs no I/O.
class CompiledModelBuilder final {
public:
	static CompiledScalarValue Null(CompiledScalarType type);
	static CompiledScalarValue Boolean(bool value);
	static CompiledScalarValue Bigint(std::int64_t value);
	static CompiledScalarValue Varchar(std::string value);

	static CompiledInputDefault NoDefault();
	static CompiledInputDefault Default(CompiledScalarValue value);
	static CompiledRelationInput Input(std::string name, CompiledScalarType type, bool nullable,
	                                   CompiledInputDefault default_value);

	static CompiledColumn Column(std::string name, CompiledScalarType type, bool nullable, std::string extractor);
	static CompiledColumn Column(std::string name, CompiledScalarType type, bool nullable, std::string extractor,
	                             std::vector<std::string> extractor_segments);
	static CompiledPagination DisabledPagination();
	static CompiledPagination LinkPagination(std::string page_size_parameter, std::uint64_t page_size,
	                                         std::string page_number_parameter, std::uint64_t first_page,
	                                         std::uint64_t page_increment, std::uint64_t max_pages_per_scan);
	// RESPONSE_NEXT_URL pagination: identical page-number/page-size/ceiling
	// fields to LinkPagination plus the declared body continuation path.
	static CompiledPagination ResponseNextPagination(std::string next_url_path, std::string page_size_parameter,
	                                                 std::uint64_t page_size, std::string page_number_parameter,
	                                                 std::uint64_t first_page, std::uint64_t page_increment,
	                                                 std::uint64_t max_pages_per_scan);
	static CompiledQueryParameter FixedQueryParameter(std::string name, CompiledScalarValue decoded_value);
	static CompiledQueryParameter RelationInputQueryParameter(std::string name, std::string input_id);
	static CompiledQueryParameter ConditionalInputQueryParameter(std::string name, std::string conditional_id);
	static CompiledQueryParameter PageSizeQueryParameter(std::string name, std::uint64_t value);
	static CompiledQueryParameter PageNumberQueryParameter(std::string name, std::uint64_t value);
	static CompiledRequiredInputReference RelationInputReference(std::string id);
	static CompiledRequiredInputReference ConditionalInputReference(std::string id);
	// duckdb_api/v1 admits required references only. There is intentionally no
	// author priority, alternative-input, or forbidden-input parameter.
	static CompiledOperationSelector
	V1OperationSelector(std::vector<CompiledRequiredInputReference> required_input_references);
	static CompiledAuthenticationPolicy AnonymousAuthentication();
	static CompiledAuthenticationPolicy BearerAuthentication(std::string logical_credential,
	                                                         std::vector<CompiledHttpOrigin> destinations);
	static CompiledResourceCeilings Resources(std::uint64_t max_response_bytes_per_page,
	                                          std::uint64_t max_response_bytes_per_scan,
	                                          std::uint64_t max_records_per_page, std::uint64_t max_records_per_scan,
	                                          std::uint64_t max_extracted_string_bytes);
	static CompiledResourceCeilings UnpaginatedResources(std::uint64_t max_records,
	                                                     std::uint64_t max_extracted_string_bytes);
	static CompiledOperation RestOperation(std::string name, bool fallback, CompiledOperationCardinality cardinality,
	                                       CompiledPagination pagination, CompiledRestRequest request,
	                                       CompiledResponseSource response_source, std::string records_extractor,
	                                       std::vector<std::string> records_extractor_segments,
	                                       CompiledOperationSelector selector);
	static CompiledOperation GraphqlOperation(std::string name, bool fallback, CompiledGraphqlOperation operation,
	                                          CompiledOperationSelector selector);
	static CompiledPredicateMapping PackagePredicate(std::string name, std::string column_name,
	                                                 CompiledScalarValue literal, std::string operation_name,
	                                                 std::string remote_input_name, std::string encoded_remote_value,
	                                                 CompiledPredicateAccuracy accuracy, std::string proof_identity,
	                                                 std::string base_domain, std::string matching_fixture,
	                                                 std::string false_or_null_fixture, std::string duplicates_fixture);
	static CompiledGraphqlLiteral GraphqlNullLiteral();
	static CompiledGraphqlLiteral GraphqlBooleanLiteral(bool value);
	static CompiledGraphqlLiteral GraphqlIntegerLiteral(std::int64_t value);
	static CompiledGraphqlLiteral GraphqlStringLiteral(std::string value);
	static CompiledGraphqlLiteral GraphqlEnumLiteral(std::string value);
	static CompiledGraphqlLiteral GraphqlListLiteral(std::vector<std::shared_ptr<const CompiledGraphqlLiteral>> items);
	static CompiledGraphqlObjectField GraphqlObjectField(std::string name, CompiledGraphqlLiteral value);
	static CompiledGraphqlLiteral GraphqlObjectLiteral(std::vector<CompiledGraphqlObjectField> fields);
	static CompiledGraphqlFixedArgument GraphqlFixedArgument(std::string name, CompiledGraphqlLiteral value);
	static CompiledGraphqlRecipeVariable GraphqlRecipeVariable(std::string name, CompiledGraphqlVariableType type,
	                                                           CompiledGraphqlRecipeVariableRole role,
	                                                           std::string argument_name);
	static CompiledGraphqlSelection GraphqlSelection(std::string column_name, std::vector<std::string> field_path);
	static std::shared_ptr<const CompiledGraphqlQueryRecipe>
	GraphqlQueryRecipe(CompiledGraphqlDocumentIdentity identity, std::string operation_name,
	                   std::vector<CompiledGraphqlRecipeVariable> variables, std::vector<std::string> root_path,
	                   std::vector<CompiledGraphqlFixedArgument> fixed_arguments, std::string nodes_field,
	                   std::vector<CompiledGraphqlSelection> selections, std::string page_info_field,
	                   std::string has_next_page_field, std::string end_cursor_field);
	static CompiledRelation
	Relation(std::string name, std::vector<CompiledColumn> columns, std::vector<CompiledRelationInput> inputs,
	         std::vector<CompiledPredicateMapping> predicate_mappings, std::vector<CompiledOperation> operations,
	         CompiledAuthenticationPolicy authentication, CompiledResourceCeilings resource_ceilings);
	static CompiledConnector Connector(CompiledConnectorOrigin origin, std::string connector_name, std::string version,
	                                   std::vector<CompiledRelation> relations, CompiledNetworkPolicy network_policy);

	static CompiledPackageIdentity PackageIdentity(std::string spec_identifier, std::string connector_id,
	                                               std::string package_version, std::string package_digest);
	static CompiledPackageGeneration PackageGeneration(CompiledPackageIdentity identity, CompiledConnector connector);
};

} // namespace internal
} // namespace duckdb_api
