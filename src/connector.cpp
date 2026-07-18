#include "duckdb_api/connector.hpp"

#include <utility>
#include <vector>

namespace duckdb_api {

CompiledConnector BuildNativeGithubConnector() {
	const CompiledRestOrigin github_origin = {CompiledUrlScheme::HTTPS, CompiledRestHost("api.github.com"), 443};
	const std::vector<CompiledColumn> columns = {{"id", "BIGINT", false, "$.id"},
	                                             {"login", "VARCHAR", false, "$.login"},
	                                             {"site_admin", "BOOLEAN", false, "$.site_admin"}};
	const std::vector<CompiledHttpHeader> headers = {{"Accept", "application/vnd.github+json"},
	                                                 {"User-Agent", "duckdb-api/0.4.0"},
	                                                 {"X-GitHub-Api-Version", "2022-11-28"}};

	std::vector<CompiledRelation> relations;
	relations.push_back(CompiledRelation(
	    "duckdb_login_search_page", columns,
	    CompiledOperation {"github_search_duckdb_login_page",
	                       true,
	                       CompiledOperationCardinality::ZERO_TO_MANY,
	                       CompiledProtocol::REST,
	                       CompiledHttpMethod::GET,
	                       CompiledReplaySafety::SAFE,
	                       false,
	                       false,
	                       {github_origin, "/search/users", {{"q", "duckdb+in%3Alogin"}, {"per_page", "3"}}, headers},
	                       CompiledResponseSource::JSON_PATH_MANY,
	                       "$.items[*]"},
	    CompiledAuthenticationPolicy::Anonymous(), CompiledResourceCeilings {3, 256}));
	relations.push_back(CompiledRelation("authenticated_user", columns,
	                                     CompiledOperation {"github_authenticated_user",
	                                                        true,
	                                                        CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS,
	                                                        CompiledProtocol::REST,
	                                                        CompiledHttpMethod::GET,
	                                                        CompiledReplaySafety::SAFE,
	                                                        false,
	                                                        false,
	                                                        {github_origin, "/user", {}, headers},
	                                                        CompiledResponseSource::ROOT_OBJECT,
	                                                        "$"},
	                                     CompiledAuthenticationPolicy::RequiredBearer(),
	                                     CompiledResourceCeilings {1, 256}));

	return CompiledConnector(CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "github", "0.4.0", std::move(relations),
	                         CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 65536});
}

} // namespace duckdb_api
