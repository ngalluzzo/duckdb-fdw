#include "connector/support/connector_catalog_test_fixtures.hpp"

#include "connector/support/catalog_test_access.hpp"
#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb_api_test {

const char DISTINCT_SCHEMA_ANONYMOUS_RELATION[] = "fixture_public_records";
const char DISTINCT_SCHEMA_AUTHENTICATED_RELATION[] = "fixture_private_profile";
const char PAGINATION_DECOY_RELATION[] = "fixture_page_shaped_unpaginated";
const char PAGINATION_LINK_RELATION[] = "fixture_explicit_link_records";
const char PREDICATE_EXACT_RELATION[] = "controlled_exact_repositories";
const char PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION[] = "controlled_equal_ranked_operations";
const char PREDICATE_AMBIGUOUS_MAPPINGS_RELATION[] = "controlled_exact_repositories";
const char OPERATION_UNIQUE_WINNER_RELATION[] = "controlled_exact_repositories";
const char OPERATION_FALLBACK_RELATION[] = "controlled_exact_repositories";
const char GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION[] = "viewer_repository_metrics";

namespace {

duckdb_api::CompiledConnector BuildPredicateDecoyCatalog(std::string connector_name,
                                                         std::vector<duckdb_api::CompiledColumn> columns,
                                                         std::string operation_name, std::string path) {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    "authenticated_repositories", std::move(columns),
	    duckdb_api::CompiledOperation {std::move(operation_name),
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::SequentialLink("per_page", 100, "page", 1, 1, 2),
	                                   {origin,
	                                    std::move(path),
	                                    {{"per_page", "100"}, {"page", "1"}},
	                                    {{"X-Connector-Fixture", "predicate-decoy"}}},
	                                   duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	                                   "$",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(),
	    ConnectorCatalogTestAccess::PaginatedResources(4096, 8192, 100, 200, 512)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, std::move(connector_name), "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 4096});
}

std::vector<duckdb_api::CompiledColumn> PredicateRepositorySchema() {
	return {{"id", "BIGINT", false, "$.id"},
	        {"full_name", "VARCHAR", false, "$.full_name"},
	        {"private", "BOOLEAN", false, "$.private"},
	        {"fork", "BOOLEAN", false, "$.fork"},
	        {"archived", "BOOLEAN", false, "$.archived"},
	        {"visibility", "VARCHAR", false, "$.visibility"}};
}

duckdb_api::CompiledOperation ControlledExactPredicateOperation(
    bool fallback = true, duckdb_api::CompiledOperationSelector selector = duckdb_api::CompiledOperationSelector(),
    std::string operation_name = "controlled_exact_repositories") {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("predicate-proof.invalid"), 443};
	return duckdb_api::CompiledOperation {
	    std::move(operation_name),
	    fallback,
	    duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	    duckdb_api::CompiledProtocol::REST,
	    duckdb_api::CompiledHttpMethod::GET,
	    duckdb_api::CompiledReplaySafety::SAFE,
	    false,
	    ConnectorCatalogTestAccess::DisabledPagination(),
	    {origin, "/fixtures/exact-repositories", {}, {{"X-Connector-Fixture", "exact-duplicate-repositories"}}},
	    duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	    "$",
	    std::move(selector)};
}

duckdb_api::CompiledPredicateMapping
ControlledExactPredicateMapping(std::string remote_input_name,
                                std::string operation_name = "controlled_exact_repositories") {
	return ConnectorCatalogTestAccess::PredicateMapping(
	    "visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
	    duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, std::move(operation_name),
	    duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, std::move(remote_input_name), "private",
	    duckdb_api::CompiledPredicateAccuracy::EXACT,
	    duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
	    duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
	    duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
	    duckdb_api::CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT);
}

std::vector<duckdb_api::CompiledColumn> ControlledPredicateSchema() {
	return {{"occurrence_id", "BIGINT", false, "$.occurrence_id"}, {"visibility", "VARCHAR", false, "$.visibility"}};
}

duckdb_api::CompiledOperation ControlledSelectorFallbackOperation() {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("predicate-proof.invalid"), 443};
	return duckdb_api::CompiledOperation {"controlled_selector_fallback_repositories",
	                                      true,
	                                      duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                      duckdb_api::CompiledProtocol::REST,
	                                      duckdb_api::CompiledHttpMethod::GET,
	                                      duckdb_api::CompiledReplaySafety::SAFE,
	                                      false,
	                                      ConnectorCatalogTestAccess::DisabledPagination(),
	                                      {origin,
	                                       "/fixtures/selector-fallback-repositories",
	                                       {},
	                                       {{"X-Connector-Fixture", "selector-fallback-repositories"}}},
	                                      duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	                                      "$.records[*]",
	                                      duckdb_api::CompiledOperationSelector()};
}

