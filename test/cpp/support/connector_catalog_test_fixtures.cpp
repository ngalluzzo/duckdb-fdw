#include "support/connector_catalog_test_fixtures.hpp"

#include "support/connector_catalog_test_access.hpp"

#include <utility>
#include <vector>

namespace duckdb_api_test {

const char DISTINCT_SCHEMA_ANONYMOUS_RELATION[] = "fixture_public_records";
const char DISTINCT_SCHEMA_AUTHENTICATED_RELATION[] = "fixture_private_profile";

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
	                                   false,
	                                   {github_origin, "/fixtures/public-records", {}, headers},
	                                   duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	                                   "$.records[*]"},
	    ConnectorCatalogTestAccess::Anonymous(), duckdb_api::CompiledResourceCeilings {4, 64}));
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
	                                   false,
	                                   {github_origin, "/fixtures/private-profile", {}, headers},
	                                   duckdb_api::CompiledResponseSource::ROOT_OBJECT,
	                                   "$"},
	    ConnectorCatalogTestAccess::RequiredBearer(), duckdb_api::CompiledResourceCeilings {1, 96}));

	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "fixture_distinct_catalog", "test-1",
	    std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 4096});
}

} // namespace duckdb_api_test
