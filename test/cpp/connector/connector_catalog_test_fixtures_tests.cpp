#include "connector/support/connector_catalog_test_fixtures.hpp"
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

void TestExplicitPaginationCatalogFixture() {
	const auto first = duckdb_api_test::BuildPaginationConnectorCatalogFixture();
	const auto second = duckdb_api_test::BuildPaginationConnectorCatalogFixture();
	Require(first.ConnectorName() == "fixture_pagination_catalog", "pagination fixture catalog name drifted");
	Require(first.Relations().size() == 2, "pagination fixture must contain exactly two relations");
	Require(std::string(duckdb_api_test::PAGINATION_DECOY_RELATION) == "fixture_page_shaped_unpaginated",
	        "pagination decoy service identifier drifted");
	Require(std::string(duckdb_api_test::PAGINATION_LINK_RELATION) == "fixture_explicit_link_records",
	        "pagination Link service identifier drifted");

	const auto *decoy = first.FindRelation(duckdb_api_test::PAGINATION_DECOY_RELATION);
	const auto *linked = first.FindRelation(duckdb_api_test::PAGINATION_LINK_RELATION);
	Require(decoy == &first.Relations()[0] && linked == &first.Relations()[1],
	        "pagination fixture exact lookup or stable order drifted");
	Require(decoy->Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED &&
	            linked->Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED,
	        "pagination fixture no longer disproves credential-based selection");
	const auto &decoy_query = decoy->Operation().request.query_parameters;
	const auto &linked_query = linked->Operation().request.query_parameters;
	Require(decoy_query.size() == 2 && linked_query.size() == 2 && decoy_query[0].name == linked_query[0].name &&
	            decoy_query[0].encoded_value == linked_query[0].encoded_value &&
	            decoy_query[1].name == linked_query[1].name &&
	            decoy_query[1].encoded_value == linked_query[1].encoded_value,
	        "pagination fixture request shapes no longer provide an inference counterexample");
	Require(decoy->Operation().pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	        "page-shaped decoy unexpectedly enabled pagination");
	Require(linked->Operation().pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::LINK_HEADER,
	        "explicit Link relation lost its pagination declaration");

	const auto &pagination = linked->Operation().pagination;
	Require(pagination.Dependency() == duckdb_api::CompiledPageDependency::SEQUENTIAL &&
	            pagination.Consistency() == duckdb_api::CompiledPageConsistency::MUTABLE &&
	            pagination.LinkRelation() == duckdb_api::CompiledLinkRelation::NEXT &&
	            pagination.TargetScope() ==
	                duckdb_api::CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH,
	        "explicit Link relation capability profile drifted");
	Require(!pagination.SupportsTotal() && !pagination.SupportsResume(),
	        "explicit Link relation gained a total or resume claim");
	Require(pagination.PageSizeParameter() == "batch_size" && pagination.PageSize() == 3 &&
	            pagination.PageNumberParameter() == "cursor_page" && pagination.FirstPage() == 1 &&
	            pagination.PageIncrement() == 1 && pagination.MaxPagesPerScan() == 4,
	        "explicit Link relation typed page bindings drifted");

	const auto &ceilings = linked->ResourceCeilings();
	Require(ceilings.HasResponseByteNarrowing() && ceilings.MaxResponseBytesPerPage() == 1024 &&
	            ceilings.MaxResponseBytesPerScan() == 4096 && ceilings.MaxRecordsPerPage() == 3 &&
	            ceilings.MaxRecordsPerScan() == 12 && ceilings.MaxExtractedStringBytes() == 96,
	        "pagination fixture page/scan resource scopes drifted");
	Require(!decoy->ResourceCeilings().HasResponseByteNarrowing() &&
	            decoy->ResourceCeilings().MaxRecordsPerPage() == 3 &&
	            decoy->ResourceCeilings().MaxRecordsPerScan() == 3,
	        "unpaginated fixture lost its one-page resource contract");

	Require(first.Snapshot() == second.Snapshot(), "pagination fixture construction is not deterministic");
	Require(first.Snapshot().find(
	            "pagination:link_header[relation:next,dependency:sequential,consistency:mutable,total:none,") !=
	            std::string::npos,
	        "pagination fixture snapshot lost its explicit capability declaration");
	Require(first.Snapshot().find("response_bytes_per_page:1024,response_bytes_per_scan:4096,records_per_page:3,") !=
	            std::string::npos,
	        "pagination fixture snapshot lost scoped resource declarations");
	for (const auto &prohibited : {"Authorization=", "secret_name=", "credential_value=", "response_url=", "Link="}) {
		Require(first.Snapshot().find(prohibited) == std::string::npos,
		        "pagination fixture snapshot contains prohibited execution material: " + std::string(prohibited));
	}
}

