#pragma once

#include "duckdb_api/compiled_package_generation.hpp"

#include <cstdint>
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
	static CompiledPagination DisabledPagination();
	static CompiledOperationSelector OperationSelector(std::vector<std::string> required_inputs,
	                                                   std::vector<std::vector<std::string>> any_input_sets,
	                                                   std::vector<std::string> forbidden_inputs,
	                                                   std::int32_t priority);
	static CompiledAuthenticationPolicy AnonymousAuthentication();
	static CompiledResourceCeilings UnpaginatedResources(std::uint64_t max_records,
	                                                     std::uint64_t max_extracted_string_bytes);
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
