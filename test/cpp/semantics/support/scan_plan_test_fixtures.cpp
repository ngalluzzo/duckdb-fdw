#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace duckdb_api_test {
namespace {

const char ANONYMOUS_SOURCE_SNAPSHOT[] =
    "relation=duckdb_login_search_page;schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,"
    "site_admin:BOOLEAN!:$.site_admin;predicate_mappings=[];"
    "operation=github_search_duckdb_login_page:fallback:zero_to_many:REST:GET:"
    "replay_safe;request=origin:[scheme:https,host:api.github.com,port:443],path:/search/users,"
    "query:[q=fixed.VARCHAR:duckdb+in%3Alogin,per_page=fixed.VARCHAR:3],headers:[Accept=application/vnd.github+json,"
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
    "query:[per_page=page_size.BIGINT:100,page=page_number.BIGINT:1],headers:[Accept=application/"
    "vnd.github+json,User-Agent=duckdb-api/0.6.0,"
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
	static duckdb_api::ScanPlan DistinctRestQueryPath(const std::string &secret_name);
	static duckdb_api::ScanPlan Graphql(const std::string *secret_name, GraphqlLocalResidualProfile profile);
	static bool RejectsRestQueryBinding(RestQueryBindingConstructionCounterexample counterexample);
	static bool RejectsPackagePredicateMaterialization(PackagePredicatePlanCounterexample counterexample);

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

duckdb_api::ScanPlan ScanPlanFixtureBuilder::DistinctRestQueryPath(const std::string &secret_name) {
	auto plan = Common("package_rest_fixture", "1.2.3", "activity_records",
	                   "package=package_rest_fixture@1.2.3;relation=activity_records;"
	                   "operation=package_activity_records;profile=typed_rest_materialization");
	plan.domain = duckdb_api::BaseDomain::PAGINATED_JSON_PATH_RECORDS;
	std::vector<duckdb_api::PlannedRestQueryBinding> bindings;
	bindings.push_back(duckdb_api::PlannedRestQueryBinding(
	    "view", duckdb_api::PlannedRestQueryValueSource::FIXED, "", duckdb_api::PlannedRestScalarKind::VARCHAR, false,
	    0, "summary", duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "summary"));
	bindings.push_back(duckdb_api::PlannedRestQueryBinding("empty_tag", duckdb_api::PlannedRestQueryValueSource::FIXED,
	                                                       "", duckdb_api::PlannedRestScalarKind::VARCHAR, false, 0, "",
	                                                       duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, ""));
	bindings.push_back(
	    duckdb_api::PlannedRestQueryBinding("include_archived", duckdb_api::PlannedRestQueryValueSource::RELATION_INPUT,
	                                        "include_archived", duckdb_api::PlannedRestScalarKind::BOOLEAN, false, 0,
	                                        "", duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "false"));
	bindings.push_back(
	    duckdb_api::PlannedRestQueryBinding("min_rank", duckdb_api::PlannedRestQueryValueSource::RELATION_INPUT,
	                                        "minimum_rank", duckdb_api::PlannedRestScalarKind::BIGINT, false, 42, "",
	                                        duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "42"));
	bindings.push_back(duckdb_api::PlannedRestQueryBinding(
	    "label_filter", duckdb_api::PlannedRestQueryValueSource::RELATION_INPUT, "label",
	    duckdb_api::PlannedRestScalarKind::VARCHAR, false, 0, "north america/β",
	    duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "north+america%2F%CE%B2"));
	bindings.push_back(duckdb_api::PlannedRestQueryBinding(
	    "access", duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT, "visibility",
	    duckdb_api::PlannedRestScalarKind::VARCHAR, false, 0, "private",
	    duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "private"));
	bindings.push_back(
	    duckdb_api::PlannedRestQueryBinding("page_size", duckdb_api::PlannedRestQueryValueSource::PAGINATION_PAGE_SIZE,
	                                        "", duckdb_api::PlannedRestScalarKind::BIGINT, false, 25, "",
	                                        duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "25"));
	bindings.push_back(
	    duckdb_api::PlannedRestQueryBinding("page", duckdb_api::PlannedRestQueryValueSource::PAGINATION_PAGE_NUMBER, "",
	                                        duckdb_api::PlannedRestScalarKind::BIGINT, false, 1, "",
	                                        duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED, "1"));
	SetRestOperation(plan, {"package_activity_records",
	                        duckdb_api::PlannedHttpMethod::GET,
	                        duckdb_api::PlannedCardinality::ZERO_TO_MANY,
	                        duckdb_api::PlannedReplaySafety::SAFE,
	                        {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443},
	                        "/fixtures/activity-records",
	                        {{"compat_query_not_runtime_authority", "decoy"}},
	                        {{"Accept", "application/json"}},
	                        duckdb_api::PlannedResponseSource::JSON_PATH_MANY,
	                        "compat-records-path-not-runtime-authority",
	                        std::move(bindings),
	                        {{"payload", "records"}},
	                        {{"record_id", duckdb_api::PlannedRestScalarKind::BIGINT, false, {{"identity", "id"}}},
	                         {"label", duckdb_api::PlannedRestScalarKind::VARCHAR, true, {{"attributes", "label"}}},
	                         {"active", duckdb_api::PlannedRestScalarKind::BOOLEAN, false, {{"flags", "active"}}}}});
	plan.output_columns = {{"record_id", "compat-bigint", false, "compat-record-id-path"},
	                       {"label", "compat-varchar", true, "compat-label-path"},
	                       {"active", "compat-boolean", false, "compat-active-path"}};
	EnablePagination(plan, "page_size", 25, "page", 4, 1024, 4096, 25, 100, 128);
	RequireBearer(plan, secret_name);
	plan.remote_predicate = duckdb_api::PlannedPredicate::TYPED_EQUALITY;
	plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::SUPERSET;
	plan.residual_predicate = duckdb_api::PlannedPredicate::TYPED_EQUALITY;
	plan.conditional_input = duckdb_api::PlannedConditionalInput::REST_QUERY_BINDING;
	plan.typed_equality =
	    std::shared_ptr<const duckdb_api::PlannedEqualityPredicate>(new duckdb_api::PlannedEqualityPredicate(
	        "label", duckdb_api::PlannedPredicateOperator::EQUALS, duckdb_api::PlannedRestScalarKind::VARCHAR, false, 0,
	        "private", "visibility", "sha256.package-proof-activity-private",
	        "sha256.package-domain-activity-occurrences",
	        duckdb_api::PlannedOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES));
	plan.predicate_category = duckdb_api::PredicateDecisionCategory::SUPERSET;
	plan.predicate_reason = duckdb_api::PredicateDecisionReason::SELECTED_SUPERSET_MAPPING;
	plan.classification_reason =
	    "generic typed equality selects one exact conditional source id while DuckDB retains the predicate";
	plan.ValidatePredicateMaterialization();
	return plan;
}

bool ScanPlanFixtureBuilder::RejectsRestQueryBinding(RestQueryBindingConstructionCounterexample counterexample) {
	duckdb_api::PlannedRestQueryValueSource source = duckdb_api::PlannedRestQueryValueSource::FIXED;
	std::string source_id;
	duckdb_api::PlannedRestScalarKind kind = duckdb_api::PlannedRestScalarKind::VARCHAR;
	bool boolean_value = false;
	std::int64_t bigint_value = 0;
	std::string varchar_value = "value";
	duckdb_api::PlannedRestQueryEncoding encoding = duckdb_api::PlannedRestQueryEncoding::FORM_URLENCODED;
	std::string encoded_value = "value";
	switch (counterexample) {
	case RestQueryBindingConstructionCounterexample::NONEMPTY_FIXED_SOURCE_ID:
		source = duckdb_api::PlannedRestQueryValueSource::FIXED;
		source_id = "invalid";
		break;
	case RestQueryBindingConstructionCounterexample::EMPTY_RELATION_INPUT_SOURCE_ID:
		source = duckdb_api::PlannedRestQueryValueSource::RELATION_INPUT;
		break;
	case RestQueryBindingConstructionCounterexample::EMPTY_CONDITIONAL_INPUT_SOURCE_ID:
		source = duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT;
		break;
	case RestQueryBindingConstructionCounterexample::NONEMPTY_PAGE_SIZE_SOURCE_ID:
		source = duckdb_api::PlannedRestQueryValueSource::PAGINATION_PAGE_SIZE;
		source_id = "invalid";
		break;
	case RestQueryBindingConstructionCounterexample::NONEMPTY_PAGE_NUMBER_SOURCE_ID:
		source = duckdb_api::PlannedRestQueryValueSource::PAGINATION_PAGE_NUMBER;
		source_id = "invalid";
		break;
	case RestQueryBindingConstructionCounterexample::UNKNOWN_SOURCE:
		source = static_cast<duckdb_api::PlannedRestQueryValueSource>(127);
		break;
	case RestQueryBindingConstructionCounterexample::UNKNOWN_SCALAR_KIND:
		kind = static_cast<duckdb_api::PlannedRestScalarKind>(127);
		break;
	case RestQueryBindingConstructionCounterexample::UNKNOWN_ENCODING:
		encoding = static_cast<duckdb_api::PlannedRestQueryEncoding>(127);
		break;
	case RestQueryBindingConstructionCounterexample::NONCANONICAL_BOOLEAN_PAYLOAD:
		kind = duckdb_api::PlannedRestScalarKind::BOOLEAN;
		bigint_value = 1;
		varchar_value.clear();
		break;
	case RestQueryBindingConstructionCounterexample::NONCANONICAL_BIGINT_PAYLOAD:
		kind = duckdb_api::PlannedRestScalarKind::BIGINT;
		boolean_value = true;
		varchar_value.clear();
		break;
	case RestQueryBindingConstructionCounterexample::NONCANONICAL_VARCHAR_PAYLOAD:
		bigint_value = 1;
		break;
	case RestQueryBindingConstructionCounterexample::BOOLEAN_ENCODED_VALUE_MISMATCH:
		kind = duckdb_api::PlannedRestScalarKind::BOOLEAN;
		varchar_value.clear();
		encoded_value = "true";
		break;
	case RestQueryBindingConstructionCounterexample::BIGINT_ENCODED_VALUE_MISMATCH:
		kind = duckdb_api::PlannedRestScalarKind::BIGINT;
		bigint_value = 42;
		varchar_value.clear();
		encoded_value = "0042";
		break;
	case RestQueryBindingConstructionCounterexample::VARCHAR_ENCODED_VALUE_MISMATCH:
		encoded_value = "other";
		break;
	case RestQueryBindingConstructionCounterexample::INVALID_VARCHAR_UTF8:
		varchar_value = std::string("\xC3\x28", 2);
		encoded_value = "%C3%28";
		break;
	case RestQueryBindingConstructionCounterexample::CONTROL_VARCHAR:
		varchar_value = "line\nbreak";
		encoded_value = "line%0Abreak";
		break;
	default:
		throw std::invalid_argument("unknown planned REST binding constructor-law counterexample");
	}
	try {
		(void)duckdb_api::PlannedRestQueryBinding("field", source, std::move(source_id), kind, boolean_value,
		                                          bigint_value, std::move(varchar_value), encoding,
		                                          std::move(encoded_value));
		return false;
	} catch (const std::invalid_argument &) {
		return true;
	}
}

bool ScanPlanFixtureBuilder::RejectsPackagePredicateMaterialization(PackagePredicatePlanCounterexample counterexample) {
	auto plan = DistinctRestQueryPath("predicate_materialization_law_secret");
	switch (counterexample) {
	case PackagePredicatePlanCounterexample::MISSING_TYPED_EQUALITY:
		plan.typed_equality.reset();
		break;
	case PackagePredicatePlanCounterexample::NATIVE_REMOTE_DISCRIMINANT:
		plan.remote_predicate = duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE;
		break;
	case PackagePredicatePlanCounterexample::CONDITIONAL_INPUT_NONE:
		plan.conditional_input = duckdb_api::PlannedConditionalInput::NONE;
		break;
	case PackagePredicatePlanCounterexample::UNKNOWN_CONDITIONAL_INPUT:
		plan.conditional_input = static_cast<duckdb_api::PlannedConditionalInput>(127);
		break;
	case PackagePredicatePlanCounterexample::RESIDUAL_TRUE:
		plan.residual_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
		break;
	case PackagePredicatePlanCounterexample::ACCURACY_CATEGORY_MISMATCH:
		plan.predicate_category = duckdb_api::PredicateDecisionCategory::EXACT;
		break;
	case PackagePredicatePlanCounterexample::EXACT_WITH_SUPERSET_OCCURRENCE_LAW:
		plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::EXACT;
		plan.predicate_category = duckdb_api::PredicateDecisionCategory::EXACT;
		break;
	case PackagePredicatePlanCounterexample::OTHER_COLUMN:
		plan.typed_equality =
		    std::shared_ptr<const duckdb_api::PlannedEqualityPredicate>(new duckdb_api::PlannedEqualityPredicate(
		        "other_label", duckdb_api::PlannedPredicateOperator::EQUALS, duckdb_api::PlannedRestScalarKind::VARCHAR,
		        false, 0, "private", "visibility", "sha256.package-proof-activity-private",
		        "sha256.package-domain-activity-occurrences",
		        duckdb_api::PlannedOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES));
		break;
	case PackagePredicatePlanCounterexample::OTHER_CONDITIONAL_SOURCE_ID:
		plan.typed_equality =
		    std::shared_ptr<const duckdb_api::PlannedEqualityPredicate>(new duckdb_api::PlannedEqualityPredicate(
		        "label", duckdb_api::PlannedPredicateOperator::EQUALS, duckdb_api::PlannedRestScalarKind::VARCHAR,
		        false, 0, "private", "other_visibility", "sha256.package-proof-activity-private",
		        "sha256.package-domain-activity-occurrences",
		        duckdb_api::PlannedOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES));
		break;
	case PackagePredicatePlanCounterexample::OTHER_TYPED_VALUE:
		plan.typed_equality =
		    std::shared_ptr<const duckdb_api::PlannedEqualityPredicate>(new duckdb_api::PlannedEqualityPredicate(
		        "label", duckdb_api::PlannedPredicateOperator::EQUALS, duckdb_api::PlannedRestScalarKind::VARCHAR,
		        false, 0, "public", "visibility", "sha256.package-proof-activity-private",
		        "sha256.package-domain-activity-occurrences",
		        duckdb_api::PlannedOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES));
		break;
	case PackagePredicatePlanCounterexample::RESIDUAL_ONLY_EMITS_BINDING:
		plan.remote_predicate = duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
		plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::UNSUPPORTED;
		plan.conditional_input = duckdb_api::PlannedConditionalInput::NONE;
		plan.predicate_category = duckdb_api::PredicateDecisionCategory::UNSUPPORTED;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE;
		break;
	default:
		throw std::invalid_argument("unknown package predicate plan-law counterexample");
	}
	try {
		plan.ValidatePredicateMaterialization();
		return false;
	} catch (const std::logic_error &) {
		return true;
	}
}

duckdb_api::ScanPlan ScanPlanFixtureBuilder::Graphql(const std::string *secret_name,
                                                     GraphqlLocalResidualProfile profile) {
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
	if (secret_name != nullptr) {
		RequireBearer(plan, *secret_name);
	}
	const char *predicate_reason = nullptr;
	switch (profile) {
	case GraphqlLocalResidualProfile::UNRESTRICTED:
		predicate_reason =
		    "no structured remote candidate was offered; the complete base-domain traversal remains authoritative";
		break;
	case GraphqlLocalResidualProfile::MAPPING_UNAVAILABLE:
		plan.residual_predicate = duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE;
		predicate_reason =
		    "no validated mapping matches the typed candidate on the selected operation; remote TRUE preserves "
		    "correctness";
		break;
	case GraphqlLocalResidualProfile::STRUCTURE_UNSUPPORTED:
		plan.residual_predicate = duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::STRUCTURE_UNSUPPORTED;
		predicate_reason =
		    "the offered predicate structure has no complete executable remote proof; remote TRUE preserves the "
		    "base domain and DuckDB remains authoritative";
		break;
	case GraphqlLocalResidualProfile::CAPABILITY_UNAVAILABLE:
		plan.residual_predicate = duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER;
		plan.predicate_reason = duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE;
		predicate_reason =
		    "structured inspection or DuckDB residual-retention capability is unavailable; remote TRUE preserves "
		    "correctness";
		break;
	case GraphqlLocalResidualProfile::COUNT:
		throw std::invalid_argument("unknown closed GraphQL local-residual profile");
	}
	plan.classification_reason =
	    std::string(predicate_reason) +
	    "; canonical viewer.repositories traversal defines a duplicate-preserving mutable occurrence bag; fixed "
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

duckdb_api::ScanPlan BuildDistinctRestQueryPathScanPlanFixture(const std::string &exact_logical_secret_name) {
	return ScanPlanFixtureBuilder::DistinctRestQueryPath(exact_logical_secret_name);
}

bool RestQueryBindingConstructionRejects(RestQueryBindingConstructionCounterexample counterexample) {
	return ScanPlanFixtureBuilder::RejectsRestQueryBinding(counterexample);
}

bool PackagePredicateMaterializationRejects(PackagePredicatePlanCounterexample counterexample) {
	return ScanPlanFixtureBuilder::RejectsPackagePredicateMaterialization(counterexample);
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

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixtureImpl(const std::string &exact_logical_secret_name,
                                                          GraphqlLocalResidualProfile profile) {
	return ScanPlanFixtureBuilder::Graphql(&exact_logical_secret_name, profile);
}

duckdb_api::ScanPlan BuildValidAnonymousGraphqlScanPlanFixtureImpl() {
	return ScanPlanFixtureBuilder::Graphql(nullptr, GraphqlLocalResidualProfile::UNRESTRICTED);
}

} // namespace duckdb_api_test