duckdb_api::CompiledConnector BuildSelectableOperationsCatalog(std::string connector_name, bool priority_winner) {
	std::vector<duckdb_api::CompiledOperation> operations;
	std::vector<duckdb_api::CompiledPredicateMapping> mappings;
	if (priority_winner) {
		operations.push_back(ControlledExactPredicateOperation(
		    false, ConnectorCatalogTestAccess::OperationSelector({}, {{"visibility"}}, {}, 10)));
		operations.push_back(ControlledExactPredicateOperation(
		    false, ConnectorCatalogTestAccess::OperationSelector({"visibility"}, {}, {}, 5),
		    "controlled_priority_exact_repositories"));
		mappings.push_back(ControlledExactPredicateMapping("visibility"));
		mappings.push_back(ControlledExactPredicateMapping("visibility", "controlled_priority_exact_repositories"));
	} else {
		operations.push_back(ControlledExactPredicateOperation(
		    false, ConnectorCatalogTestAccess::OperationSelector({"visibility"}, {}, {}, 10)));
		mappings.push_back(ControlledExactPredicateMapping("visibility"));
	}
	operations.push_back(ControlledSelectorFallbackOperation());
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PREDICATE_EXACT_RELATION, ControlledPredicateSchema(), std::move(operations),
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(8, 128),
	    std::move(mappings)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, std::move(connector_name), "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"predicate-proof.invalid"}, false, false, false, false, 4096});
}

} // namespace

duckdb_api::CompiledConnector BuildDistinctSchemaConnectorCatalogFixture() {
	const duckdb_api::CompiledRestOrigin github_origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                                      duckdb_api::CompiledRestHost("api.github.com"), 443};
	const std::vector<duckdb_api::CompiledHttpHeader> headers = {{"X-Connector-Fixture", "distinct-schema"}};

	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    DISTINCT_SCHEMA_ANONYMOUS_RELATION,
	    {{"public_id", "BIGINT", false, "$.public_id"}, {"public_label", "VARCHAR", false, "$.public_label"}},
	    duckdb_api::CompiledOperation {"fixture_public_records_page",
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::DisabledPagination(),
	                                   {github_origin, "/fixtures/public-records", {}, headers},
	                                   duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	                                   "$.records[*]",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(4, 64)));
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    DISTINCT_SCHEMA_AUTHENTICATED_RELATION,
	    {{"profile_login", "VARCHAR", false, "$.profile_login"},
	     {"profile_verified", "BOOLEAN", false, "$.profile_verified"},
	     {"profile_generation", "BIGINT", false, "$.profile_generation"}},
	    duckdb_api::CompiledOperation {"fixture_private_profile",
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::DisabledPagination(),
	                                   {github_origin, "/fixtures/private-profile", {}, headers},
	                                   duckdb_api::CompiledResponseSource::ROOT_OBJECT,
	                                   "$",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(), ConnectorCatalogTestAccess::UnpaginatedResources(1, 96)));

	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "fixture_distinct_catalog", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 4096});
}

