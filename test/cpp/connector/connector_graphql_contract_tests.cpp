#include "duckdb_api/compiled_protocol_operation.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/content_digest.hpp"
#include "connector/support/catalog_test_access.hpp"
#include "connector/support/graphql_contract.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ConnectorCatalogTestAccess;
using duckdb_api_test::Require;

#define DEFINE_MEMBER_PROBE(PROBE_NAME, MEMBER_NAME)                                                                   \
	template <typename T>                                                                                              \
	class PROBE_NAME {                                                                                                 \
		template <typename U>                                                                                          \
		static char Test(decltype(&U::MEMBER_NAME));                                                                   \
		template <typename U>                                                                                          \
		static long Test(...);                                                                                         \
                                                                                                                       \
	public:                                                                                                            \
		static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);                                                  \
	}

DEFINE_MEMBER_PROBE(HasReplaySafetyMember, replay_safety);
DEFINE_MEMBER_PROBE(HasQueryKindMember, query_kind);
DEFINE_MEMBER_PROBE(HasRemotePredicateMember, remote_predicate);
DEFINE_MEMBER_PROBE(HasRemoteLimitMember, remote_limit);

#undef DEFINE_MEMBER_PROBE

static_assert(!HasReplaySafetyMember<duckdb_api::CompiledGraphqlOperation>::VALUE,
              "GraphQL replay authority must derive from admitted canonical bytes");
static_assert(!HasQueryKindMember<duckdb_api::CompiledGraphqlOperation>::VALUE,
              "GraphQL query kind must not be independently relabeled");
static_assert(!HasRemotePredicateMember<duckdb_api::CompiledGraphqlOperation>::VALUE,
              "GraphQL metadata must not grant remote predicate authority");
static_assert(!HasRemoteLimitMember<duckdb_api::CompiledGraphqlOperation>::VALUE,
              "GraphQL metadata must not grant remote limit authority");
static_assert(std::is_copy_constructible<duckdb_api::CompiledProtocolOperation>::value,
              "immutable protocol alternatives must support catalog copies");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledProtocolOperation>::value,
              "protocol alternative assignment would permit authority replacement");
static_assert(!std::is_default_constructible<duckdb_api::CompiledProtocolOperation>::value,
              "protocol alternatives must not admit an empty tag");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledOperation>::value,
              "published operations must not permit protocol authority replacement");
static_assert(std::is_same<decltype(duckdb_api::CompiledGraphqlOperation::endpoint_origin),
                           duckdb_api::CompiledHttpOrigin>::value,
              "GraphQL endpoint authority must use the protocol-neutral HTTP origin");

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

template <typename Callable>
void RequireLogic(const std::string &message, Callable callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, message);
}

const duckdb_api::CompiledRelation &NativeGraphqlRelation(const duckdb_api::CompiledConnector &connector) {
	const auto *relation = connector.FindRelation("viewer_repository_metrics");
	Require(relation != nullptr, "native GraphQL relation disappeared");
	return *relation;
}

std::string ReplaceOnce(std::string value, const std::string &from, const std::string &to) {
	const auto position = value.find(from);
	if (position == std::string::npos) {
		throw std::logic_error("GraphQL test mutation target disappeared: " + from);
	}
	value.replace(position, from.size(), to);
	return value;
}

duckdb_api::CompiledRelation
BuildGraphqlRelation(duckdb_api::CompiledGraphqlOperation graphql, std::vector<duckdb_api::CompiledColumn> columns = {},
                     duckdb_api::CompiledOperationSelector selector = duckdb_api::CompiledOperationSelector(),
                     duckdb_api::CompiledResourceCeilings ceilings = ConnectorCatalogTestAccess::PaginatedResources(
                         8ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL, 100, 3200, 512)) {
	if (columns.empty()) {
		columns = NativeGraphqlRelation(duckdb_api::BuildNativeGithubConnector()).Columns();
	}
	return ConnectorCatalogTestAccess::Relation(
	    "viewer_repository_metrics", std::move(columns),
	    ConnectorCatalogTestAccess::GraphqlOperation("github_viewer_repository_metrics", true,
	                                                 duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                                 std::move(graphql), std::move(selector)),
	    ConnectorCatalogTestAccess::RequiredBearer(), std::move(ceilings));
}

