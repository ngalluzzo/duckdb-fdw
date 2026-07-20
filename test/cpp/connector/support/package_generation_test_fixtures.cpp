#include "connector/support/package_generation_test_fixtures.hpp"

#include "connector/support/catalog_test_access.hpp"
#include "duckdb_api/internal/connector/compiled_model_builder.hpp"
#include "duckdb_api/internal/connector/predicate_declaration.hpp"

#include <utility>
#include <vector>

namespace duckdb_api_test {

const char PACKAGE_TYPED_RELATION[] = "typed_records";
const char PACKAGE_DISTINCT_RELATION[] = "distinct_status";
const char PACKAGE_PREDICATE_RELATION[] = "controlled_exact_repositories";

namespace {

using duckdb_api::CompiledConnector;
using duckdb_api::CompiledConnectorOrigin;
using duckdb_api::CompiledHttpHost;
using duckdb_api::CompiledHttpMethod;
using duckdb_api::CompiledHttpOrigin;
using duckdb_api::CompiledOperation;
using duckdb_api::CompiledOperationCardinality;
using duckdb_api::CompiledPredicateAccuracy;
using duckdb_api::CompiledPredicateBaseDomain;
using duckdb_api::CompiledPredicateEncodingCapability;
using duckdb_api::CompiledPredicateInputPlacement;
using duckdb_api::CompiledPredicateLiteral;
using duckdb_api::CompiledPredicateOccurrencePreservation;
using duckdb_api::CompiledPredicateOperator;
using duckdb_api::CompiledPredicateProofIdentity;
using duckdb_api::CompiledProtocol;
using duckdb_api::CompiledRelation;
using duckdb_api::CompiledReplaySafety;
using duckdb_api::CompiledResponseSource;
using duckdb_api::CompiledScalarType;
using duckdb_api::CompiledUrlScheme;
using duckdb_api::internal::CompiledModelBuilder;

std::string Digest(char fill) {
	return "sha256." + std::string(64, fill);
}

CompiledHttpOrigin Origin(const std::string &host) {
	return CompiledHttpOrigin {CompiledUrlScheme::HTTPS, CompiledHttpHost(host), 443};
}

CompiledOperation RestOperation(std::string name, bool fallback, std::string path,
                                duckdb_api::CompiledOperationSelector selector, std::string host = "api.github.com") {
	return CompiledOperation {std::move(name),
	                          fallback,
	                          CompiledOperationCardinality::ZERO_TO_MANY,
	                          CompiledProtocol::REST,
	                          CompiledHttpMethod::GET,
	                          CompiledReplaySafety::SAFE,
	                          false,
	                          CompiledModelBuilder::DisabledPagination(),
	                          {Origin(host), std::move(path), {}, {{"X-Connector-Fixture", "package-v1"}}},
	                          CompiledResponseSource::JSON_PATH_MANY,
	                          "$.records[*]",
	                          std::move(selector)};
}

std::vector<duckdb_api::CompiledRelationInput> TypedInputs(bool default_changed) {
	std::vector<duckdb_api::CompiledRelationInput> inputs;
	inputs.push_back(
	    CompiledModelBuilder::Input("query", CompiledScalarType::VARCHAR, false, CompiledModelBuilder::NoDefault()));
	inputs.push_back(CompiledModelBuilder::Input(
	    "limit", CompiledScalarType::BIGINT, false,
	    CompiledModelBuilder::Default(CompiledModelBuilder::Bigint(default_changed ? 50 : 25))));
	inputs.push_back(CompiledModelBuilder::Input("include_archived", CompiledScalarType::BOOLEAN, false,
	                                             CompiledModelBuilder::Default(CompiledModelBuilder::Boolean(false))));
	inputs.push_back(CompiledModelBuilder::Input(
	    "cursor", CompiledScalarType::VARCHAR, true,
	    CompiledModelBuilder::Default(CompiledModelBuilder::Null(CompiledScalarType::VARCHAR))));
	inputs.push_back(CompiledModelBuilder::Input(
	    "locale", CompiledScalarType::VARCHAR, true,
	    CompiledModelBuilder::Default(CompiledModelBuilder::Varchar(default_changed ? "regional" : "global"))));
	return inputs;
}

std::vector<duckdb_api::CompiledColumn> TypedColumns(bool changed) {
	std::vector<duckdb_api::CompiledColumn> columns;
	columns.push_back(CompiledModelBuilder::Column("record_id", CompiledScalarType::BIGINT, false, "$.record_id"));
	columns.push_back(CompiledModelBuilder::Column("label", CompiledScalarType::VARCHAR, true,
	                                               changed ? "$.display_label" : "$.label"));
	columns.push_back(CompiledModelBuilder::Column("active", CompiledScalarType::BOOLEAN, false, "$.active"));
	return columns;
}

CompiledRelation BuildTypedRelation(bool tie, PackageCompatibilityFixture variant) {
	const bool operation_changed = variant == PackageCompatibilityFixture::OPERATION_CHANGED;
	const bool origin_changed = variant == PackageCompatibilityFixture::OPERATION_ORIGIN_CHANGED;
	const bool selector_changed = variant == PackageCompatibilityFixture::SELECTOR_REFERENCE_CHANGED;
	std::vector<CompiledOperation> operations;
	operations.push_back(RestOperation(
	    "typed_by_query", false, operation_changed ? "/fixtures/typed-records-v2" : "/fixtures/typed-records",
	    CompiledModelBuilder::V1OperationSelector(
	        {CompiledModelBuilder::RelationInputReference(selector_changed ? "cursor" : "query")}),
	    origin_changed ? "secondary.example" : "api.github.com"));
	if (tie) {
		operations.push_back(
		    RestOperation("typed_by_query_peer", false, "/fixtures/typed-records-peer",
		                  CompiledModelBuilder::V1OperationSelector(
		                      {CompiledModelBuilder::RelationInputReference(selector_changed ? "cursor" : "query")})));
	} else {
		operations.push_back(RestOperation("typed_default", true, "/fixtures/typed-records-default",
		                                   CompiledModelBuilder::V1OperationSelector({})));
	}
	const auto authentication = variant == PackageCompatibilityFixture::AUTHENTICATION_CHANGED
	                                ? ConnectorCatalogTestAccess::RequiredBearer()
	                                : ConnectorCatalogTestAccess::Anonymous();
	const auto resources = ConnectorCatalogTestAccess::UnpaginatedResources(
	    variant == PackageCompatibilityFixture::RESOURCE_CHANGED ? 17 : 16, 256);
	return ConnectorCatalogTestAccess::Relation(PACKAGE_TYPED_RELATION,
	                                            TypedColumns(variant == PackageCompatibilityFixture::COLUMN_CHANGED),
	                                            TypedInputs(variant == PackageCompatibilityFixture::INPUT_CHANGED),
	                                            std::move(operations), authentication, resources);
}

CompiledRelation BuildDistinctRelation(bool renamed = false) {
	std::vector<duckdb_api::CompiledColumn> columns;
	columns.push_back(CompiledModelBuilder::Column("status", CompiledScalarType::VARCHAR, false, "$.status"));
	std::vector<duckdb_api::CompiledRelationInput> inputs;
	inputs.push_back(CompiledModelBuilder::Input("partition", CompiledScalarType::VARCHAR, false,
	                                             CompiledModelBuilder::Default(CompiledModelBuilder::Varchar("all"))));
	std::vector<CompiledOperation> operations;
	operations.push_back(RestOperation("distinct_status", true, "/fixtures/distinct-status",
	                                   CompiledModelBuilder::V1OperationSelector({})));
	return ConnectorCatalogTestAccess::Relation(renamed ? "distinct_state" : PACKAGE_DISTINCT_RELATION,
	                                            std::move(columns), std::move(inputs), std::move(operations),
	                                            ConnectorCatalogTestAccess::Anonymous(),
	                                            ConnectorCatalogTestAccess::UnpaginatedResources(1, 64));
}

duckdb_api::CompiledPredicateMapping
PackagePredicateMapping(const std::string &column, duckdb_api::CompiledScalarValue literal,
                        const std::string &operation, const std::string &remote_input, const std::string &encoded_value,
                        const duckdb_api::internal::CompiledPackagePredicateIdentities &identities,
                        const std::string &fixture_prefix) {
	return ConnectorCatalogTestAccess::PackagePredicateMapping(
	    column, std::move(literal), operation, remote_input, encoded_value, CompiledPredicateAccuracy::EXACT,
	    identities.proof, identities.base_domain, fixture_prefix + "_match", fixture_prefix + "_false_or_null",
	    fixture_prefix + "_duplicates");
}

CompiledRelation BuildPredicateRelation(bool changed, const std::string &package_digest,
                                        bool conflicting_mappings = false) {
	const std::string conditional_input = changed ? "repository_visibility" : "visibility";
	std::vector<duckdb_api::CompiledColumn> columns;
	columns.push_back(
	    CompiledModelBuilder::Column("occurrence_id", CompiledScalarType::BIGINT, false, "$.occurrence_id"));
	columns.push_back(CompiledModelBuilder::Column("visibility", CompiledScalarType::VARCHAR, false, "$.visibility"));
	std::vector<CompiledOperation> operations;
	operations.push_back(CompiledOperation {
	    "controlled_exact_repositories",
	    false,
	    CompiledOperationCardinality::ZERO_TO_MANY,
	    CompiledProtocol::REST,
	    CompiledHttpMethod::GET,
	    CompiledReplaySafety::SAFE,
	    false,
	    CompiledModelBuilder::DisabledPagination(),
	    {Origin("predicate-proof.invalid"),
	     "/fixtures/exact-repositories",
	     {duckdb_api::CompiledQueryParameter(conditional_input, duckdb_api::CompiledQueryValueSource::CONDITIONAL_INPUT,
	                                         conditional_input, true, false)},
	     {{"X-Connector-Fixture", "exact-duplicate-repositories"}}},
	    CompiledResponseSource::JSON_PATH_MANY,
	    "$.records[*]",
	    CompiledModelBuilder::V1OperationSelector(
	        {CompiledModelBuilder::ConditionalInputReference(conditional_input)})});
	operations.push_back(CompiledOperation {"controlled_all_repositories",
	                                        true,
	                                        CompiledOperationCardinality::ZERO_TO_MANY,
	                                        CompiledProtocol::REST,
	                                        CompiledHttpMethod::GET,
	                                        CompiledReplaySafety::SAFE,
	                                        false,
	                                        CompiledModelBuilder::DisabledPagination(),
	                                        {Origin("predicate-proof.invalid"),
	                                         "/fixtures/all-repositories",
	                                         {},
	                                         {{"X-Connector-Fixture", "all-repositories"}}},
	                                        CompiledResponseSource::JSON_PATH_MANY,
	                                        "$.records[*]",
	                                        CompiledModelBuilder::V1OperationSelector({})});
	const auto identities = duckdb_api::internal::DerivePackagePredicateIdentities(
	    package_digest, PACKAGE_PREDICATE_RELATION, operations.front());
	std::vector<duckdb_api::CompiledPredicateMapping> mappings;
	mappings.push_back(PackagePredicateMapping("visibility", CompiledModelBuilder::Varchar("private"),
	                                           "controlled_exact_repositories", conditional_input, "private",
	                                           identities, "private"));
	if (conflicting_mappings) {
		mappings.push_back(PackagePredicateMapping("visibility", CompiledModelBuilder::Varchar("public"),
		                                           "controlled_exact_repositories", conditional_input, "public",
		                                           identities, "public"));
	}
	return ConnectorCatalogTestAccess::Relation(PACKAGE_PREDICATE_RELATION, std::move(columns), {},
	                                            std::move(operations), ConnectorCatalogTestAccess::Anonymous(),
	                                            ConnectorCatalogTestAccess::UnpaginatedResources(8, 128),
	                                            std::move(mappings));
}

CompiledRelation BuildScalarPredicateRelation(const std::string &package_digest, const std::string &relation_name,
                                              const std::string &column_name, CompiledScalarType type,
                                              duckdb_api::CompiledScalarValue literal,
                                              const std::string &encoded_value) {
	const std::string operation_name = relation_name + "_selected";
	std::vector<duckdb_api::CompiledColumn> columns;
	columns.push_back(
	    CompiledModelBuilder::Column("occurrence_id", CompiledScalarType::BIGINT, false, "$.occurrence_id"));
	columns.push_back(CompiledModelBuilder::Column(column_name, type, false, "$." + column_name));
	std::vector<CompiledOperation> operations;
	operations.push_back(CompiledOperation {
	    operation_name,
	    false,
	    CompiledOperationCardinality::ZERO_TO_MANY,
	    CompiledProtocol::REST,
	    CompiledHttpMethod::GET,
	    CompiledReplaySafety::SAFE,
	    false,
	    CompiledModelBuilder::DisabledPagination(),
	    {Origin("predicate-proof.invalid"),
	     "/fixtures/" + relation_name + "/restricted",
	     {duckdb_api::CompiledQueryParameter(column_name, duckdb_api::CompiledQueryValueSource::CONDITIONAL_INPUT,
	                                         column_name, true, false)},
	     {{"X-Connector-Fixture", relation_name}}},
	    CompiledResponseSource::JSON_PATH_MANY,
	    "$.records[*]",
	    CompiledModelBuilder::V1OperationSelector({CompiledModelBuilder::ConditionalInputReference(column_name)})});
	operations.push_back(CompiledOperation {relation_name + "_fallback",
	                                        true,
	                                        CompiledOperationCardinality::ZERO_TO_MANY,
	                                        CompiledProtocol::REST,
	                                        CompiledHttpMethod::GET,
	                                        CompiledReplaySafety::SAFE,
	                                        false,
	                                        CompiledModelBuilder::DisabledPagination(),
	                                        {Origin("predicate-proof.invalid"),
	                                         "/fixtures/" + relation_name + "/all",
	                                         {},
	                                         {{"X-Connector-Fixture", relation_name + "-fallback"}}},
	                                        CompiledResponseSource::JSON_PATH_MANY,
	                                        "$.records[*]",
	                                        CompiledModelBuilder::V1OperationSelector({})});
	const auto identities =
	    duckdb_api::internal::DerivePackagePredicateIdentities(package_digest, relation_name, operations.front());
	std::vector<duckdb_api::CompiledPredicateMapping> mappings;
	mappings.push_back(PackagePredicateMapping(column_name, std::move(literal), operation_name, column_name,
	                                           encoded_value, identities, relation_name));
	return ConnectorCatalogTestAccess::Relation(
	    relation_name, std::move(columns), {}, std::move(operations), ConnectorCatalogTestAccess::Anonymous(),
	    ConnectorCatalogTestAccess::UnpaginatedResources(8, 128), std::move(mappings));
}

CompiledRelation BuildAppendedRelation(const std::string &name = "appended_records") {
	std::vector<duckdb_api::CompiledColumn> columns;
	columns.push_back(CompiledModelBuilder::Column("value", CompiledScalarType::VARCHAR, false, "$.value"));
	std::vector<CompiledOperation> operations;
	operations.push_back(RestOperation(name, true, "/fixtures/" + name, CompiledModelBuilder::V1OperationSelector({}),
	                                   "secondary.example"));
	return ConnectorCatalogTestAccess::Relation(name, std::move(columns), {}, std::move(operations),
	                                            ConnectorCatalogTestAccess::Anonymous(),
	                                            ConnectorCatalogTestAccess::UnpaginatedResources(4, 64));
}

duckdb_api::CompiledNetworkPolicy NetworkPolicy(bool changed) {
	std::vector<std::string> hosts = {"api.github.com", "predicate-proof.invalid", "secondary.example"};
	if (changed) {
		hosts.push_back("new.example");
	}
	return duckdb_api::CompiledNetworkPolicy {{"https"}, std::move(hosts), false, false, false, false, 4096};
}

duckdb_api::CompiledPackageGeneration BuildGeneration(PackageCompatibilityFixture variant, bool tie,
                                                      const std::string &version, char digest_fill,
                                                      bool distinct_only = false, bool conflicting_predicates = false) {
	std::vector<CompiledRelation> relations;
	if (distinct_only) {
		relations.push_back(BuildDistinctRelation());
	} else {
		if (variant == PackageCompatibilityFixture::RELATION_INSERTED_BEFORE) {
			relations.push_back(BuildAppendedRelation("inserted_records"));
		}
		if (variant == PackageCompatibilityFixture::RELATION_REORDERED) {
			relations.push_back(BuildDistinctRelation());
			relations.push_back(BuildTypedRelation(tie, variant));
		} else {
			relations.push_back(BuildTypedRelation(tie, variant));
			relations.push_back(BuildDistinctRelation(variant == PackageCompatibilityFixture::RELATION_CHANGED));
		}
		if (variant != PackageCompatibilityFixture::RELATION_REMOVED) {
			relations.push_back(BuildPredicateRelation(variant == PackageCompatibilityFixture::PREDICATE_CHANGED,
			                                           Digest(digest_fill), conflicting_predicates));
		}
		if (variant == PackageCompatibilityFixture::APPEND_RELATION) {
			relations.push_back(BuildAppendedRelation());
		}
	}
	const std::string connector_id =
	    variant == PackageCompatibilityFixture::CONNECTOR_ID_CHANGED ? "fixture_package_other" : "fixture_package";
	auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", connector_id, version, Digest(digest_fill));
	auto connector = CompiledModelBuilder::Connector(
	    CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA, connector_id, version, std::move(relations),
	    NetworkPolicy(variant == PackageCompatibilityFixture::NETWORK_POLICY_CHANGED));
	return CompiledModelBuilder::PackageGeneration(std::move(identity), std::move(connector));
}

duckdb_api::CompiledPackageGeneration BuildTypedPredicateGeneration(const std::string &version, char digest_fill) {
	const auto digest = Digest(digest_fill);
	std::vector<CompiledRelation> relations;
	relations.push_back(BuildScalarPredicateRelation(digest, "boolean_predicates", "active",
	                                                 CompiledScalarType::BOOLEAN, CompiledModelBuilder::Boolean(true),
	                                                 "true"));
	relations.push_back(BuildScalarPredicateRelation(digest, "bigint_predicates", "rank", CompiledScalarType::BIGINT,
	                                                 CompiledModelBuilder::Bigint(42), "42"));
	relations.push_back(BuildScalarPredicateRelation(digest, "varchar_predicates", "visibility",
	                                                 CompiledScalarType::VARCHAR, CompiledModelBuilder::Varchar(""),
	                                                 ""));
	auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "typed_predicate_package", version, digest);
	auto connector =
	    CompiledModelBuilder::Connector(CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA, "typed_predicate_package",
	                                    version, std::move(relations), NetworkPolicy(false));
	return CompiledModelBuilder::PackageGeneration(std::move(identity), std::move(connector));
}

} // namespace

