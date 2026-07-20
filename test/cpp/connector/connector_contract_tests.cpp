#include "duckdb_api/connector.hpp"
#include "connector/support/catalog_contract.hpp"
#include "connector/support/graphql_contract.hpp"
#include "connector/support/pagination_contract.hpp"
#include "connector/support/predicate_contract.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::Require;

class GroupedDigits final : public std::numpunct<char> {
protected:
	char do_thousands_sep() const override {
		return '_';
	}

	std::string do_grouping() const override {
		return "\3";
	}
};

void RequireColumn(const duckdb_api::CompiledColumn &column, const std::string &name, const std::string &logical_type,
                   const std::string &extractor) {
	Require(column.name == name, "CompiledRelation column name drifted: " + name);
	Require(column.logical_type == logical_type, "CompiledRelation column type drifted: " + name);
	Require(!column.nullable, "CompiledRelation column became nullable: " + name);
	Require(column.extractor == extractor, "CompiledRelation column extractor drifted: " + name);
}

void RequireSharedSchema(const duckdb_api::CompiledRelation &relation) {
	const auto &columns = relation.Columns();
	Require(columns.size() == 3, "CompiledRelation schema width drifted");
	RequireColumn(columns[0], "id", "BIGINT", "$.id");
	RequireColumn(columns[1], "login", "VARCHAR", "$.login");
	RequireColumn(columns[2], "site_admin", "BOOLEAN", "$.site_admin");
}

void RequireRepositorySchema(const duckdb_api::CompiledRelation &relation) {
	const auto &columns = relation.Columns();
	Require(columns.size() == 6, "authenticated repository schema width drifted");
	RequireColumn(columns[0], "id", "BIGINT", "$.id");
	RequireColumn(columns[1], "full_name", "VARCHAR", "$.full_name");
	RequireColumn(columns[2], "private", "BOOLEAN", "$.private");
	RequireColumn(columns[3], "fork", "BOOLEAN", "$.fork");
	RequireColumn(columns[4], "archived", "BOOLEAN", "$.archived");
	RequireColumn(columns[5], "visibility", "VARCHAR", "$.visibility");
}

void RequireQueryParameter(const duckdb_api::CompiledQueryParameter &parameter, const std::string &name,
                           const std::string &encoded_value) {
	Require(parameter.name == name, "CompiledOperation query name drifted: " + name);
	Require(parameter.encoded_value == encoded_value, "CompiledOperation query value drifted: " + name);
}

void RequireHeader(const duckdb_api::CompiledHttpHeader &header, const std::string &name, const std::string &value) {
	Require(header.name == name, "CompiledOperation header name drifted: " + name);
	Require(header.value == value, "CompiledOperation header value drifted: " + name);
}

void RequireFixedHeaders(const std::vector<duckdb_api::CompiledHttpHeader> &headers) {
	Require(headers.size() == 3, "CompiledOperation fixed header width drifted");
	RequireHeader(headers[0], "Accept", "application/vnd.github+json");
	RequireHeader(headers[1], "User-Agent", "duckdb-api/0.6.0");
	RequireHeader(headers[2], "X-GitHub-Api-Version", "2022-11-28");
	for (const auto &header : headers) {
		Require(header.name != "Authorization" && header.name != "authorization",
		        "credential placement leaked into fixed headers");
	}
}

void RequireGithubOrigin(const duckdb_api::CompiledRestOrigin &origin) {
	Require(origin.scheme == duckdb_api::CompiledUrlScheme::HTTPS, "CompiledOperation origin scheme drifted");
	Require(origin.host.Value() == "api.github.com", "CompiledOperation origin host drifted");
	Require(origin.host.Value().find_first_of("/:?#@") == std::string::npos,
	        "CompiledOperation origin host contains URL structure");
	Require(origin.port == 443, "CompiledOperation origin port drifted");
}

void RequireRequiredBearer(const duckdb_api::CompiledRelation &relation) {
	const auto &authentication = relation.Authentication();
	Require(authentication.Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED,
	        "authenticated relation no longer requires a credential");
	Require(authentication.LogicalCredential() == "token", "authenticated logical credential identifier drifted");
	Require(authentication.Authenticator() == duckdb_api::CompiledAuthenticator::BEARER,
	        "authenticated relation authenticator drifted");
	Require(authentication.Placement() == duckdb_api::CompiledCredentialPlacement::AUTHORIZATION_HEADER,
	        "authenticated relation placement drifted");
	Require(authentication.Destination() != nullptr, "authenticated relation lost its credential destination");
	RequireGithubOrigin(*authentication.Destination());
}