void TestDigestServiceKnownVectors() {
	Require(duckdb_api::ComputeSha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	        "SHA-256 empty-string vector drifted");
	Require(duckdb_api::ComputeSha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	        "SHA-256 abc vector drifted");
	Require(duckdb_api::ComputeSha256Hex(std::string(1000000, 'a')) ==
	            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
	        "SHA-256 multi-block vector drifted");
}

void TestClosedProtocolAlternative() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &rest = connector.Relations()[0].Operation();
	const auto &graphql = NativeGraphqlRelation(connector).Operation();
	Require(rest.Protocol() == duckdb_api::CompiledProtocol::REST &&
	            rest.ProtocolOperation().Protocol() == duckdb_api::CompiledProtocol::REST,
	        "REST operation lost its explicit protocol alternative");
	Require(graphql.Protocol() == duckdb_api::CompiledProtocol::GRAPHQL &&
	            graphql.ProtocolOperation().Protocol() == duckdb_api::CompiledProtocol::GRAPHQL,
	        "GraphQL operation lost its explicit protocol alternative");
	RequireLogic("REST accessor accepted GraphQL payload", [&graphql]() { (void)graphql.Rest(); });
	RequireLogic("GraphQL accessor accepted REST payload", [&rest]() { (void)rest.Graphql(); });
	RequireInvalid("REST payload accepted a GraphQL tag", [&rest]() {
		const auto &value = rest.Rest();
		duckdb_api::CompiledOperation mismatch(rest.name, rest.fallback, rest.cardinality,
		                                       duckdb_api::CompiledProtocol::GRAPHQL, value.method, value.replay_safety,
		                                       value.retry_enabled, value.pagination, value.request,
		                                       value.response_source, value.records_extractor, rest.selector);
		(void)mismatch;
	});
}

void TestCanonicalGraphqlOperationIsExclusive() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = NativeGraphqlRelation(connector);
	const duckdb_api::CompiledHttpOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledHttpHost("api.github.com"), 443};
	duckdb_api::CompiledOperation extra_rest(
	    "fixture_extra_rest_operation", false, duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	    duckdb_api::CompiledProtocol::REST, duckdb_api::CompiledHttpMethod::GET, duckdb_api::CompiledReplaySafety::SAFE,
	    false, ConnectorCatalogTestAccess::SequentialLink("per_page", 100, "page", 1, 1, 32),
	    {origin,
	     "/user/repos",
	     {ConnectorCatalogTestAccess::PageSizeQuery("per_page", 100),
	      ConnectorCatalogTestAccess::PageNumberQuery("page", 1)},
	     {{"X-Fixture", "safe"}}},
	    duckdb_api::CompiledResponseSource::ROOT_ARRAY, "$", duckdb_api::CompiledOperationSelector());
	RequireInvalid("canonical GraphQL relation accepted an extra valid REST operation", [&relation, &extra_rest]() {
		std::vector<duckdb_api::CompiledOperation> operations = {relation.Operation(), extra_rest};
		ConnectorCatalogTestAccess::Relation(relation.Name(), relation.Columns(), std::move(operations),
		                                     relation.Authentication(), relation.ResourceCeilings());
	});
}

