#include "support/connector_catalog_test_fixtures.hpp"

#include "support/connector_catalog_test_access.hpp"

#include <utility>
#include <vector>

namespace duckdb_api_test {

const char DISTINCT_SCHEMA_ANONYMOUS_RELATION[] = "fixture_public_records";
const char DISTINCT_SCHEMA_AUTHENTICATED_RELATION[] = "fixture_private_profile";
const char PAGINATION_DECOY_RELATION[] = "fixture_page_shaped_unpaginated";
const char PAGINATION_LINK_RELATION[] = "fixture_explicit_link_records";

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
	                                   "$.records[*]"},
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
	                                   "$"},
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
	                                   "$.records[*]"},
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
	        "$.records[*]"},
	    ConnectorCatalogTestAccess::RequiredBearer(),
	    ConnectorCatalogTestAccess::PaginatedResources(1024, 4096, 3, 12, 96)));

	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "fixture_pagination_catalog", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 2048});
}

} // namespace duckdb_api_test
