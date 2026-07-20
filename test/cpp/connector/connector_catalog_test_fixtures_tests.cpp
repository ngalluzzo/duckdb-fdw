#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::Require;

template <typename Callable>
void RequireInvalid(const std::string &message, Callable callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, message);
}

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
	            anonymous->Operation().Rest().response_source == duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	        "fixture anonymous response contract drifted");
	Require(authenticated->Operation().cardinality ==
	                duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS &&
	            authenticated->Operation().Rest().response_source == duckdb_api::CompiledResponseSource::ROOT_OBJECT,
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
	const auto &decoy_query = decoy->Operation().Rest().request.query_parameters;
	const auto &linked_query = linked->Operation().Rest().request.query_parameters;
	Require(decoy_query.size() == 2 && linked_query.size() == 2 && decoy_query[0].name == linked_query[0].name &&
	            decoy_query[0].encoded_value == linked_query[0].encoded_value &&
	            decoy_query[1].name == linked_query[1].name &&
	            decoy_query[1].encoded_value == linked_query[1].encoded_value,
	        "pagination fixture request shapes no longer provide an inference counterexample");
	Require(decoy->Operation().Rest().pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	        "page-shaped decoy unexpectedly enabled pagination");
	Require(linked->Operation().Rest().pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::LINK_HEADER,
	        "explicit Link relation lost its pagination declaration");

	const auto &pagination = linked->Operation().Rest().pagination;
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
	const auto &pagination = candidate.Operation().Rest().pagination;
	const auto &ceilings = candidate.ResourceCeilings();
	Require(candidate.Name() == "planner_pagination_candidate" &&
	            candidate.Operation().Rest().response_source == duckdb_api::CompiledResponseSource::JSON_PATH_MANY &&
	            candidate.Operation().Rest().records_extractor == "$.records[*]",
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
	const auto &rest = operation.Rest();
	const auto &repository_ceilings = repository.ResourceCeilings();
	Require(
	    repository.Name() == "authenticated_repositories" && operation.name == "github_authenticated_repositories" &&
	        rest.response_source == duckdb_api::CompiledResponseSource::ROOT_ARRAY && rest.records_extractor == "$" &&
	        rest.pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	    "disabled root-array fixture relation shape drifted");
	Require(rest.request.path == "/user/repos" && rest.request.query_parameters.size() == 2 &&
	            rest.request.query_parameters[0].name == "per_page" &&
	            rest.request.query_parameters[0].encoded_value == "100" &&
	            rest.request.query_parameters[1].name == "page" &&
	            rest.request.query_parameters[1].encoded_value == "1",
	        "disabled root-array fixture request shape drifted");
	Require(!repository_ceilings.HasResponseByteNarrowing() && repository_ceilings.MaxRecordsPerPage() == 100 &&
	            repository_ceilings.MaxRecordsPerScan() == 100 &&
	            repository_ceilings.MaxExtractedStringBytes() == 512 &&
	            disabled.NetworkPolicy().max_response_bytes == 8 * 1024 * 1024,
	        "disabled root-array fixture resource envelope drifted");
}

void TestPredicateMappingDecoyCatalogFixtures() {
	const auto absent = duckdb_api_test::BuildPredicateMappingAbsentCatalogFixture();
	const auto schema = duckdb_api_test::BuildPredicateSchemaVariationCatalogFixture();
	const auto operation = duckdb_api_test::BuildPredicateOperationVariationCatalogFixture();
	const auto *absent_relation = absent.FindRelation("authenticated_repositories");
	const auto *schema_relation = schema.FindRelation("authenticated_repositories");
	const auto *operation_relation = operation.FindRelation("authenticated_repositories");
	Require(absent_relation != nullptr && schema_relation != nullptr && operation_relation != nullptr,
	        "predicate decoy fixture lost its stable relation lookup");
	Require(absent_relation->PredicateMappings().empty() && schema_relation->PredicateMappings().empty() &&
	            operation_relation->PredicateMappings().empty(),
	        "predicate decoy fixture unexpectedly published a mapping");
	Require(absent_relation->Columns().size() == 6 && absent_relation->Columns().back().name == "visibility" &&
	            absent_relation->Columns().back().extractor == "$.visibility" &&
	            absent_relation->Operation().name == "github_authenticated_repositories" &&
	            absent_relation->Operation().Rest().request.path == "/user/repos",
	        "mapping-absence decoy no longer preserves native inference bait");
	Require(schema_relation->Columns().size() == 6 &&
	            schema_relation->Columns().back().name == "repository_visibility" &&
	            schema_relation->Columns().back().extractor == "$.visibility",
	        "schema-variation decoy no longer varies the mapped column identity");
	Require(operation_relation->Columns().size() == 6 && operation_relation->Columns().back().name == "visibility" &&
	            operation_relation->Operation().name == "fixture_repository_operation" &&
	            operation_relation->Operation().Rest().request.path == "/fixtures/repositories",
	        "operation-variation decoy no longer varies the operation identity");
	for (const auto *catalog : {&absent, &schema, &operation}) {
		Require(catalog->Snapshot().find("predicate_mappings=[]") != std::string::npos,
		        "predicate decoy explanation did not make mapping absence explicit");
		Require(catalog->Snapshot().find("Authorization=") == std::string::npos,
		        "predicate decoy explanation exposed credential material");
	}
}

void TestExactPredicateCatalogFixture() {
	const auto first = duckdb_api_test::BuildExactPredicateCatalogFixture();
	const auto second = duckdb_api_test::BuildExactPredicateCatalogFixture();
	Require(std::string(duckdb_api_test::PREDICATE_EXACT_RELATION) == "controlled_exact_repositories",
	        "exact predicate fixture relation identifier drifted");
	Require(first.ConnectorName() == "controlled_exact_predicate" && first.Version() == "test-1" &&
	            first.Relations().size() == 1,
	        "exact predicate fixture catalog identity drifted");
	const auto *relation = first.FindRelation(duckdb_api_test::PREDICATE_EXACT_RELATION);
	Require(relation == &first.Relations()[0], "exact predicate fixture lookup or singular relation drifted");
	Require(relation->Columns().size() == 2 && relation->Columns()[0].name == "occurrence_id" &&
	            relation->Columns()[0].logical_type == "BIGINT" && !relation->Columns()[0].nullable &&
	            relation->Columns()[1].name == "visibility" && relation->Columns()[1].logical_type == "VARCHAR" &&
	            !relation->Columns()[1].nullable && relation->Columns()[1].extractor == "$.visibility",
	        "exact predicate fixture schema or visibility ordinal drifted");
	Require(relation->PredicateMappings().size() == 1, "exact predicate fixture mapping count drifted");
	const auto &mapping = relation->PredicateMappings()[0];
	Require(mapping.ColumnName() == "visibility" &&
	            mapping.Literal() == duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE &&
	            mapping.OperationName() == relation->Operation().name && mapping.RemoteInputName() == "visibility" &&
	            mapping.EncodedRemoteValue() == "private" &&
	            mapping.Accuracy() == duckdb_api::CompiledPredicateAccuracy::EXACT &&
	            mapping.ProofIdentity() ==
	                duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY &&
	            mapping.BaseDomain() ==
	                duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES &&
	            mapping.OccurrencePreservation() ==
	                duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES &&
	            mapping.EncodingCapability() ==
	                duckdb_api::CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT &&
	            mapping.MaximumConditionalInputs() == 1 && !mapping.SupportsCompoundConjunctionEncoding() &&
	            !mapping.SupportsDisjunctionEncoding() && !mapping.SupportsComplementEncoding(),
	        "exact predicate fixture proof, domain, occurrence, or encoding facts drifted");
	Require(relation->Operation().Rest().request.query_parameters.empty(),
	        "exact predicate fixture duplicated its conditional input into the base request");
	Require(first.Snapshot() == second.Snapshot(), "exact predicate fixture construction is not deterministic");
	Require(
	    first.Snapshot().find("proof:controlled_exact_duplicate_repository_visibility") != std::string::npos &&
	        first.Snapshot().find("base_domain:controlled_duplicate_repository_occurrences") != std::string::npos &&
	        first.Snapshot().find("occurrences:exact_matching_base_occurrences") != std::string::npos &&
	        first.Snapshot().find("encoding:single_positive_rest_query_input[max_inputs:1,compound_and:unsupported,") !=
	            std::string::npos,
	    "exact predicate fixture snapshot lost structural proof facts");
	for (const auto &prohibited : {"SELECT ", "secret_name=", "credential_value=", "Authorization=", "Link="}) {
		Require(first.Snapshot().find(prohibited) == std::string::npos,
		        "exact predicate fixture snapshot contains prohibited state: " + std::string(prohibited));
	}
}

void TestEqualRankedOperationsCatalogFixture() {
	const auto first = duckdb_api_test::BuildEqualRankedOperationsCatalogFixture();
	const auto second = duckdb_api_test::BuildEqualRankedOperationsCatalogFixture();
	Require(std::string(duckdb_api_test::PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION) ==
	            "controlled_equal_ranked_operations",
	        "equal-ranked operation fixture relation identifier drifted");
	Require(first.ConnectorName() == "controlled_equal_ranked_operations" && first.Relations().size() == 1,
	        "equal-ranked operation fixture catalog identity drifted");
	const auto *relation = first.FindRelation(duckdb_api_test::PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION);
	Require(relation == &first.Relations()[0] && relation->Operations().size() == 2 && !relation->HasSingleOperation(),
	        "equal-ranked operation fixture lost its immutable operation collection");
	bool singleton_access_rejected = false;
	try {
		(void)relation->Operation();
	} catch (const std::logic_error &) {
		singleton_access_rejected = true;
	}
	Require(singleton_access_rejected, "singleton operation access selected an arbitrary equal-ranked operation");

	const auto &left = relation->Operations()[0];
	const auto &right = relation->Operations()[1];
	const auto &left_rest = left.Rest();
	const auto &right_rest = right.Rest();
	Require(left.name == "controlled_equal_ranked_repositories_a" &&
	            right.name == "controlled_equal_ranked_repositories_b" && left.name != right.name && !left.fallback &&
	            !right.fallback && left.cardinality == right.cardinality && left.Protocol() == right.Protocol() &&
	            left_rest.method == right_rest.method && left_rest.replay_safety == right_rest.replay_safety &&
	            left_rest.pagination.Strategy() == right_rest.pagination.Strategy() &&
	            left_rest.response_source == right_rest.response_source &&
	            left_rest.records_extractor == right_rest.records_extractor && left.selector.RequiredInputs().empty() &&
	            left.selector.AnyInputSets().empty() && left.selector.ForbiddenInputs().empty() &&
	            left.selector.Priority() == 0 && right.selector.RequiredInputs().empty() &&
	            right.selector.AnyInputSets().empty() && right.selector.ForbiddenInputs().empty() &&
	            right.selector.Priority() == 0,
	        "equal-ranked operation fixture no longer exposes two equally eligible base declarations");
	Require(left_rest.request.path != right_rest.request.path && relation->PredicateMappings().empty(),
	        "equal-ranked operation fixture lost distinct operation identity or invented predicate selection facts");
	Require(first.Snapshot() == second.Snapshot(), "equal-ranked operation fixture construction is not deterministic");
	Require(first.Snapshot().find("operations=[{operation=controlled_equal_ranked_repositories_a") !=
	                std::string::npos &&
	            first.Snapshot().find("{operation=controlled_equal_ranked_repositories_b") != std::string::npos,
	        "equal-ranked operation snapshot omitted the complete stable operation collection");
}

void TestSelectableOperationsCatalogFixtures() {
	const auto unique = duckdb_api_test::BuildUniqueWinnerOperationsCatalogFixture();
	const auto fallback = duckdb_api_test::BuildFallbackOperationsCatalogFixture();
	Require(std::string(duckdb_api_test::OPERATION_UNIQUE_WINNER_RELATION) == "controlled_exact_repositories" &&
	            std::string(duckdb_api_test::OPERATION_FALLBACK_RELATION) == "controlled_exact_repositories",
	        "selectable-operation fixture relation identifiers drifted");
	Require(unique.ConnectorName() == "controlled_unique_winner_operations" &&
	            fallback.ConnectorName() == "controlled_fallback_operations",
	        "selectable-operation fixture catalog identities drifted");
	const auto *unique_relation = unique.FindRelation("controlled_exact_repositories");
	Require(unique_relation != nullptr && unique_relation->Operations().size() == 3 &&
	            !unique_relation->HasSingleOperation() && unique_relation->PredicateMappings().size() == 2,
	        "unique-winner fixture lost its candidate, fallback, or mapping collection");
	const auto &any_candidate = unique_relation->Operations()[0];
	const auto &required_candidate = unique_relation->Operations()[1];
	const auto &unique_fallback = unique_relation->Operations()[2];
	Require(any_candidate.name == "controlled_exact_repositories" && !any_candidate.fallback &&
	            any_candidate.selector.RequiredInputs().empty() &&
	            any_candidate.selector.AnyInputSets() == std::vector<std::vector<std::string>>({{"visibility"}}) &&
	            any_candidate.selector.ForbiddenInputs().empty() && any_candidate.selector.Priority() == 10 &&
	            unique_relation->PredicateMappings()[0].OperationName() == any_candidate.name &&
	            unique_relation->PredicateMappings()[0].RemoteInputName() == "visibility",
	        "unique-winner fixture lost its higher-priority any-input candidate facts");
	Require(required_candidate.name == "controlled_priority_exact_repositories" && !required_candidate.fallback &&
	            required_candidate.selector.RequiredInputs() == std::vector<std::string>({"visibility"}) &&
	            required_candidate.selector.AnyInputSets().empty() &&
	            required_candidate.selector.ForbiddenInputs().empty() && required_candidate.selector.Priority() == 5 &&
	            unique_relation->PredicateMappings()[1].OperationName() == required_candidate.name &&
	            unique_relation->PredicateMappings()[1].RemoteInputName() == "visibility",
	        "unique-winner fixture lost its equally specific required-input candidate facts");
	Require(unique_fallback.name == "controlled_selector_fallback_repositories" && unique_fallback.fallback &&
	            unique_fallback.selector.RequiredInputs().empty() && unique_fallback.selector.AnyInputSets().empty() &&
	            unique_fallback.selector.ForbiddenInputs().empty() && unique_fallback.selector.Priority() == 0,
	        "unique-winner fixture lost its sole fallback facts");
	Require(unique.Snapshot().find("selector=required:[],any:[[visibility]],forbidden:[],priority:10") !=
	                std::string::npos &&
	            unique.Snapshot().find("selector=required:[visibility],any:[],forbidden:[],priority:5") !=
	                std::string::npos,
	        "unique-winner snapshot omitted immutable selector facts");

	const auto *fallback_relation = fallback.FindRelation("controlled_exact_repositories");
	Require(fallback_relation != nullptr && fallback_relation->Operations().size() == 2 &&
	            !fallback_relation->HasSingleOperation() && fallback_relation->PredicateMappings().size() == 1,
	        "fallback fixture lost its candidate, fallback, or mapping collection");
	const auto &ineligible_candidate = fallback_relation->Operations()[0];
	const auto &base_fallback = fallback_relation->Operations()[1];
	Require(ineligible_candidate.name == "controlled_exact_repositories" && !ineligible_candidate.fallback &&
	            ineligible_candidate.selector.RequiredInputs() == std::vector<std::string>({"visibility"}) &&
	            ineligible_candidate.selector.AnyInputSets().empty() &&
	            ineligible_candidate.selector.ForbiddenInputs().empty() &&
	            ineligible_candidate.selector.Priority() == 10 &&
	            fallback_relation->PredicateMappings()[0].OperationName() == ineligible_candidate.name &&
	            fallback_relation->PredicateMappings()[0].RemoteInputName() == "visibility",
	        "fallback fixture lost its required mapped candidate facts");
	Require(base_fallback.name == "controlled_selector_fallback_repositories" && base_fallback.fallback &&
	            base_fallback.selector.RequiredInputs().empty() && base_fallback.selector.AnyInputSets().empty() &&
	            base_fallback.selector.ForbiddenInputs().empty() && base_fallback.selector.Priority() == 0,
	        "fallback fixture lost its sole fallback-after-ineligibility facts");
	Require(fallback.Snapshot().find("selector=required:[visibility],any:[],forbidden:[],priority:10") !=
	            std::string::npos,
	        "fallback snapshot omitted immutable selector facts");
	Require(unique.Snapshot() == duckdb_api_test::BuildUniqueWinnerOperationsCatalogFixture().Snapshot() &&
	            fallback.Snapshot() == duckdb_api_test::BuildFallbackOperationsCatalogFixture().Snapshot(),
	        "selectable-operation fixture construction is not deterministic");
	RequireInvalid("controlled contradictory-selector fixture escaped validation",
	               []() { (void)duckdb_api_test::BuildContradictorySelectorCatalogFixture(); });
	RequireInvalid("controlled multiple-fallback fixture escaped validation",
	               []() { (void)duckdb_api_test::BuildMultipleFallbackOperationsCatalogFixture(); });
}

void TestAmbiguousPredicateMappingsCatalogFixture() {
	const auto first = duckdb_api_test::BuildAmbiguousPredicateMappingsCatalogFixture();
	const auto second = duckdb_api_test::BuildAmbiguousPredicateMappingsCatalogFixture();
	Require(std::string(duckdb_api_test::PREDICATE_AMBIGUOUS_MAPPINGS_RELATION) == "controlled_exact_repositories",
	        "ambiguous predicate fixture relation identifier drifted");
	Require(first.ConnectorName() == "controlled_ambiguous_predicate" && first.Relations().size() == 1,
	        "ambiguous predicate fixture catalog identity drifted");
	const auto *relation = first.FindRelation(duckdb_api_test::PREDICATE_AMBIGUOUS_MAPPINGS_RELATION);
	Require(relation == &first.Relations()[0] && relation->HasSingleOperation() && relation->Operations().size() == 1 &&
	            &relation->Operation() == &relation->Operations()[0],
	        "ambiguous predicate fixture changed its selected-operation precondition");
	Require(relation->PredicateMappings().size() == 2,
	        "ambiguous predicate fixture must expose exactly two safe mapping candidates");
	const auto &left = relation->PredicateMappings()[0];
	const auto &right = relation->PredicateMappings()[1];
	Require(left.ColumnName() == right.ColumnName() && left.Operator() == right.Operator() &&
	            left.Literal() == right.Literal() && left.OperationName() == relation->Operation().name &&
	            right.OperationName() == relation->Operation().name && left.Accuracy() == right.Accuracy() &&
	            left.ProofIdentity() == right.ProofIdentity() && left.BaseDomain() == right.BaseDomain() &&
	            left.OccurrencePreservation() == right.OccurrencePreservation() &&
	            left.EncodingCapability() == right.EncodingCapability() && left.RemoteInputName() == "visibility" &&
	            right.RemoteInputName() == "repository_visibility" &&
	            left.RemoteInputName() != right.RemoteInputName() && left.EncodedRemoteValue() == "private" &&
	            right.EncodedRemoteValue() == "private",
	        "ambiguous predicate fixture mappings are not distinct individually safe input encodings");
	Require(first.Snapshot() == second.Snapshot(), "ambiguous predicate fixture construction is not deterministic");
	Require(first.Snapshot().find("input:rest_query:visibility=private") != std::string::npos &&
	            first.Snapshot().find("input:rest_query:repository_visibility=private") != std::string::npos,
	        "ambiguous predicate fixture snapshot omitted a safe mapping candidate");
}

void TestCanonicalGraphqlCatalogFixture() {
	const auto first = duckdb_api_test::BuildCanonicalGraphqlConnectorCatalogFixture();
	const auto second = duckdb_api_test::BuildCanonicalGraphqlConnectorCatalogFixture();
	Require(std::string(duckdb_api_test::GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION) == "viewer_repository_metrics",
	        "GraphQL fixture stable relation identifier drifted");
	Require(first.ConnectorName() == "canonical_graphql_fixture" && first.Version() == "test-graphql-v1" &&
	            first.Relations().size() == 1,
	        "GraphQL fixture catalog identity drifted");
	const auto *relation = first.FindRelation(duckdb_api_test::GRAPHQL_VIEWER_REPOSITORY_METRICS_RELATION);
	Require(relation == &first.Relations()[0] && relation->Columns().size() == 8 && relation->HasSingleOperation() &&
	            relation->Operation().Protocol() == duckdb_api::CompiledProtocol::GRAPHQL,
	        "GraphQL fixture lookup, schema width, or protocol drifted");
	const auto &graphql = relation->Operation().Graphql();
	Require(graphql.result_columns.size() == relation->Columns().size() &&
	            graphql.result_columns[2].name == "owner_login" &&
	            graphql.result_columns[2].response_path.segments == std::vector<std::string>({"owner", "login"}) &&
	            graphql.result_columns[3].scalar_kind == duckdb_api::CompiledGraphqlScalarKind::INT64 &&
	            duckdb_api::IsCanonicalGraphqlDocumentProfile(graphql.document_identity, graphql.document,
	                                                          graphql.digest_algorithm, graphql.document_digest),
	        "GraphQL fixture lost canonical typed operation facts");
	Require(first.Snapshot() == second.Snapshot(), "GraphQL fixture construction is not deterministic");
	for (const auto &prohibited : {"query DuckdbApiViewer", "secret_name=", "credential_value=", "cursor_value=",
	                               "request_body=", "response_row="}) {
		Require(first.Snapshot().find(prohibited) == std::string::npos,
		        "GraphQL fixture snapshot contains prohibited state: " + std::string(prohibited));
	}
}

} // namespace

int main() {
	try {
		TestDistinctSchemaCatalogFixture();
		TestExplicitPaginationCatalogFixture();
		TestPlannerCounterexampleCatalogFixtures();
		TestPredicateMappingDecoyCatalogFixtures();
		TestExactPredicateCatalogFixture();
		TestEqualRankedOperationsCatalogFixture();
		TestSelectableOperationsCatalogFixtures();
		TestAmbiguousPredicateMappingsCatalogFixture();
		TestCanonicalGraphqlCatalogFixture();
		std::cout << "connector catalog fixture tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector catalog fixture tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