void TestCanonicalGraphqlFactsAndCopy() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = NativeGraphqlRelation(connector);
	const auto &operation = relation.Operation();
	const auto &graphql = operation.Graphql();
	Require(graphql.document_identity ==
	                duckdb_api::CompiledGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 &&
	            graphql.digest_algorithm == duckdb_api::CompiledGraphqlDigestAlgorithm::SHA256 &&
	            graphql.document_digest == "9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85" &&
	            duckdb_api::ComputeSha256Hex(graphql.document) == graphql.document_digest &&
	            duckdb_api::IsCanonicalGraphqlDocumentProfile(graphql.document_identity, graphql.document,
	                                                          graphql.digest_algorithm, graphql.document_digest),
	        "canonical GraphQL identity, bytes, or recomputed digest drifted");
	Require(graphql.document.size() == 581 && graphql.max_document_bytes == 4096,
	        "canonical GraphQL document length or ceiling drifted");
	Require(graphql.endpoint_origin.scheme == duckdb_api::CompiledUrlScheme::HTTPS &&
	            graphql.endpoint_origin.host.Value() == "api.github.com" && graphql.endpoint_origin.port == 443 &&
	            graphql.endpoint_path == "/graphql",
	        "canonical GraphQL endpoint drifted");
	Require(graphql.headers.size() == 4 && graphql.headers[0].name == "Accept" &&
	            graphql.headers[0].value == "application/vnd.github+json" &&
	            graphql.headers[1].name == "Content-Type" && graphql.headers[1].value == "application/json" &&
	            graphql.headers[2].name == "User-Agent" && graphql.headers[2].value == "duckdb-api/0.7.0" &&
	            graphql.headers[3].name == "X-GitHub-Api-Version" && graphql.headers[3].value == "2022-11-28",
	        "canonical GraphQL fixed non-secret metadata drifted");
	Require(graphql.variables.size() == 2 && graphql.variables[0].name == "pageSize" &&
	            graphql.variables[0].type == duckdb_api::CompiledGraphqlVariableType::INT_NON_NULL &&
	            graphql.variables[0].source == duckdb_api::CompiledGraphqlVariableSource::FIXED_PAGE_SIZE &&
	            graphql.variables[0].integer_value == 100 && graphql.variables[1].name == "cursor" &&
	            graphql.variables[1].type == duckdb_api::CompiledGraphqlVariableType::STRING_NULLABLE &&
	            graphql.variables[1].source == duckdb_api::CompiledGraphqlVariableSource::RUNTIME_CURSOR,
	        "canonical GraphQL variable profile drifted");
	Require(graphql.result_columns.size() == 8 && graphql.result_columns[0].name == "id" &&
	            graphql.result_columns[0].scalar_kind == duckdb_api::CompiledGraphqlScalarKind::STRING &&
	            !graphql.result_columns[0].nullable &&
	            graphql.result_columns[0].response_path.segments == std::vector<std::string>({"id"}) &&
	            graphql.result_columns[2].name == "owner_login" &&
	            graphql.result_columns[2].response_path.segments == std::vector<std::string>({"owner", "login"}) &&
	            graphql.result_columns[3].scalar_kind == duckdb_api::CompiledGraphqlScalarKind::INT64 &&
	            graphql.result_columns[4].nullable &&
	            graphql.result_columns[5].scalar_kind == duckdb_api::CompiledGraphqlScalarKind::BOOLEAN,
	        "canonical GraphQL typed result mapping drifted");
	Require(graphql.cursor.dependency == duckdb_api::CompiledGraphqlCursorDependency::SEQUENTIAL &&
	            graphql.cursor.consistency == duckdb_api::CompiledGraphqlCursorConsistency::MUTABLE &&
	            !graphql.cursor.supports_total && !graphql.cursor.supports_resume &&
	            graphql.cursor.max_concurrent_pages == 1 && graphql.cursor.max_pages_per_scan == 32 &&
	            graphql.max_serialized_request_body_bytes_per_request == 8192 &&
	            graphql.max_serialized_request_body_bytes_per_scan == 262144 && !graphql.retry_enabled &&
	            !graphql.cache_enabled && !graphql.providers_enabled,
	        "canonical GraphQL cursor, body, or disabled feature profile drifted");
	Require(graphql.response.nodes.segments == std::vector<std::string>({"data", "viewer", "repositories", "nodes"}) &&
	            graphql.response.errors.segments == std::vector<std::string>({"errors"}) &&
	            graphql.response.page_info.segments ==
	                std::vector<std::string>({"data", "viewer", "repositories", "pageInfo"}) &&
	            graphql.response.partial_data == duckdb_api::CompiledGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR &&
	            graphql.cursor.has_next_page.segments ==
	                std::vector<std::string>({"data", "viewer", "repositories", "pageInfo", "hasNextPage"}) &&
	            graphql.cursor.end_cursor.segments ==
	                std::vector<std::string>({"data", "viewer", "repositories", "pageInfo", "endCursor"}),
	        "canonical GraphQL response or cursor paths drifted");
	Require(relation.Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED &&
	            relation.Authentication().LogicalCredential() == "token" &&
	            relation.Authentication().Authenticator() == duckdb_api::CompiledAuthenticator::BEARER &&
	            relation.Authentication().Placement() ==
	                duckdb_api::CompiledCredentialPlacement::AUTHORIZATION_HEADER &&
	            relation.PredicateMappings().empty(),
	        "canonical GraphQL authentication or relational authority drifted");
	const auto copy = connector;
	const auto &copied = NativeGraphqlRelation(copy).Operation().Graphql();
	Require(copied.document == graphql.document && copied.document_digest == graphql.document_digest &&
	            copy.Snapshot() == connector.Snapshot(),
	        "immutable GraphQL catalog copy changed canonical facts");
}