void RequireBaseOperation(const duckdb_api::CompiledOperation &operation) {
	const auto &rest = operation.Rest();
	Require(operation.fallback, "CompiledOperation is no longer the fallback");
	Require(operation.Protocol() == duckdb_api::CompiledProtocol::REST, "CompiledOperation protocol drifted");
	Require(rest.method == duckdb_api::CompiledHttpMethod::GET, "CompiledOperation method drifted");
	Require(rest.replay_safety == duckdb_api::CompiledReplaySafety::SAFE, "CompiledOperation replay safety drifted");
	Require(!rest.retry_enabled, "CompiledOperation enabled retry");
	RequireGithubOrigin(rest.request.origin);
	RequireFixedHeaders(rest.request.headers);
}

void TestCatalogAndLookup() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	Require(connector.Origin() == duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA,
	        "CompiledConnector origin drifted");
	Require(connector.ConnectorName() == "github", "CompiledConnector identifier drifted");
	Require(connector.Version() == "0.7.0", "CompiledConnector metadata version drifted");
	Require(connector.Relations().size() == 4, "CompiledConnector relation catalog width drifted");
	Require(connector.Relations()[0].Name() == "duckdb_login_search_page",
	        "CompiledConnector anonymous relation order drifted");
	Require(connector.Relations()[1].Name() == "authenticated_user",
	        "CompiledConnector authenticated relation order drifted");
	Require(connector.Relations()[2].Name() == "authenticated_repositories",
	        "CompiledConnector authenticated repository relation order drifted");
	Require(connector.Relations()[3].Name() == "viewer_repository_metrics",
	        "CompiledConnector GraphQL repository relation order drifted");
	Require(connector.FindRelation("duckdb_login_search_page") == &connector.Relations()[0],
	        "CompiledConnector exact lookup did not return the catalog relation");
	Require(connector.FindRelation("authenticated_user") == &connector.Relations()[1],
	        "CompiledConnector exact lookup did not return the authenticated relation");
	Require(connector.FindRelation("authenticated_repositories") == &connector.Relations()[2],
	        "CompiledConnector exact lookup did not return the authenticated repository relation");
	Require(connector.FindRelation("viewer_repository_metrics") == &connector.Relations()[3],
	        "CompiledConnector exact lookup did not return the GraphQL repository relation");
	Require(connector.FindRelation("Authenticated_User") == nullptr,
	        "CompiledConnector lookup unexpectedly folded relation case");
	Require(connector.FindRelation("Authenticated_Repositories") == nullptr,
	        "CompiledConnector repository lookup unexpectedly folded relation case");
	Require(connector.FindRelation("missing") == nullptr, "CompiledConnector lookup fabricated an unknown relation");
	for (const auto &relation : connector.Relations()) {
		Require(relation.HasSingleOperation() && relation.Operations().size() == 1 &&
		            &relation.Operation() == &relation.Operations()[0],
		        "installed native relation gained another base operation or inconsistent singleton access");
		const auto &selector = relation.Operation().selector;
		Require(relation.Operation().fallback && selector.RequiredInputs().empty() && selector.AnyInputSets().empty() &&
		            selector.ForbiddenInputs().empty() && selector.Priority() == 0,
		        "installed native operation selector behavior drifted");
	}

	const auto &policy = connector.NetworkPolicy();
	Require(policy.allowed_schemes == std::vector<std::string>({"https"}), "CompiledConnector allowed schemes drifted");
	Require(policy.allowed_hosts == std::vector<std::string>({"api.github.com"}),
	        "CompiledConnector allowed hosts drifted");
	Require(!policy.redirects_enabled && !policy.private_addresses_enabled && !policy.link_local_addresses_enabled &&
	            !policy.loopback_addresses_enabled,
	        "CompiledConnector widened network authority");
	Require(policy.max_response_bytes == 8388608, "CompiledConnector response-byte ceiling drifted");
}