duckdb_api::CompiledPackageGeneration BuildTypedFallbackPackageGenerationFixture(const std::string &package_version,
                                                                                 char digest_fill) {
	return BuildGeneration(PackageCompatibilityFixture::BASELINE, false, package_version, digest_fill);
}

duckdb_api::CompiledPackageGeneration BuildTypedTiePackageGenerationFixture(const std::string &package_version,
                                                                            char digest_fill) {
	return BuildGeneration(PackageCompatibilityFixture::BASELINE, true, package_version, digest_fill);
}

duckdb_api::CompiledPackageGeneration BuildPredicateConflictPackageGenerationFixture(const std::string &package_version,
                                                                                     char digest_fill) {
	return BuildGeneration(PackageCompatibilityFixture::BASELINE, false, package_version, digest_fill, false, true);
}

duckdb_api::CompiledPackageGeneration BuildTypedPredicatePackageGenerationFixture(const std::string &package_version,
                                                                                  char digest_fill) {
	return BuildTypedPredicateGeneration(package_version, digest_fill);
}

duckdb_api::CompiledPackageGeneration BuildDistinctPackageGenerationFixture(const std::string &package_version,
                                                                            char digest_fill) {
	return BuildGeneration(PackageCompatibilityFixture::BASELINE, false, package_version, digest_fill, true);
}

duckdb_api::CompiledPackageGeneration BuildPackageCompatibilityFixture(PackageCompatibilityFixture variant,
                                                                       const std::string &package_version,
                                                                       char digest_fill) {
	return BuildGeneration(variant, false, package_version, digest_fill);
}

} // namespace duckdb_api_test