void TestDocumentAndRequestCounterexamples() {
	auto canonical = NativeGraphqlRelation(duckdb_api::BuildNativeGithubConnector()).Operation().Graphql();
	RequireInvalid("GraphQL accepted an unknown document identity", [canonical]() mutable {
		canonical.document_identity = static_cast<duckdb_api::CompiledGraphqlDocumentIdentity>(255);
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted an empty document", [canonical]() mutable {
		canonical.document.clear();
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted an oversized document", [canonical]() mutable {
		canonical.document.assign(4097, 'q');
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted a digest mismatch", [canonical]() mutable {
		canonical.document_digest[0] = canonical.document_digest[0] == '0' ? '1' : '0';
		BuildGraphqlRelation(std::move(canonical));
	});
	const std::vector<std::string> drifted_documents = {
	    ReplaceOnce(canonical.document, "query DuckdbApiViewerRepositoryMetrics",
	                "mutation DuckdbApiViewerRepositoryMetrics"),
	    ReplaceOnce(canonical.document, "query DuckdbApiViewerRepositoryMetrics",
	                "subscription DuckdbApiViewerRepositoryMetrics"),
	    canonical.document + "\nquery Extra { viewer { login } }",
	    ReplaceOnce(canonical.document, "  viewer {", "  organization(login: \"duckdb\") {"),
	    ReplaceOnce(canonical.document, "      first: $pageSize", "      first: 99"),
	    ReplaceOnce(canonical.document, "      affiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]\n", ""),
	    ReplaceOnce(canonical.document, "        updatedAt", "        updatedAt\n        databaseId"),
	    ReplaceOnce(canonical.document, "      first: $pageSize\n      after: $cursor",
	                "      last: $pageSize\n      before: $cursor")};
	for (const auto &drifted_document : drifted_documents) {
		RequireInvalid("GraphQL accepted canonical document structure drift", [canonical, drifted_document]() mutable {
			canonical.document = drifted_document;
			canonical.document_digest = duckdb_api::ComputeSha256Hex(canonical.document);
			BuildGraphqlRelation(std::move(canonical));
		});
	}
	auto changed = canonical.document;
	changed[0] = changed[0] == 'q' ? 'Q' : 'q';
	Require(!duckdb_api::IsCanonicalGraphqlDocumentProfile(canonical.document_identity, changed,
	                                                       canonical.digest_algorithm,
	                                                       duckdb_api::ComputeSha256Hex(changed)),
	        "canonical profile admitted changed bytes with their recomputed digest");
	RequireInvalid("GraphQL accepted a changed endpoint", [canonical]() mutable {
		canonical.endpoint_path = "/graphql/v2";
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted credential-bearing fixed metadata", [canonical]() mutable {
		canonical.headers.push_back({"Authorization", "Bearer fixture"});
		BuildGraphqlRelation(std::move(canonical));
	});
}