duckdb_api::CompiledConnector BuildPaginationConnectorCatalogFixture() {
	const duckdb_api::CompiledRestOrigin github_origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                                      duckdb_api::CompiledRestHost("api.github.com"), 443};
	const std::vector<duckdb_api::CompiledColumn> columns = {{"record_id", "BIGINT", false, "$.record_id"},
	                                                         {"record_label", "VARCHAR", false, "$.record_label"}};
	const std::vector<duckdb_api::CompiledHttpHeader> headers = {{"X-Connector-Fixture", "pagination-shape"}};
	const std::vector<duckdb_api::CompiledQueryParameter> page_query = {{"batch_size", "3"}, {"cursor_page", "1"}};

	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PAGINATION_DECOY_RELATION, columns,
	    duckdb_api::CompiledOperation {"fixture_page_shaped_unpaginated",
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::DisabledPagination(),
	                                   {github_origin, "/fixtures/page-shaped", page_query, headers},
	                                   duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	                                   "$.records[*]",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(), ConnectorCatalogTestAccess::UnpaginatedResources(3, 96)));
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PAGINATION_LINK_RELATION, columns,
	    duckdb_api::CompiledOperation {
	        "fixture_explicit_link_records",
	        true,
	        duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        duckdb_api::CompiledProtocol::REST,
	        duckdb_api::CompiledHttpMethod::GET,
	        duckdb_api::CompiledReplaySafety::SAFE,
	        false,
	        ConnectorCatalogTestAccess::SequentialLink("batch_size", 3, "cursor_page", 1, 1, 4),
	        {github_origin, "/fixtures/linked-records", page_query, headers},
	        duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	        "$.records[*]",
	        duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(),
	    ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 3, 12, 96)));

	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "fixture_pagination_catalog", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 2048});
}

duckdb_api::CompiledConnector
BuildPaginationPlannerCandidate(std::uint64_t max_pages, std::uint64_t response_bytes_per_page,
                                std::uint64_t response_bytes_per_scan, std::uint64_t records_per_page,
                                std::uint64_t records_per_scan, std::uint64_t extracted_string_bytes) {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    "planner_pagination_candidate",
	    {{"record_id", "BIGINT", false, "$.record_id"}, {"label", "VARCHAR", false, "$.label"}},
	    duckdb_api::CompiledOperation {
	        "planner_pagination_candidate",
	        true,
	        duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        duckdb_api::CompiledProtocol::REST,
	        duckdb_api::CompiledHttpMethod::GET,
	        duckdb_api::CompiledReplaySafety::SAFE,
	        false,
	        ConnectorCatalogTestAccess::SequentialLink("batch_size", 3, "cursor_page", 1, 1, max_pages),
	        {origin,
	         "/fixtures/planner-pagination",
	         {{"batch_size", "3"}, {"cursor_page", "1"}},
	         {{"X-Connector-Fixture", "planner-pagination"}}},
	        duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	        "$.records[*]",
	        duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(),
	    ConnectorCatalogTestAccess::PaginatedResources(response_bytes_per_page, response_bytes_per_scan,
	                                                   records_per_page, records_per_scan, extracted_string_bytes)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "planner_pagination_catalog", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {
	        {"https"}, {"api.github.com"}, false, false, false, false, response_bytes_per_page});
}

duckdb_api::CompiledConnector BuildDisabledRootArrayRepositoryCandidate() {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    "authenticated_repositories", {{"id", "BIGINT", false, "$.id"}, {"full_name", "VARCHAR", false, "$.full_name"}},
	    duckdb_api::CompiledOperation {"github_authenticated_repositories",
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::DisabledPagination(),
	                                   {origin,
	                                    "/user/repos",
	                                    {{"per_page", "100"}, {"page", "1"}},
	                                    {{"X-Connector-Fixture", "disabled-root-array"}}},
	                                   duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	                                   "$",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(), ConnectorCatalogTestAccess::UnpaginatedResources(100, 512)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "github", "test-disabled-root-array",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 8 * 1024 * 1024});
}

duckdb_api::CompiledConnector BuildPredicateMappingAbsentCatalogFixture() {
	return ConnectorCatalogTestAccess::WithoutPredicateMappings(duckdb_api::BuildNativeGithubConnector());
}

duckdb_api::CompiledConnector BuildPredicateSchemaVariationCatalogFixture() {
	auto columns = PredicateRepositorySchema();
	columns.back() = {"repository_visibility", "VARCHAR", false, "$.visibility"};
	return BuildPredicateDecoyCatalog("fixture_predicate_schema", std::move(columns),
	                                  "github_authenticated_repositories", "/user/repos");
}

duckdb_api::CompiledConnector BuildPredicateOperationVariationCatalogFixture() {
	return BuildPredicateDecoyCatalog("fixture_predicate_operation", PredicateRepositorySchema(),
	                                  "fixture_repository_operation", "/fixtures/repositories");
}