void TestPlannerCounterexampleCatalogFixtures() {
	const auto paginated = duckdb_api_test::BuildPaginationPlannerCandidate(5, 1024, 5120, 3, 15, 96);
	Require(paginated.ConnectorName() == "planner_pagination_catalog" && paginated.Version() == "test-1" &&
	            paginated.Relations().size() == 1,
	        "planner pagination fixture catalog identity drifted");
	const auto &candidate = paginated.Relations()[0];
	const auto &pagination = candidate.Operation().pagination;
	const auto &ceilings = candidate.ResourceCeilings();
	Require(candidate.Name() == "planner_pagination_candidate" &&
	            candidate.Operation().response_source == duckdb_api::CompiledResponseSource::JSON_PATH_MANY &&
	            candidate.Operation().records_extractor == "$.records[*]",
	        "planner pagination fixture relation shape drifted");
	Require(pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::LINK_HEADER &&
	            pagination.PageSizeParameter() == "batch_size" && pagination.PageSize() == 3 &&
	            pagination.PageNumberParameter() == "cursor_page" && pagination.FirstPage() == 1 &&
	            pagination.PageIncrement() == 1 && pagination.MaxPagesPerScan() == 5,
	        "planner pagination fixture declaration drifted");
	Require(ceilings.HasResponseByteNarrowing() && ceilings.MaxResponseBytesPerPage() == 1024 &&
	            ceilings.MaxResponseBytesPerScan() == 5120 && ceilings.MaxRecordsPerPage() == 3 &&
	            ceilings.MaxRecordsPerScan() == 15 && ceilings.MaxExtractedStringBytes() == 96 &&
	            paginated.NetworkPolicy().max_response_bytes == 1024,
	        "planner pagination fixture resource envelope drifted");

	const auto disabled = duckdb_api_test::BuildDisabledRootArrayRepositoryCandidate();
	Require(disabled.ConnectorName() == "github" && disabled.Version() == "test-disabled-root-array" &&
	            disabled.Relations().size() == 1,
	        "disabled root-array fixture catalog identity drifted");
	const auto &repository = disabled.Relations()[0];
	const auto &operation = repository.Operation();
	const auto &repository_ceilings = repository.ResourceCeilings();
	Require(repository.Name() == "authenticated_repositories" &&
	            operation.name == "github_authenticated_repositories" &&
	            operation.response_source == duckdb_api::CompiledResponseSource::ROOT_ARRAY &&
	            operation.records_extractor == "$" &&
	            operation.pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	        "disabled root-array fixture relation shape drifted");
	Require(operation.request.path == "/user/repos" && operation.request.query_parameters.size() == 2 &&
	            operation.request.query_parameters[0].name == "per_page" &&
	            operation.request.query_parameters[0].encoded_value == "100" &&
	            operation.request.query_parameters[1].name == "page" &&
	            operation.request.query_parameters[1].encoded_value == "1",
	        "disabled root-array fixture request shape drifted");
	Require(!repository_ceilings.HasResponseByteNarrowing() && repository_ceilings.MaxRecordsPerPage() == 100 &&
	            repository_ceilings.MaxRecordsPerScan() == 100 &&
	            repository_ceilings.MaxExtractedStringBytes() == 512 &&
	            disabled.NetworkPolicy().max_response_bytes == 8 * 1024 * 1024,
	        "disabled root-array fixture resource envelope drifted");
}

} // namespace

int main() {
	try {
		TestDistinctSchemaCatalogFixture();
		TestExplicitPaginationCatalogFixture();
		TestPlannerCounterexampleCatalogFixtures();
		std::cout << "connector catalog fixture tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector catalog fixture tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