void TestAnonymousRelation() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("duckdb_login_search_page");
	Require(relation != nullptr, "anonymous relation disappeared");
	RequireSharedSchema(*relation);
	Require(relation->PredicateMappings().empty(), "anonymous relation gained a predicate mapping");

	const auto &operation = relation->Operation();
	RequireBaseOperation(operation);
	const auto &rest = operation.Rest();
	Require(rest.pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	        "anonymous relation gained pagination");
	Require(operation.name == "github_search_duckdb_login_page", "anonymous operation identifier drifted");
	Require(operation.cardinality == duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        "anonymous operation cardinality drifted");
	Require(rest.request.path == "/search/users", "anonymous request path drifted");
	Require(rest.request.query_parameters.size() == 2, "anonymous fixed query width drifted");
	RequireQueryParameter(rest.request.query_parameters[0], "q", "duckdb+in%3Alogin");
	RequireQueryParameter(rest.request.query_parameters[1], "per_page", "3");
	Require(rest.response_source == duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	        "anonymous response source drifted");
	Require(rest.records_extractor == "$.items[*]", "anonymous response extractor drifted");

	const auto &authentication = relation->Authentication();
	Require(authentication.Requirement() == duckdb_api::CompiledCredentialRequirement::NONE,
	        "anonymous relation unexpectedly requires a credential");
	Require(authentication.LogicalCredential().empty(), "anonymous relation retained a logical credential");
	Require(authentication.Authenticator() == duckdb_api::CompiledAuthenticator::NONE,
	        "anonymous relation retained an authenticator");
	Require(authentication.Placement() == duckdb_api::CompiledCredentialPlacement::NONE,
	        "anonymous relation retained credential placement");
	Require(authentication.Destination() == nullptr, "anonymous relation retained a credential destination");
	Require(relation->ResourceCeilings().HasResponseByteNarrowing() &&
	            relation->ResourceCeilings().MaxResponseBytesPerPage() == 65536 &&
	            relation->ResourceCeilings().MaxResponseBytesPerScan() == 65536 &&
	            relation->ResourceCeilings().MaxRecordsPerPage() == 3 &&
	            relation->ResourceCeilings().MaxRecordsPerScan() == 3,
	        "anonymous resource scope drifted");
	Require(relation->ResourceCeilings().MaxExtractedStringBytes() == 256,
	        "anonymous extracted-string ceiling drifted");
}

void TestAuthenticatedRelation() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("authenticated_user");
	Require(relation != nullptr, "authenticated relation disappeared");
	RequireSharedSchema(*relation);
	Require(relation->PredicateMappings().empty(), "authenticated user relation gained a predicate mapping");

	const auto &operation = relation->Operation();
	RequireBaseOperation(operation);
	const auto &rest = operation.Rest();
	Require(rest.pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::DISABLED,
	        "authenticated user relation gained pagination");
	Require(operation.name == "github_authenticated_user", "authenticated operation identifier drifted");
	Require(operation.cardinality == duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS,
	        "authenticated operation cardinality drifted");
	Require(rest.request.path == "/user", "authenticated request path drifted");
	Require(rest.request.query_parameters.empty(), "authenticated request gained a fixed query");
	Require(rest.response_source == duckdb_api::CompiledResponseSource::ROOT_OBJECT,
	        "authenticated response source drifted");
	Require(rest.records_extractor == "$", "authenticated root-object extractor drifted");

	RequireRequiredBearer(*relation);
	Require(relation->ResourceCeilings().HasResponseByteNarrowing() &&
	            relation->ResourceCeilings().MaxResponseBytesPerPage() == 65536 &&
	            relation->ResourceCeilings().MaxResponseBytesPerScan() == 65536 &&
	            relation->ResourceCeilings().MaxRecordsPerPage() == 1 &&
	            relation->ResourceCeilings().MaxRecordsPerScan() == 1,
	        "authenticated relation lacks a distinct one-record ceiling");
	Require(relation->ResourceCeilings().MaxExtractedStringBytes() == 256,
	        "authenticated extracted-string ceiling drifted");
}