duckdb_api::CompiledConnector BuildExactPredicateCatalogFixture() {
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PREDICATE_EXACT_RELATION, ControlledPredicateSchema(), ControlledExactPredicateOperation(),
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(8, 128),
	    {ControlledExactPredicateMapping("visibility")}));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "controlled_exact_predicate", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"predicate-proof.invalid"}, false, false, false, false, 4096});
}

duckdb_api::CompiledConnector BuildEqualRankedOperationsCatalogFixture() {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("predicate-proof.invalid"), 443};
	const std::vector<duckdb_api::CompiledHttpHeader> headers = {{"X-Connector-Fixture", "equal-ranked-repositories"}};
	std::vector<duckdb_api::CompiledOperation> operations;
	for (const auto &suffix : {std::string("a"), std::string("b")}) {
		operations.push_back(
		    duckdb_api::CompiledOperation {"controlled_equal_ranked_repositories_" + suffix,
		                                   false,
		                                   duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
		                                   duckdb_api::CompiledProtocol::REST,
		                                   duckdb_api::CompiledHttpMethod::GET,
		                                   duckdb_api::CompiledReplaySafety::SAFE,
		                                   false,
		                                   ConnectorCatalogTestAccess::DisabledPagination(),
		                                   {origin, "/fixtures/equal-ranked-repositories-" + suffix, {}, headers},
		                                   duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
		                                   "$.records[*]",
		                                   duckdb_api::CompiledOperationSelector()});
	}

	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION, ControlledPredicateSchema(), std::move(operations),
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(8, 128)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "controlled_equal_ranked_operations", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"predicate-proof.invalid"}, false, false, false, false, 4096});
}

duckdb_api::CompiledConnector BuildUniqueWinnerOperationsCatalogFixture() {
	return BuildSelectableOperationsCatalog("controlled_unique_winner_operations", true);
}

duckdb_api::CompiledConnector BuildFallbackOperationsCatalogFixture() {
	return BuildSelectableOperationsCatalog("controlled_fallback_operations", false);
}

duckdb_api::CompiledConnector BuildAmbiguousPredicateMappingsCatalogFixture() {
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PREDICATE_AMBIGUOUS_MAPPINGS_RELATION, ControlledPredicateSchema(), ControlledExactPredicateOperation(),
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(8, 128),
	    {ControlledExactPredicateMapping("visibility"), ControlledExactPredicateMapping("repository_visibility")}));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "controlled_ambiguous_predicate", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"predicate-proof.invalid"}, false, false, false, false, 4096});
}

duckdb_api::CompiledConnector BuildContradictorySelectorCatalogFixture() {
	auto selector = ConnectorCatalogTestAccess::OperationSelector({"visibility"}, {}, {"visibility"});
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PREDICATE_EXACT_RELATION, ControlledPredicateSchema(),
	    ControlledExactPredicateOperation(false, std::move(selector)), ConnectorCatalogTestAccess::Anonymous(),
	    ConnectorCatalogTestAccess::UnpaginatedResources(8, 128), {ControlledExactPredicateMapping("visibility")}));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "controlled_contradictory_selector", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"predicate-proof.invalid"}, false, false, false, false, 4096});
}

duckdb_api::CompiledConnector BuildMultipleFallbackOperationsCatalogFixture() {
	std::vector<duckdb_api::CompiledOperation> operations;
	operations.push_back(ControlledExactPredicateOperation());
	operations.push_back(ControlledSelectorFallbackOperation());
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    PREDICATE_EXACT_RELATION, ControlledPredicateSchema(), std::move(operations),
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(8, 128),
	    {ControlledExactPredicateMapping("visibility")}));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "controlled_multiple_fallback_operations",
	    "test-1", std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"predicate-proof.invalid"}, false, false, false, false, 4096});
}