void TestVariableResponseCursorAndResourceCounterexamples() {
	auto canonical = NativeGraphqlRelation(duckdb_api::BuildNativeGithubConnector()).Operation().Graphql();
	RequireInvalid("GraphQL accepted a missing variable", [canonical]() mutable {
		canonical.variables.pop_back();
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted an extra variable", [canonical]() mutable {
		canonical.variables.push_back({"secret", duckdb_api::CompiledGraphqlVariableType::STRING_NULLABLE,
		                               duckdb_api::CompiledGraphqlVariableSource::LOGICAL_SECRET, 0});
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted a caller cursor", [canonical]() mutable {
		canonical.variables[1].source = duckdb_api::CompiledGraphqlVariableSource::CALLER_INPUT;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted a changed typed result scalar", [canonical]() mutable {
		canonical.result_columns[3].scalar_kind = duckdb_api::CompiledGraphqlScalarKind::STRING;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted a changed typed result path", [canonical]() mutable {
		canonical.result_columns[2].response_path.segments = {"login"};
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted changed typed result nullability", [canonical]() mutable {
		canonical.result_columns[4].nullable = false;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted overlapping row/error paths", [canonical]() mutable {
		canonical.response.errors = canonical.response.nodes;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted changed pageInfo response authority", [canonical]() mutable {
		canonical.response.page_info.segments.pop_back();
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted partial data", [canonical]() mutable {
		canonical.response.partial_data = static_cast<duckdb_api::CompiledGraphqlPartialDataPolicy>(255);
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted independent cursor pagination", [canonical]() mutable {
		canonical.cursor.dependency = duckdb_api::CompiledGraphqlCursorDependency::INDEPENDENT;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted stable cursor ordering", [canonical]() mutable {
		canonical.cursor.consistency = duckdb_api::CompiledGraphqlCursorConsistency::STABLE_ORDERING;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted resumable cursors", [canonical]() mutable {
		canonical.cursor.supports_resume = true;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted concurrent pages", [canonical]() mutable {
		canonical.cursor.max_concurrent_pages = 2;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted changed hasNextPage authority", [canonical]() mutable {
		canonical.cursor.has_next_page.segments.back() = "hasPreviousPage";
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted changed endCursor authority", [canonical]() mutable {
		canonical.cursor.end_cursor.segments.back() = "startCursor";
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted a widened page size", [canonical]() mutable {
		canonical.cursor.page_size = 101;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted an empty request-body ceiling", [canonical]() mutable {
		canonical.max_serialized_request_body_bytes_per_request = 0;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted retry enablement", [canonical]() mutable {
		canonical.retry_enabled = true;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted cache enablement", [canonical]() mutable {
		canonical.cache_enabled = true;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted provider enablement", [canonical]() mutable {
		canonical.providers_enabled = true;
		BuildGraphqlRelation(std::move(canonical));
	});
	RequireInvalid("GraphQL accepted inconsistent row ceilings", [canonical]() mutable {
		BuildGraphqlRelation(std::move(canonical), {}, duckdb_api::CompiledOperationSelector(),
		                     ConnectorCatalogTestAccess::PaginatedResources(8ULL * 1024ULL * 1024ULL,
		                                                                    64ULL * 1024ULL * 1024ULL, 100, 3201, 512));
	});
}

void TestSchemaAndSafeSnapshotCounterexamples() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = NativeGraphqlRelation(connector);
	const auto canonical = relation.Operation().Graphql();
	for (std::size_t index = 0; index < relation.Columns().size(); index++) {
		RequireInvalid("GraphQL accepted a missing schema column", [canonical, &relation, index]() mutable {
			auto columns = relation.Columns();
			columns.erase(columns.begin() + static_cast<std::ptrdiff_t>(index));
			BuildGraphqlRelation(std::move(canonical), std::move(columns));
		});
	}
	RequireInvalid("GraphQL accepted changed primary-language nullability", [canonical, &relation]() mutable {
		auto columns = relation.Columns();
		columns[4].nullable = false;
		BuildGraphqlRelation(std::move(canonical), std::move(columns));
	});
	RequireInvalid("GraphQL accepted a mistyped stars column", [canonical, &relation]() mutable {
		auto columns = relation.Columns();
		columns[3].logical_type = "VARCHAR";
		BuildGraphqlRelation(std::move(canonical), std::move(columns));
	});
	RequireInvalid("GraphQL accepted a changed relation extractor", [canonical, &relation]() mutable {
		auto columns = relation.Columns();
		columns[2].extractor = "$.login";
		BuildGraphqlRelation(std::move(canonical), std::move(columns));
	});
	const auto snapshot = relation.Snapshot();
	Require(snapshot.find("identity:github_viewer_repository_metrics_v1") != std::string::npos &&
	            snapshot.find("sha256:9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85") !=
	                std::string::npos &&
	            snapshot.find("primary_language:VARCHAR?:$.primaryLanguage.name") != std::string::npos &&
	            snapshot.find("serialized_bytes_per_request:8192,serialized_bytes_per_scan:262144") !=
	                std::string::npos,
	        "safe GraphQL snapshot omitted identity, digest, nullability, or bounds");
	for (const auto &prohibited : {"query DuckdbApiViewer", "Bearer fixture", "secret_name=", "credential_value=",
	                               "cursor_value=", "request_body=", "response_row=", "SELECT "}) {
		Require(snapshot.find(prohibited) == std::string::npos,
		        "safe GraphQL snapshot contains prohibited state: " + std::string(prohibited));
	}
}

} // namespace

namespace duckdb_api_test {

void RunConnectorGraphqlContractTests() {
	TestDigestServiceKnownVectors();
	TestClosedProtocolAlternative();
	TestCanonicalGraphqlOperationIsExclusive();
	TestCanonicalGraphqlFactsAndCopy();
	TestDocumentAndRequestCounterexamples();
	TestVariableResponseCursorAndResourceCounterexamples();
	TestSchemaAndSafeSnapshotCounterexamples();
}

} // namespace duckdb_api_test
