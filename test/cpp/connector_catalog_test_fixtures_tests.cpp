#include "support/connector_catalog_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

void RequireColumn(const duckdb_api::CompiledColumn &column, const std::string &name, const std::string &logical_type,
                   const std::string &extractor) {
	Require(column.name == name, "fixture column name drifted: " + name);
	Require(column.logical_type == logical_type, "fixture column type drifted: " + name);
	Require(!column.nullable, "fixture column became nullable: " + name);
	Require(column.extractor == extractor, "fixture column extractor drifted: " + name);
}

void TestDistinctSchemaCatalogFixture() {
	const auto first = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto second = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	Require(first.Origin() == duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA,
	        "fixture catalog origin drifted");
	Require(first.ConnectorName() == "fixture_distinct_catalog", "fixture catalog name drifted");
	Require(first.Version() == "test-1", "fixture catalog version drifted");
	Require(first.Relations().size() == 2, "fixture catalog must contain exactly two relations");
	Require(std::string(duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION) == "fixture_public_records",
	        "fixture anonymous service identifier drifted");
	Require(std::string(duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION) == "fixture_private_profile",
	        "fixture authenticated service identifier drifted");

	const auto *anonymous = first.FindRelation(duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto *authenticated = first.FindRelation(duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION);
	Require(anonymous == &first.Relations()[0], "fixture anonymous lookup or stable order drifted");
	Require(authenticated == &first.Relations()[1], "fixture authenticated lookup or stable order drifted");
	Require(first.FindRelation("duckdb_login_search_page") == nullptr,
	        "fixture accidentally restored the native anonymous relation name");
	Require(first.FindRelation("authenticated_user") == nullptr,
	        "fixture accidentally restored the native authenticated relation name");

	Require(anonymous->Columns().size() == 2, "fixture anonymous schema width drifted");
	RequireColumn(anonymous->Columns()[0], "public_id", "BIGINT", "$.public_id");
	RequireColumn(anonymous->Columns()[1], "public_label", "VARCHAR", "$.public_label");
	Require(authenticated->Columns().size() == 3, "fixture authenticated schema width drifted");
	RequireColumn(authenticated->Columns()[0], "profile_login", "VARCHAR", "$.profile_login");
	RequireColumn(authenticated->Columns()[1], "profile_verified", "BOOLEAN", "$.profile_verified");
	RequireColumn(authenticated->Columns()[2], "profile_generation", "BIGINT", "$.profile_generation");
	Require(anonymous->Columns()[0].name != authenticated->Columns()[0].name,
	        "fixture schemas no longer distinguish relation selection");

	Require(anonymous->Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::NONE,
	        "fixture anonymous relation unexpectedly requires credentials");
	Require(authenticated->Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED,
	        "fixture authenticated relation lost its credential requirement");
	Require(authenticated->Authentication().LogicalCredential() == "token",
	        "fixture authenticated relation logical requirement drifted");
	Require(anonymous->Operation().cardinality == duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY &&
	            anonymous->Operation().response_source == duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	        "fixture anonymous response contract drifted");
	Require(authenticated->Operation().cardinality ==
	                duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS &&
	            authenticated->Operation().response_source == duckdb_api::CompiledResponseSource::ROOT_OBJECT,
	        "fixture authenticated response contract drifted");

	Require(first.Snapshot() == second.Snapshot(), "fixture construction is not deterministic");
	Require(first.Snapshot().find("Authorization=") == std::string::npos,
	        "fixture snapshot contains a valued credential header");
	Require(first.Snapshot().find("secret_name=") == std::string::npos,
	        "fixture snapshot contains a DuckDB secret binding");
}

} // namespace

int main() {
	try {
		TestDistinctSchemaCatalogFixture();
		std::cout << "connector catalog fixture tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector catalog fixture tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