void TestAuthenticatedRepositoriesRelation() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("authenticated_repositories");
	Require(relation != nullptr, "authenticated repository relation disappeared");
	RequireRepositorySchema(*relation);
	Require(relation->PredicateMappings().size() == 1,
	        "authenticated repository relation lost its single predicate mapping");
	const auto &mapping = relation->PredicateMappings()[0];
	Require(mapping.ColumnName() == "visibility" &&
	            mapping.Operator() == duckdb_api::CompiledPredicateOperator::EQUALS &&
	            mapping.Literal() == duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE &&
	            mapping.OperationName() == "github_authenticated_repositories" &&
	            mapping.InputPlacement() == duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER &&
	            mapping.RemoteInputName() == "visibility" && mapping.EncodedRemoteValue() == "private" &&
	            mapping.Accuracy() == duckdb_api::CompiledPredicateAccuracy::SUPERSET &&
	            mapping.ProofIdentity() ==
	                duckdb_api::CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY &&
	            mapping.BaseDomain() ==
	                duckdb_api::CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES &&
	            mapping.OccurrencePreservation() ==
	                duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES &&
	            mapping.EncodingCapability() ==
	                duckdb_api::CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT &&
	            mapping.MaximumConditionalInputs() == 1 && !mapping.SupportsCompoundConjunctionEncoding() &&
	            !mapping.SupportsDisjunctionEncoding() && !mapping.SupportsComplementEncoding(),
	        "authenticated repository predicate mapping drifted");

	const auto &operation = relation->Operation();
	RequireBaseOperation(operation);
	const auto &rest = operation.Rest();
	Require(operation.name == "github_authenticated_repositories",
	        "authenticated repository operation identifier drifted");
	Require(operation.cardinality == duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        "authenticated repository cardinality drifted");
	Require(rest.request.path == "/user/repos", "authenticated repository request path drifted");
	Require(rest.request.query_parameters.size() == 2, "authenticated repository fixed query width drifted");
	RequireQueryParameter(rest.request.query_parameters[0], "per_page", "100");
	RequireQueryParameter(rest.request.query_parameters[1], "page", "1");
	Require(rest.response_source == duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	        "authenticated repository response source drifted");
	Require(rest.records_extractor == "$", "authenticated repository root-array extractor drifted");

	const auto &pagination = rest.pagination;
	Require(pagination.Strategy() == duckdb_api::CompiledPaginationStrategy::LINK_HEADER &&
	            pagination.LinkRelation() == duckdb_api::CompiledLinkRelation::NEXT &&
	            pagination.Dependency() == duckdb_api::CompiledPageDependency::SEQUENTIAL &&
	            pagination.Consistency() == duckdb_api::CompiledPageConsistency::MUTABLE,
	        "authenticated repository pagination profile drifted");
	Require(pagination.PageSizeParameter() == "per_page" && pagination.PageSize() == 100 &&
	            pagination.PageNumberParameter() == "page" && pagination.FirstPage() == 1 &&
	            pagination.PageIncrement() == 1 && pagination.MaxPagesPerScan() == 32,
	        "authenticated repository page bindings drifted");
	Require(pagination.TargetScope() == duckdb_api::CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH &&
	            !pagination.SupportsTotal() && !pagination.SupportsResume(),
	        "authenticated repository continuation authority drifted");

	RequireRequiredBearer(*relation);
	const auto &ceilings = relation->ResourceCeilings();
	Require(ceilings.HasResponseByteNarrowing() && ceilings.MaxResponseBytesPerPage() == 8388608 &&
	            ceilings.MaxResponseBytesPerScan() == 67108864 && ceilings.MaxRecordsPerPage() == 100 &&
	            ceilings.MaxRecordsPerScan() == 3200 && ceilings.MaxExtractedStringBytes() == 512,
	        "authenticated repository resource envelope drifted");
}

const std::string ANONYMOUS_SNAPSHOT =
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

const std::string AUTHENTICATED_SNAPSHOT =
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

const std::string AUTHENTICATED_REPOSITORIES_SNAPSHOT =
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

const std::string GRAPHQL_REPOSITORY_METRICS_SNAPSHOT =
    "relation=viewer_repository_metrics;schema=id:VARCHAR!:$.id,full_name:VARCHAR!:$.nameWithOwner,"
    "owner_login:VARCHAR!:$.owner.login,stars:BIGINT!:$.stargazerCount,"
    "primary_language:VARCHAR?:$.primaryLanguage.name,private:BOOLEAN!:$.isPrivate,"
    "archived:BOOLEAN!:$.isArchived,updated_at:VARCHAR!:$.updatedAt;predicate_mappings=[];"
    "operation=github_viewer_repository_metrics:fallback:zero_to_many:GRAPHQL:"
    "identity:github_viewer_repository_metrics_v1:"
    "sha256:9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85;"
    "endpoint=origin:[scheme:https,host:api.github.com,port:443],path:/graphql,"
    "headers:[Accept=application/vnd.github+json,Content-Type=application/json,"
    "User-Agent=duckdb-api/0.7.0,X-GitHub-Api-Version=2022-11-28];"
    "variables:[pageSize:Int!:fixed_page_size=100,cursor:String:runtime_cursor];"
    "result_columns:[id:string!:id,full_name:string!:nameWithOwner,owner_login:string!:owner.login,"
    "stars:int64!:stargazerCount,primary_language:string?:primaryLanguage.name,private:boolean!:isPrivate,"
    "archived:boolean!:isArchived,updated_at:string!:updatedAt];"
    "response=nodes:data.viewer.repositories.nodes,errors:errors,page_info:data.viewer.repositories.pageInfo,"
    "partial_data:fail;cursor=forward:sequential:mutable,total:none,resume:none,concurrency:1,"
    "page_size:pageSize=100,cursor_variable:cursor,has_next:data.viewer.repositories.pageInfo.hasNextPage,"
    "end_cursor:data.viewer.repositories.pageInfo.endCursor,max_pages:32;body=max_document_bytes:4096,"
    "serialized_bytes_per_request:8192,serialized_bytes_per_scan:262144;"
    "features=retry:disabled,cache:disabled,providers:disabled;authentication=requirement:required,"
    "logical_credential:token,authenticator:bearer,"
    "destination:[scheme:https,host:api.github.com,port:443],placement:Authorization;"
    "ceilings=response_bytes_per_page:8388608,response_bytes_per_scan:67108864,records_per_page:100,"
    "records_per_scan:3200,extracted_string_bytes:512";

