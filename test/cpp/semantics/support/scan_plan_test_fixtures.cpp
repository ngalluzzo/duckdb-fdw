#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"

#include <algorithm>
#include <utility>

namespace duckdb_api_test {
namespace {

const char ANONYMOUS_SOURCE_SNAPSHOT[] =
    "relation=duckdb_login_search_page;schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,"
    "site_admin:BOOLEAN!:$.site_admin;predicate_mappings=[];"
    "operation=github_search_duckdb_login_page:fallback:zero_to_many:REST:GET:"
    "replay_safe;request=origin:[scheme:https,host:api.github.com,port:443],path:/search/users,"
    "query:[q=duckdb+in%3Alogin,per_page=3],headers:[Accept=application/vnd.github+json,"
    "User-Agent=duckdb-api/0.6.0,X-GitHub-Api-Version=2022-11-28];response=source:json_path_many,"
    "records:$.items[*];features=retry:disabled,pagination:disabled;authentication=requirement:none,"
    "logical_credential:none,authenticator:none,destination:none,placement:none;"
    "ceilings=response_bytes_per_page:65536,response_bytes_per_scan:65536,records_per_page:3,"
    "records_per_scan:3,extracted_string_bytes:256";

const char AUTHENTICATED_SOURCE_SNAPSHOT[] =
    "relation=authenticated_user;schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,site_admin:BOOLEAN!:$.site_admin;"
    "predicate_mappings=[];"
    "operation=github_authenticated_user:fallback:exactly_one_on_success:REST:GET:replay_safe;"
    "request=origin:[scheme:https,host:api.github.com,port:443],path:/user,query:[],"
    "headers:[Accept=application/vnd.github+json,User-Agent=duckdb-api/0.6.0,"
    "X-GitHub-Api-Version=2022-11-28];response=source:root_object,records:$;"
    "features=retry:disabled,pagination:disabled;authentication=requirement:required,logical_credential:token,"
    "authenticator:bearer,destination:[scheme:https,host:api.github.com,port:443],placement:Authorization;"
    "ceilings=response_bytes_per_page:65536,response_bytes_per_scan:65536,records_per_page:1,"
    "records_per_scan:1,extracted_string_bytes:256";

const char REPOSITORY_SOURCE_SNAPSHOT[] =
    "relation=authenticated_repositories;schema=id:BIGINT!:$.id,full_name:VARCHAR!:$.full_name,"
    "private:BOOLEAN!:$.private,fork:BOOLEAN!:$.fork,archived:BOOLEAN!:$.archived,"
    "visibility:VARCHAR!:$.visibility;"
    "predicate_mappings=[{column:visibility,operator:equals,literal:varchar:private,"
    "operation:github_authenticated_repositories,input:rest_query:visibility=private,accuracy:superset,"
    "proof:github_rest_2022_11_28_repository_visibility,"
    "base_domain:github_authenticated_repository_occurrences,occurrences:all_matching_base_occurrences,"
    "encoding:single_positive_rest_query_input[max_inputs:1,compound_and:unsupported,or:unsupported,"
    "not:unsupported]}];"
    "operation=github_authenticated_repositories:fallback:zero_to_many:REST:GET:replay_safe;"
    "request=origin:[scheme:https,host:api.github.com,port:443],path:/user/repos,"
    "query:[per_page=100,page=1],headers:[Accept=application/vnd.github+json,User-Agent=duckdb-api/0.6.0,"
    "X-GitHub-Api-Version=2022-11-28];response=source:root_array,records:$;"
    "features=retry:disabled,pagination:link_header[relation:next,dependency:sequential,consistency:mutable,"
    "total:none,resume:none,page_size:per_page=100,page_number:page=1,increment:1,"
    "target:exact_operation_origin_and_path,max_pages:32];authentication=requirement:required,"
    "logical_credential:token,authenticator:bearer,"
    "destination:[scheme:https,host:api.github.com,port:443],placement:Authorization;"
    "ceilings=response_bytes_per_page:8388608,response_bytes_per_scan:67108864,records_per_page:100,"
    "records_per_scan:3200,extracted_string_bytes:512";

} // namespace

// Semantics owns this non-installable builder. It constructs only closed,
// literal fixtures and publishes them as immutable ScanPlan values. Runtime
// links the resulting provider object without importing Connector, Query, or
// planner construction services; Semantics' focused tests independently
// compare these values with planner-produced plans.
class ScanPlanFixtureBuilder {
public:
	static duckdb_api::ScanPlan Anonymous();
	static duckdb_api::ScanPlan Authenticated(const std::string &secret_name);
	static duckdb_api::ScanPlan Repository(const std::string &secret_name,
	                                       duckdb_api::PredicateDecisionCategory predicate_category,
	                                       bool complete_residual);
	static duckdb_api::ScanPlan GenericPagination(const std::string &secret_name);
	static duckdb_api::ScanPlan Graphql(const std::string &secret_name);

private:
	static duckdb_api::ScanPlan Common(std::string connector, std::string version, std::string relation,
	                                   std::string source_snapshot);
	static void RequireBearer(duckdb_api::ScanPlan &plan, const std::string &secret_name);
	static void EnablePagination(duckdb_api::ScanPlan &plan, std::string page_size_parameter, uint64_t page_size,
	                             std::string page_number_parameter, uint64_t max_pages,
	                             uint64_t response_bytes_per_page, uint64_t response_bytes_per_scan,
	                             uint64_t records_per_page, uint64_t records_per_scan, uint64_t extracted_string_bytes);
	static void SetRestOperation(duckdb_api::ScanPlan &plan, duckdb_api::PlannedRestOperation operation);
};

void ScanPlanFixtureBuilder::SetRestOperation(duckdb_api::ScanPlan &plan, duckdb_api::PlannedRestOperation operation) {
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromRest(std::move(operation)));
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::Common(std::string connector, std::string version, std::string relation,
                                                    std::string source_snapshot) {
	duckdb_api::ScanPlan plan;
	plan.connector_name = std::move(connector);
	plan.connector_version = std::move(version);
	plan.relation_name = std::move(relation);
	plan.source_snapshot = std::move(source_snapshot);
	plan.remote_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::UNSUPPORTED;
	plan.residual_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	plan.residual_owner = duckdb_api::RelationalOwner::DUCKDB;
	plan.conditional_input = duckdb_api::PlannedConditionalInput::NONE;
	plan.predicate_category = duckdb_api::PredicateDecisionCategory::UNSUPPORTED;
	plan.predicate_reason = duckdb_api::PredicateDecisionReason::NO_REMOTE_CANDIDATE;
	plan.ownership = {duckdb_api::RelationalOwner::DUCKDB, duckdb_api::RelationalOwner::DUCKDB,
	                  duckdb_api::RelationalOwner::DUCKDB, duckdb_api::RelationalOwner::DUCKDB,
	                  duckdb_api::RelationalOwner::DUCKDB};
	plan.remote_ordering = duckdb_api::RelationalDelegation::NONE;
	plan.runtime_ordering = duckdb_api::RelationalDelegation::NONE;
	plan.remote_limit = duckdb_api::RelationalDelegation::NONE;
	plan.remote_offset = duckdb_api::RelationalDelegation::NONE;
	plan.runtime_limit = duckdb_api::RelationalDelegation::NONE;
	plan.runtime_offset = duckdb_api::RelationalDelegation::NONE;
	plan.providers = duckdb_api::FeatureState::DISABLED;
	plan.retry = duckdb_api::FeatureState::DISABLED;
	plan.cache = duckdb_api::FeatureState::DISABLED;
	plan.authentication = duckdb_api::FeatureState::DISABLED;
	plan.network = {{"https"}, {"api.github.com"}, false, false, false, false};
	plan.classification_reason = "closed Semantics plan fixture";
	return plan;
}

void ScanPlanFixtureBuilder::RequireBearer(duckdb_api::ScanPlan &plan, const std::string &secret_name) {
	plan.authentication = duckdb_api::FeatureState::ENABLED;
	plan.secret_reference = duckdb_api::PlannedSecretReference(secret_name);
	plan.authentication_obligation.requirement = duckdb_api::PlannedCredentialRequirement::REQUIRED;
	plan.authentication_obligation.logical_credential = "token";
	plan.authentication_obligation.authenticator = duckdb_api::PlannedAuthenticator::BEARER;
	plan.authentication_obligation.placement = duckdb_api::PlannedCredentialPlacement::AUTHORIZATION_HEADER;
	plan.authentication_obligation.has_destination = true;
	plan.authentication_obligation.destination = {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443};
}

void ScanPlanFixtureBuilder::EnablePagination(duckdb_api::ScanPlan &plan, std::string page_size_parameter,
                                              uint64_t page_size, std::string page_number_parameter, uint64_t max_pages,
                                              uint64_t response_bytes_per_page, uint64_t response_bytes_per_scan,
                                              uint64_t records_per_page, uint64_t records_per_scan,
                                              uint64_t extracted_string_bytes) {
	plan.pagination.strategy = duckdb_api::PlannedPaginationStrategy::LINK_HEADER;
	plan.pagination.dependency = duckdb_api::PlannedPageDependency::SEQUENTIAL;
	plan.pagination.consistency = duckdb_api::PlannedPageConsistency::MUTABLE;
	plan.pagination.link_relation = duckdb_api::PlannedLinkRelation::NEXT;
	plan.pagination.target_scope = duckdb_api::PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH;
	plan.pagination.supports_total = false;
	plan.pagination.supports_resume = false;
	plan.pagination.target = {plan.Operation().Rest().origin,
	                          plan.Operation().Rest().path,
	                          std::move(page_size_parameter),
	                          page_size,
	                          std::move(page_number_parameter),
	                          1,
	                          1};
	plan.pagination.page_budgets = {duckdb_api::PAGINATION_MAX_REQUEST_ATTEMPTS_PER_PAGE,
	                                response_bytes_per_page,
	                                duckdb_api::PAGINATION_MAX_HEADER_BYTES_PER_PAGE,
	                                duckdb_api::PAGINATION_MAX_DECOMPRESSED_BYTES_PER_PAGE,
	                                records_per_page,
	                                extracted_string_bytes,
	                                duckdb_api::PAGINATION_MAX_JSON_NESTING,
	                                duckdb_api::PAGINATION_MAX_DECODED_MEMORY_BYTES,
	                                duckdb_api::PAGINATION_OUTPUT_BATCH_ROWS,
	                                duckdb_api::PAGINATION_MAX_EXECUTION_MILLISECONDS,
	                                duckdb_api::PAGINATION_MAX_CONCURRENCY,
	                                0};
	plan.pagination.scan_budgets = {max_pages,
	                                max_pages,
	                                response_bytes_per_scan,
	                                std::min(duckdb_api::PAGINATION_MAX_HEADER_BYTES_PER_PAGE * max_pages,
	                                         duckdb_api::PAGINATION_MAX_HEADER_BYTES_PER_SCAN),
	                                std::min(duckdb_api::PAGINATION_MAX_DECOMPRESSED_BYTES_PER_PAGE * max_pages,
	                                         duckdb_api::PAGINATION_MAX_DECOMPRESSED_BYTES_PER_SCAN),
	                                records_per_scan,
	                                extracted_string_bytes,
	                                duckdb_api::PAGINATION_MAX_JSON_NESTING,
	                                duckdb_api::PAGINATION_MAX_DECODED_MEMORY_BYTES,
	                                duckdb_api::PAGINATION_OUTPUT_BATCH_ROWS,
	                                duckdb_api::PAGINATION_MAX_EXECUTION_MILLISECONDS,
	                                duckdb_api::PAGINATION_MAX_CONCURRENCY,
	                                0};
	plan.budgets = plan.pagination.page_budgets;
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::Anonymous() {
	auto plan = Common("github", "0.7.0", "duckdb_login_search_page", ANONYMOUS_SOURCE_SNAPSHOT);
	plan.domain = duckdb_api::BaseDomain::JSON_PATH_RECORDS;
	SetRestOperation(plan, {"github_search_duckdb_login_page",
	                        duckdb_api::PlannedHttpMethod::GET,
	                        duckdb_api::PlannedCardinality::ZERO_TO_MANY,
	                        duckdb_api::PlannedReplaySafety::SAFE,
	                        {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	                        "/search/users",
	                        {{"q", "duckdb+in%3Alogin"}, {"per_page", "3"}},
	                        {{"Accept", "application/vnd.github+json"},
	                         {"User-Agent", "duckdb-api/0.6.0"},
	                         {"X-GitHub-Api-Version", "2022-11-28"}},
	                        duckdb_api::PlannedResponseSource::JSON_PATH_MANY,
	                        "$.items[*]"});
	plan.output_columns = {{"id", "BIGINT", false, "$.id"},
	                       {"login", "VARCHAR", false, "$.login"},
	                       {"site_admin", "BOOLEAN", false, "$.site_admin"}};
	plan.budgets = {1, 65536, 16384, 65536, 3, 256, 16, 131072, 2, 5000, 1, 0};
	return plan;
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::Authenticated(const std::string &secret_name) {
	auto plan = Common("github", "0.7.0", "authenticated_user", AUTHENTICATED_SOURCE_SNAPSHOT);
	plan.domain = duckdb_api::BaseDomain::SUCCESSFUL_ROOT_OBJECT;
	SetRestOperation(plan, {"github_authenticated_user",
	                        duckdb_api::PlannedHttpMethod::GET,
	                        duckdb_api::PlannedCardinality::EXACTLY_ONE_ON_SUCCESS,
	                        duckdb_api::PlannedReplaySafety::SAFE,
	                        {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	                        "/user",
	                        {},
	                        {{"Accept", "application/vnd.github+json"},
	                         {"User-Agent", "duckdb-api/0.6.0"},
	                         {"X-GitHub-Api-Version", "2022-11-28"}},
	                        duckdb_api::PlannedResponseSource::ROOT_OBJECT,
	                        "$"});
	plan.output_columns = {{"id", "BIGINT", false, "$.id"},
	                       {"login", "VARCHAR", false, "$.login"},
	                       {"site_admin", "BOOLEAN", false, "$.site_admin"}};
	plan.budgets = {1, 65536, 16384, 65536, 1, 256, 16, 131072, 2, 5000, 1, 0};
	RequireBearer(plan, secret_name);
	return plan;
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::Repository(const std::string &secret_name,
                                                        duckdb_api::PredicateDecisionCategory predicate_category,
                                                        bool complete_residual) {
	auto plan = Common("github", "0.7.0", "authenticated_repositories", REPOSITORY_SOURCE_SNAPSHOT);
	plan.domain = duckdb_api::BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS;
	SetRestOperation(plan, {"github_authenticated_repositories",
	                        duckdb_api::PlannedHttpMethod::GET,
	                        duckdb_api::PlannedCardinality::ZERO_TO_MANY,
	                        duckdb_api::PlannedReplaySafety::SAFE,
	                        {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	                        "/user/repos",
	                        {{"per_page", "100"}, {"page", "1"}},
	                        {{"Accept", "application/vnd.github+json"},
	                         {"User-Agent", "duckdb-api/0.6.0"},
	                         {"X-GitHub-Api-Version", "2022-11-28"}},
	                        duckdb_api::PlannedResponseSource::ROOT_ARRAY,
	                        "$"});
	plan.output_columns = {{"id", "BIGINT", false, "$.id"},
	                       {"full_name", "VARCHAR", false, "$.full_name"},
	                       {"private", "BOOLEAN", false, "$.private"},
	                       {"fork", "BOOLEAN", false, "$.fork"},
	                       {"archived", "BOOLEAN", false, "$.archived"},
	                       {"visibility", "VARCHAR", false, "$.visibility"}};
	EnablePagination(plan, "per_page", 100, "page", 32, 8 * 1024 * 1024, 64 * 1024 * 1024, 100, 3200, 512);
	RequireBearer(plan, secret_name);
	if (predicate_category == duckdb_api::PredicateDecisionCategory::SUPERSET) {
		plan.remote_predicate = duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE;
		plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::SUPERSET;
		plan.conditional_input = duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE;
		plan.predicate_category = predicate_category;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::SELECTED_SUPERSET_MAPPING;
	} else if (predicate_category == duckdb_api::PredicateDecisionCategory::AMBIGUOUS) {
		plan.predicate_category = predicate_category;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::AMBIGUOUS_CONDITIONAL_INPUT;
	}
	if (complete_residual) {
		plan.residual_predicate = duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER;
	} else if (predicate_category == duckdb_api::PredicateDecisionCategory::SUPERSET) {
		plan.residual_predicate = duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE;
	}
	return plan;
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::GenericPagination(const std::string &secret_name) {
	auto plan = Common("fixture_pagination_catalog", "test-1", "fixture_explicit_link_records",
	                   "fixture:explicit-link-records");
	plan.domain = duckdb_api::BaseDomain::PAGINATED_JSON_PATH_RECORDS;
	SetRestOperation(plan, {"fixture_explicit_link_records",
	                        duckdb_api::PlannedHttpMethod::GET,
	                        duckdb_api::PlannedCardinality::ZERO_TO_MANY,
	                        duckdb_api::PlannedReplaySafety::SAFE,
	                        {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	                        "/fixtures/linked-records",
	                        {{"batch_size", "3"}, {"cursor_page", "1"}},
	                        {{"X-Connector-Fixture", "pagination-shape"}},
	                        duckdb_api::PlannedResponseSource::JSON_PATH_MANY,
	                        "$.records[*]"});
	plan.output_columns = {{"record_id", "BIGINT", false, "$.record_id"},
	                       {"record_label", "VARCHAR", false, "$.record_label"}};
	EnablePagination(plan, "batch_size", 3, "cursor_page", 4, 1024, 4096, 3, 12, 96);
	RequireBearer(plan, secret_name);
	return plan;
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::Graphql(const std::string &secret_name) {
	static const std::string DOCUMENT = "query DuckdbApiViewerRepositoryMetrics($pageSize: Int!, $cursor: String) {\n"
	                                    "  viewer {\n"
	                                    "    repositories(\n"
	                                    "      first: $pageSize\n"
	                                    "      after: $cursor\n"
	                                    "      affiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]\n"
	                                    "      ownerAffiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]\n"
	                                    "      orderBy: {field: UPDATED_AT, direction: DESC}\n"
	                                    "    ) {\n"
	                                    "      nodes {\n"
	                                    "        id\n"
	                                    "        nameWithOwner\n"
	                                    "        owner { login }\n"
	                                    "        stargazerCount\n"
	                                    "        primaryLanguage { name }\n"
	                                    "        isPrivate\n"
	                                    "        isArchived\n"
	                                    "        updatedAt\n"
	                                    "      }\n"
	                                    "      pageInfo { hasNextPage endCursor }\n"
	                                    "    }\n"
	                                    "  }\n"
	                                    "}";
	auto plan = Common("canonical_graphql_fixture", "test-graphql-v1", "viewer_repository_metrics",
	                   "fixture:canonical-graphql-viewer-repository-metrics");
	plan.domain = duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES;
	duckdb_api::PlannedGraphqlOperation operation {
	    "github_viewer_repository_metrics",
	    duckdb_api::PlannedCardinality::ZERO_TO_MANY,
	    duckdb_api::PlannedReplaySafety::SAFE,
	    duckdb_api::PlannedGraphqlOperationKind::QUERY,
	    duckdb_api::PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1,
	    DOCUMENT,
	    duckdb_api::PlannedGraphqlDigestAlgorithm::SHA256,
	    "9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85",
	    {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	    "/graphql",
	    {{"Accept", "application/vnd.github+json"},
	     {"Content-Type", "application/json"},
	     {"User-Agent", "duckdb-api/0.7.0"},
	     {"X-GitHub-Api-Version", "2022-11-28"}},
	    {{"pageSize", duckdb_api::PlannedGraphqlVariableType::INT_NON_NULL,
	      duckdb_api::PlannedGraphqlVariableSource::FIXED_PAGE_SIZE, 100},
	     {"cursor", duckdb_api::PlannedGraphqlVariableType::STRING_NULLABLE,
	      duckdb_api::PlannedGraphqlVariableSource::RUNTIME_CURSOR, 0}},
	    {{"id", duckdb_api::PlannedGraphqlScalarKind::STRING, false, {{"id"}}},
	     {"full_name", duckdb_api::PlannedGraphqlScalarKind::STRING, false, {{"nameWithOwner"}}},
	     {"owner_login", duckdb_api::PlannedGraphqlScalarKind::STRING, false, {{"owner", "login"}}},
	     {"stars", duckdb_api::PlannedGraphqlScalarKind::INT64, false, {{"stargazerCount"}}},
	     {"primary_language", duckdb_api::PlannedGraphqlScalarKind::STRING, true, {{"primaryLanguage", "name"}}},
	     {"private", duckdb_api::PlannedGraphqlScalarKind::BOOLEAN, false, {{"isPrivate"}}},
	     {"archived", duckdb_api::PlannedGraphqlScalarKind::BOOLEAN, false, {{"isArchived"}}},
	     {"updated_at", duckdb_api::PlannedGraphqlScalarKind::STRING, false, {{"updatedAt"}}}},
	    {{{"data", "viewer", "repositories", "nodes"}},
	     {{"errors"}},
	     {{"data", "viewer", "repositories", "pageInfo"}},
	     duckdb_api::PlannedGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR},
	    {duckdb_api::PlannedGraphqlCursorDirection::FORWARD,
	     duckdb_api::PlannedGraphqlCursorDependency::SEQUENTIAL,
	     duckdb_api::PlannedGraphqlCursorConsistency::MUTABLE,
	     false,
	     false,
	     1,
	     "pageSize",
	     100,
	     "cursor",
	     {{"data", "viewer", "repositories", "pageInfo", "hasNextPage"}},
	     {{"data", "viewer", "repositories", "pageInfo", "endCursor"}},
	     32},
	    4096,
	    8 * 1024,
	    256 * 1024};
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromGraphql(std::move(operation)));
	plan.output_columns = {{"id", "VARCHAR", false, "$.id"},
	                       {"full_name", "VARCHAR", false, "$.nameWithOwner"},
	                       {"owner_login", "VARCHAR", false, "$.owner.login"},
	                       {"stars", "BIGINT", false, "$.stargazerCount"},
	                       {"primary_language", "VARCHAR", true, "$.primaryLanguage.name"},
	                       {"private", "BOOLEAN", false, "$.isPrivate"},
	                       {"archived", "BOOLEAN", false, "$.isArchived"},
	                       {"updated_at", "VARCHAR", false, "$.updatedAt"}};
	plan.pagination.strategy = duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR;
	plan.pagination.graphql_cursor = plan.Operation().Graphql().cursor;
	plan.pagination.page_budgets = {1,  8 * 1024 * 1024, 16 * 1024, 8 * 1024 * 1024, 100, 512,
	                                16, 2 * 1024 * 1024, 64,        30000,           1,   8 * 1024};
	plan.pagination.scan_budgets = {
	    32,    32, 64 * 1024 * 1024, 512 * 1024, 64 * 1024 * 1024, 3200, 512, 16, 2 * 1024 * 1024, 64,
	    30000, 1,  256 * 1024};
	plan.budgets = plan.pagination.page_budgets;
	RequireBearer(plan, secret_name);
	plan.classification_reason =
	    "no structured remote candidate was offered; the complete base-domain traversal remains authoritative; "
	    "canonical viewer.repositories traversal defines a duplicate-preserving mutable occurrence bag; fixed "
	    "UPDATED_AT DESC enumerates cursors but grants no DuckDB ordering or snapshot; body and row ceilings grant "
	    "no limit or truncation authority; DuckDB retains every relational operator";
	return plan;
}

duckdb_api::ScanPlan BuildValidAnonymousPlanFixture() {
	return ScanPlanFixtureBuilder::Anonymous();
}

duckdb_api::ScanPlan BuildValidAuthenticatedPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Authenticated(exact_logical_secret_name);
}

duckdb_api::ScanPlan BuildValidPaginatedPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::GenericPagination(exact_logical_secret_name);
}

duckdb_api::ScanPlan BuildValidAuthenticatedRepositoriesPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Repository(exact_logical_secret_name,
	                                          duckdb_api::PredicateDecisionCategory::UNSUPPORTED, false);
}

duckdb_api::ScanPlan BuildVisibilityPrivatePlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Repository(exact_logical_secret_name,
	                                          duckdb_api::PredicateDecisionCategory::SUPERSET, false);
}

duckdb_api::ScanPlan BuildVisibilityPrivateCompleteResidualPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Repository(exact_logical_secret_name,
	                                          duckdb_api::PredicateDecisionCategory::SUPERSET, true);
}

duckdb_api::ScanPlan BuildCompleteResidualFallbackPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Repository(exact_logical_secret_name,
	                                          duckdb_api::PredicateDecisionCategory::UNSUPPORTED, true);
}

duckdb_api::ScanPlan BuildAmbiguousPredicateFallbackPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Repository(exact_logical_secret_name,
	                                          duckdb_api::PredicateDecisionCategory::AMBIGUOUS, true);
}

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixtureImpl(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::Graphql(exact_logical_secret_name);
}

} // namespace duckdb_api_test
