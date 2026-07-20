#include "duckdb_api/connector.hpp"
#include "duckdb_api/internal/connector/compiled_model_builder.hpp"
#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"

#include <utility>
#include <vector>

namespace duckdb_api {

namespace {

// The 0.7.0 catalog-wide response policy covers repository pages. Keep both
// existing relations explicitly narrowed to their accepted 64 KiB effective
// plans instead of allowing them to inherit that wider outer policy.
constexpr std::uint64_t PREVIOUS_RELATION_MAX_RESPONSE_BYTES = 64ULL * 1024ULL;
constexpr std::uint64_t REPOSITORY_MAX_RESPONSE_BYTES_PER_PAGE = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t REPOSITORY_MAX_RESPONSE_BYTES_PER_SCAN = 64ULL * 1024ULL * 1024ULL;

} // namespace

CompiledConnector BuildNativeGithubConnector() {
	using internal::CompiledModelBuilder;
	const CompiledHttpOrigin github_origin = {CompiledUrlScheme::HTTPS, CompiledHttpHost("api.github.com"), 443};
	const std::vector<CompiledColumn> columns = {{"id", "BIGINT", false, "$.id"},
	                                             {"login", "VARCHAR", false, "$.login"},
	                                             {"site_admin", "BOOLEAN", false, "$.site_admin"}};
	const std::vector<CompiledColumn> repository_columns = {{"id", "BIGINT", false, "$.id"},
	                                                        {"full_name", "VARCHAR", false, "$.full_name"},
	                                                        {"private", "BOOLEAN", false, "$.private"},
	                                                        {"fork", "BOOLEAN", false, "$.fork"},
	                                                        {"archived", "BOOLEAN", false, "$.archived"},
	                                                        {"visibility", "VARCHAR", false, "$.visibility"}};
	const std::vector<CompiledColumn> graphql_repository_columns = {
	    {"id", "VARCHAR", false, "$.id"},
	    {"full_name", "VARCHAR", false, "$.nameWithOwner"},
	    {"owner_login", "VARCHAR", false, "$.owner.login"},
	    {"stars", "BIGINT", false, "$.stargazerCount"},
	    {"primary_language", "VARCHAR", true, "$.primaryLanguage.name"},
	    {"private", "BOOLEAN", false, "$.isPrivate"},
	    {"archived", "BOOLEAN", false, "$.isArchived"},
	    {"updated_at", "VARCHAR", false, "$.updatedAt"}};
	const std::vector<CompiledHttpHeader> headers = {{"Accept", "application/vnd.github+json"},
	                                                 {"User-Agent", "duckdb-api/0.6.0"},
	                                                 {"X-GitHub-Api-Version", "2022-11-28"}};
	std::vector<CompiledQueryParameter> search_query;
	search_query.push_back(
	    CompiledModelBuilder::FixedQueryParameter("q", CompiledModelBuilder::Varchar("duckdb in:login")));
	search_query.push_back(CompiledModelBuilder::FixedQueryParameter("per_page", CompiledModelBuilder::Varchar("3")));
	std::vector<CompiledQueryParameter> repository_query;
	repository_query.push_back(CompiledModelBuilder::PageSizeQueryParameter("per_page", 100));
	repository_query.push_back(CompiledModelBuilder::PageNumberQueryParameter("page", 1));

	std::vector<CompiledRelation> relations;
	relations.push_back(
	    CompiledRelation("duckdb_login_search_page", columns, {},
	                     CompiledOperation {"github_search_duckdb_login_page",
	                                        true,
	                                        CompiledOperationCardinality::ZERO_TO_MANY,
	                                        CompiledProtocol::REST,
	                                        CompiledHttpMethod::GET,
	                                        CompiledReplaySafety::SAFE,
	                                        false,
	                                        CompiledPagination::Disabled(),
	                                        {github_origin, "/search/users", std::move(search_query), headers},
	                                        CompiledResponseSource::JSON_PATH_MANY,
	                                        "$.items[*]",
	                                        CompiledOperationSelector()},
	                     CompiledAuthenticationPolicy::Anonymous(),
	                     CompiledResourceCeilings {PREVIOUS_RELATION_MAX_RESPONSE_BYTES,
	                                               PREVIOUS_RELATION_MAX_RESPONSE_BYTES, 3, 3, 256}));
	relations.push_back(CompiledRelation("authenticated_user", columns, {},
	                                     CompiledOperation {"github_authenticated_user",
	                                                        true,
	                                                        CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS,
	                                                        CompiledProtocol::REST,
	                                                        CompiledHttpMethod::GET,
	                                                        CompiledReplaySafety::SAFE,
	                                                        false,
	                                                        CompiledPagination::Disabled(),
	                                                        {github_origin, "/user", {}, headers},
	                                                        CompiledResponseSource::ROOT_OBJECT,
	                                                        "$",
	                                                        CompiledOperationSelector()},
	                                     CompiledAuthenticationPolicy::RequiredBearer(),
	                                     CompiledResourceCeilings {PREVIOUS_RELATION_MAX_RESPONSE_BYTES,
	                                                               PREVIOUS_RELATION_MAX_RESPONSE_BYTES, 1, 1, 256}));
	relations.push_back(CompiledRelation(
	    "authenticated_repositories", repository_columns,
	    {CompiledPredicateMapping("visibility", CompiledPredicateOperator::EQUALS,
	                              CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
	                              CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, "visibility", "private",
	                              CompiledPredicateAccuracy::SUPERSET,
	                              CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
	                              CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES,
	                              CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES,
	                              CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT)},
	    CompiledOperation {"github_authenticated_repositories",
	                       true,
	                       CompiledOperationCardinality::ZERO_TO_MANY,
	                       CompiledProtocol::REST,
	                       CompiledHttpMethod::GET,
	                       CompiledReplaySafety::SAFE,
	                       false,
	                       CompiledPagination("per_page", 100, "page", 1, 1, 32),
	                       {github_origin, "/user/repos", std::move(repository_query), headers},
	                       CompiledResponseSource::ROOT_ARRAY,
	                       "$",
	                       CompiledOperationSelector()},
	    CompiledAuthenticationPolicy::RequiredBearer(),
	    CompiledResourceCeilings {REPOSITORY_MAX_RESPONSE_BYTES_PER_PAGE, REPOSITORY_MAX_RESPONSE_BYTES_PER_SCAN, 100,
	                              3200, 512}));
	relations.push_back(CompiledRelation(
	    "viewer_repository_metrics", graphql_repository_columns, {},
	    CompiledOperation("github_viewer_repository_metrics", true, CompiledOperationCardinality::ZERO_TO_MANY,
	                      internal::BuildCanonicalGithubViewerRepositoryMetricsGraphqlOperation(),
	                      CompiledOperationSelector()),
	    CompiledAuthenticationPolicy::RequiredBearer(),
	    CompiledResourceCeilings {REPOSITORY_MAX_RESPONSE_BYTES_PER_PAGE, REPOSITORY_MAX_RESPONSE_BYTES_PER_SCAN, 100,
	                              3200, 512}));

	return CompiledConnector(
	    CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "github", "0.7.0", std::move(relations),
	    CompiledNetworkPolicy {
	        {"https"}, {"api.github.com"}, false, false, false, false, REPOSITORY_MAX_RESPONSE_BYTES_PER_PAGE});
}

} // namespace duckdb_api