duckdb_api::CompiledConnector BuildCanonicalGraphqlConnectorCatalogFixture() {
	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION,
	    {{"id", "VARCHAR", false, "$.id"},
	     {"full_name", "VARCHAR", false, "$.nameWithOwner"},
	     {"owner_login", "VARCHAR", false, "$.owner.login"},
	     {"stars", "BIGINT", false, "$.stargazerCount"},
	     {"primary_language", "VARCHAR", true, "$.primaryLanguage.name"},
	     {"private", "BOOLEAN", false, "$.isPrivate"},
	     {"archived", "BOOLEAN", false, "$.isArchived"},
	     {"updated_at", "VARCHAR", false, "$.updatedAt"}},
	    ConnectorCatalogTestAccess::GraphqlOperation(
	        "github_viewer_repository_metrics", true, duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        duckdb_api::internal::BuildCanonicalGithubViewerRepositoryMetricsGraphqlOperation()),
	    ConnectorCatalogTestAccess::RequiredBearer(),
	    ConnectorCatalogTestAccess::PaginatedResources(8ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL, 100, 3200,
	                                                   512)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "canonical_graphql_fixture", "test-graphql-v1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {
	        {"https"}, {"api.github.com"}, false, false, false, false, 8ULL * 1024ULL * 1024ULL});
}

duckdb_api::CompiledConnector BuildInvalidGraphqlConnectorCatalogCandidate(InvalidGraphqlCatalogCandidate candidate) {
	auto catalog = BuildCanonicalGraphqlConnectorCatalogFixture();
	if (candidate == InvalidGraphqlCatalogCandidate::SCHEMA_TYPE_DRIFT) {
		return ConnectorCatalogTestAccess::WithInvalidGraphqlColumnType(std::move(catalog), 3, "VARCHAR");
	}
	if (candidate == InvalidGraphqlCatalogCandidate::SCHEMA_NULLABILITY_DRIFT) {
		return ConnectorCatalogTestAccess::WithInvalidGraphqlColumnNullability(std::move(catalog), 4, false);
	}

	auto graphql = catalog.Relations().at(0).Operation().Graphql();
	switch (candidate) {
	case InvalidGraphqlCatalogCandidate::UNKNOWN_DOCUMENT_IDENTITY:
		graphql.document_identity = static_cast<duckdb_api::CompiledGraphqlDocumentIdentity>(255);
		break;
	case InvalidGraphqlCatalogCandidate::CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST:
		graphql.document.replace(graphql.document.find("DuckdbApiViewerRepositoryMetrics"),
		                         std::string("DuckdbApiViewerRepositoryMetrics").size(),
		                         "DuckdbApiViewerRepositoryMetricsAlternate");
		graphql.document_digest = duckdb_api::ComputeSha256Hex(graphql.document);
		break;
	case InvalidGraphqlCatalogCandidate::DOCUMENT_DIGEST_MISMATCH:
		graphql.document_digest.at(0) = graphql.document_digest.at(0) == '0' ? '1' : '0';
		break;
	case InvalidGraphqlCatalogCandidate::VARIABLE_PROFILE_DRIFT:
		graphql.variables.at(1).source = duckdb_api::CompiledGraphqlVariableSource::CALLER_INPUT;
		break;
	case InvalidGraphqlCatalogCandidate::RESPONSE_NODES_PATH_DRIFT:
		graphql.response.nodes.segments.back() = "edges";
		break;
	case InvalidGraphqlCatalogCandidate::RESPONSE_ERRORS_PATH_DRIFT:
		graphql.response.errors.segments.back() = "graphqlErrors";
		break;
	case InvalidGraphqlCatalogCandidate::PARTIAL_DATA_POLICY_DRIFT:
		graphql.response.partial_data = static_cast<duckdb_api::CompiledGraphqlPartialDataPolicy>(255);
		break;
	case InvalidGraphqlCatalogCandidate::CURSOR_PROFILE_DRIFT:
		graphql.cursor.dependency = duckdb_api::CompiledGraphqlCursorDependency::INDEPENDENT;
		break;
	case InvalidGraphqlCatalogCandidate::BODY_BUDGET_DRIFT:
		graphql.max_serialized_request_body_bytes_per_request++;
		break;
	case InvalidGraphqlCatalogCandidate::SCHEMA_TYPE_DRIFT:
	case InvalidGraphqlCatalogCandidate::SCHEMA_NULLABILITY_DRIFT:
		throw std::logic_error("GraphQL schema candidate dispatch escaped its fixture-only branch");
	default:
		throw std::invalid_argument("unknown invalid GraphQL catalog candidate");
	}
	return ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(std::move(catalog), std::move(graphql));
}

} // namespace duckdb_api_test