void TestCanonicalSnapshotsAndProvenance() {
	const auto first = duckdb_api::BuildNativeGithubConnector();
	const auto second = duckdb_api::BuildNativeGithubConnector();
	const std::string expected =
	    "origin=native_product_metadata;connector=github;version=0.7.0;network=schemes:[https],"
	    "hosts:[api.github.com],redirects:denied,private:denied,link_local:denied,loopback:denied,"
	    "max_response_bytes:8388608;relations=[{" +
	    ANONYMOUS_SNAPSHOT + "},{" + AUTHENTICATED_SNAPSHOT + "},{" + AUTHENTICATED_REPOSITORIES_SNAPSHOT + "},{" +
	    GRAPHQL_REPOSITORY_METRICS_SNAPSHOT + "}]";

	Require(first.Relations()[0].Snapshot() == ANONYMOUS_SNAPSHOT, "anonymous relation snapshot drifted");
	Require(first.Relations()[1].Snapshot() == AUTHENTICATED_SNAPSHOT, "authenticated relation snapshot drifted");
	Require(first.Relations()[2].Snapshot() == AUTHENTICATED_REPOSITORIES_SNAPSHOT,
	        "authenticated repository relation snapshot drifted");
	Require(first.Relations()[3].Snapshot() == GRAPHQL_REPOSITORY_METRICS_SNAPSHOT,
	        "GraphQL repository relation snapshot drifted");
	Require(first.Snapshot() == expected, "CompiledConnector canonical snapshot drifted");
	Require(second.Snapshot() == first.Snapshot(), "CompiledConnector construction is not deterministic");
	const auto copy = first;
	Require(copy.Snapshot() == first.Snapshot(), "CompiledConnector copy changed source metadata");
	Require(&copy.Relations()[0] != &first.Relations()[0], "CompiledConnector copy did not own its relation catalog");

	const std::locale original_locale;
	std::string localized_snapshot;
	try {
		std::locale::global(std::locale(std::locale::classic(), new GroupedDigits()));
		localized_snapshot = first.Snapshot();
		std::locale::global(original_locale);
	} catch (...) {
		std::locale::global(original_locale);
		throw;
	}
	Require(localized_snapshot == expected, "CompiledConnector snapshot depends on the process-global locale");

	const std::vector<std::string> prohibited = {
	    "github_default", "fixture=",          "digest=",        "package_root=", ".yaml",
	    "secret_name=",   "credential_value=", "Authorization=", "response_url=", "Link="};
	for (const auto &value : prohibited) {
		Require(first.Snapshot().find(value) == std::string::npos,
		        "CompiledConnector snapshot retained prohibited provenance or credential material: " + value);
	}
	Require(first.Snapshot().find("origin=native_product_metadata") != std::string::npos,
	        "CompiledConnector snapshot lost native provenance");
	Require(first.Snapshot().find("logical_credential:token") != std::string::npos,
	        "CompiledConnector snapshot lost its safe logical credential requirement");
}

} // namespace

int main() {
	try {
		duckdb_api_test::RunConnectorCatalogContractTests();
		duckdb_api_test::RunConnectorGraphqlContractTests();
		duckdb_api_test::RunConnectorPaginationContractTests();
		duckdb_api_test::RunConnectorPredicateContractTests();
		duckdb_api_test::RunConnectorPredicateProofContractTests();
		TestCatalogAndLookup();
		TestAnonymousRelation();
		TestAuthenticatedRelation();
		TestAuthenticatedRepositoriesRelation();
		TestCanonicalSnapshotsAndProvenance();
		std::cout << "connector contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
