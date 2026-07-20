#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"
#include "duckdb_api/content_digest.hpp"

#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {
namespace internal {

namespace {

constexpr std::uint64_t MAX_DOCUMENT_BYTES = 4096;
constexpr std::uint64_t MAX_BODY_BYTES_PER_REQUEST = 8ULL * 1024ULL;
constexpr std::uint64_t MAX_BODY_BYTES_PER_SCAN = 256ULL * 1024ULL;
constexpr std::uint64_t MAX_PAGES_PER_SCAN = 32;

const char CANONICAL_DOCUMENT[] = "query DuckdbApiViewerRepositoryMetrics($pageSize: Int!, $cursor: String) {\n"
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

// SHA-256 of CANONICAL_DOCUMENT's exact bytes (without a trailing newline).
const char CANONICAL_DIGEST[] = "9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85";

bool PathEquals(const CompiledGraphqlResponsePath &path, std::initializer_list<const char *> expected) {
	if (path.segments.size() != expected.size()) {
		return false;
	}
	std::size_t index = 0;
	for (const auto *segment : expected) {
		if (path.segments[index++] != segment) {
			return false;
		}
	}
	return true;
}

bool HasCanonicalOrigin(const CompiledHttpOrigin &origin) {
	return origin.scheme == CompiledUrlScheme::HTTPS && origin.host.Value() == "api.github.com" && origin.port == 443;
}

bool HasCanonicalHeaders(const std::vector<CompiledHttpHeader> &headers) {
	return headers.size() == 4 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "Content-Type" && headers[1].value == "application/json" &&
	       headers[2].name == "User-Agent" && headers[2].value == "duckdb-api/0.7.0" &&
	       headers[3].name == "X-GitHub-Api-Version" && headers[3].value == "2022-11-28";
}

bool HasCanonicalVariables(const std::vector<CompiledGraphqlVariable> &variables) {
	return variables.size() == 2 && variables[0].name == "pageSize" &&
	       variables[0].type == CompiledGraphqlVariableType::INT_NON_NULL &&
	       variables[0].source == CompiledGraphqlVariableSource::FIXED_PAGE_SIZE && variables[0].integer_value == 100 &&
	       variables[1].name == "cursor" && variables[1].type == CompiledGraphqlVariableType::STRING_NULLABLE &&
	       variables[1].source == CompiledGraphqlVariableSource::RUNTIME_CURSOR && variables[1].integer_value == 0;
}

bool HasResultColumn(const CompiledGraphqlResultColumn &column, const char *name, CompiledGraphqlScalarKind kind,
                     bool nullable, std::initializer_list<const char *> path) {
	return column.name == name && column.scalar_kind == kind && column.nullable == nullable &&
	       PathEquals(column.response_path, path);
}

bool HasCanonicalResultColumns(const std::vector<CompiledGraphqlResultColumn> &columns) {
	return columns.size() == 8 && HasResultColumn(columns[0], "id", CompiledGraphqlScalarKind::STRING, false, {"id"}) &&
	       HasResultColumn(columns[1], "full_name", CompiledGraphqlScalarKind::STRING, false, {"nameWithOwner"}) &&
	       HasResultColumn(columns[2], "owner_login", CompiledGraphqlScalarKind::STRING, false, {"owner", "login"}) &&
	       HasResultColumn(columns[3], "stars", CompiledGraphqlScalarKind::INT64, false, {"stargazerCount"}) &&
	       HasResultColumn(columns[4], "primary_language", CompiledGraphqlScalarKind::STRING, true,
	                       {"primaryLanguage", "name"}) &&
	       HasResultColumn(columns[5], "private", CompiledGraphqlScalarKind::BOOLEAN, false, {"isPrivate"}) &&
	       HasResultColumn(columns[6], "archived", CompiledGraphqlScalarKind::BOOLEAN, false, {"isArchived"}) &&
	       HasResultColumn(columns[7], "updated_at", CompiledGraphqlScalarKind::STRING, false, {"updatedAt"});
}

void AppendPath(std::ostream &result, const CompiledGraphqlResponsePath &path) {
	for (std::size_t index = 0; index < path.segments.size(); index++) {
		result << (index == 0 ? "" : ".") << path.segments[index];
	}
}

const char *SchemeName(CompiledUrlScheme scheme) {
	return scheme == CompiledUrlScheme::HTTPS ? "https" : "http";
}

bool HasColumn(const CompiledColumn &column, const char *name, const char *type, bool nullable, const char *extractor) {
	return column.name == name && column.logical_type == type && column.nullable == nullable &&
	       column.extractor == extractor;
}

const char *LogicalTypeName(CompiledGraphqlScalarKind kind) {
	switch (kind) {
	case CompiledGraphqlScalarKind::STRING:
		return "VARCHAR";
	case CompiledGraphqlScalarKind::INT64:
		return "BIGINT";
	case CompiledGraphqlScalarKind::BOOLEAN:
		return "BOOLEAN";
	}
	throw std::invalid_argument("compiled GraphQL result mapping contains an unknown scalar kind");
}

std::string RelationExtractor(const CompiledGraphqlResponsePath &path) {
	if (path.segments.empty()) {
		throw std::invalid_argument("compiled GraphQL result mapping contains an empty response path");
	}
	std::string result;
	for (const auto &segment : path.segments) {
		if (segment.empty() || segment.find_first_of(".$[]") != std::string::npos) {
			throw std::invalid_argument("compiled GraphQL result mapping contains an invalid response path segment");
		}
		result += "." + segment;
	}
	return "$" + result;
}

bool ResultColumnsAlign(const std::vector<CompiledColumn> &columns,
                        const std::vector<CompiledGraphqlResultColumn> &result_columns) {
	if (columns.size() != result_columns.size()) {
		return false;
	}
	for (std::size_t index = 0; index < columns.size(); index++) {
		const auto &column = columns[index];
		const auto &result_column = result_columns[index];
		if (column.name != result_column.name || column.nullable != result_column.nullable ||
		    column.logical_type != LogicalTypeName(result_column.scalar_kind) ||
		    column.extractor != RelationExtractor(result_column.response_path)) {
			return false;
		}
	}
	return true;
}

} // namespace

const std::string &CanonicalGithubViewerRepositoryMetricsDocument() {
	static const std::string document(CANONICAL_DOCUMENT);
	return document;
}

const std::string &CanonicalGithubViewerRepositoryMetricsDigest() {
	static const std::string digest(CANONICAL_DIGEST);
	return digest;
}

CompiledGraphqlOperation BuildCanonicalGithubViewerRepositoryMetricsGraphqlOperation() {
	const CompiledHttpOrigin origin = {CompiledUrlScheme::HTTPS, CompiledHttpHost("api.github.com"), 443};
	return CompiledGraphqlOperation {
	    CompiledGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1,
	    CanonicalGithubViewerRepositoryMetricsDocument(),
	    CompiledGraphqlDigestAlgorithm::SHA256,
	    CanonicalGithubViewerRepositoryMetricsDigest(),
	    origin,
	    "/graphql",
	    {{"Accept", "application/vnd.github+json"},
	     {"Content-Type", "application/json"},
	     {"User-Agent", "duckdb-api/0.7.0"},
	     {"X-GitHub-Api-Version", "2022-11-28"}},
	    {{"pageSize", CompiledGraphqlVariableType::INT_NON_NULL, CompiledGraphqlVariableSource::FIXED_PAGE_SIZE, 100},
	     {"cursor", CompiledGraphqlVariableType::STRING_NULLABLE, CompiledGraphqlVariableSource::RUNTIME_CURSOR, 0}},
	    {{"id", CompiledGraphqlScalarKind::STRING, false, {{"id"}}},
	     {"full_name", CompiledGraphqlScalarKind::STRING, false, {{"nameWithOwner"}}},
	     {"owner_login", CompiledGraphqlScalarKind::STRING, false, {{"owner", "login"}}},
	     {"stars", CompiledGraphqlScalarKind::INT64, false, {{"stargazerCount"}}},
	     {"primary_language", CompiledGraphqlScalarKind::STRING, true, {{"primaryLanguage", "name"}}},
	     {"private", CompiledGraphqlScalarKind::BOOLEAN, false, {{"isPrivate"}}},
	     {"archived", CompiledGraphqlScalarKind::BOOLEAN, false, {{"isArchived"}}},
	     {"updated_at", CompiledGraphqlScalarKind::STRING, false, {{"updatedAt"}}}},
	    {{{"data", "viewer", "repositories", "nodes"}},
	     {{"errors"}},
	     {{"data", "viewer", "repositories", "pageInfo"}},
	     CompiledGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR},
	    {CompiledGraphqlCursorDirection::FORWARD,
	     CompiledGraphqlCursorDependency::SEQUENTIAL,
	     CompiledGraphqlCursorConsistency::MUTABLE,
	     false,
	     false,
	     1,
	     "pageSize",
	     100,
	     "cursor",
	     {{"data", "viewer", "repositories", "pageInfo", "hasNextPage"}},
	     {{"data", "viewer", "repositories", "pageInfo", "endCursor"}},
	     MAX_PAGES_PER_SCAN},
	    MAX_DOCUMENT_BYTES,
	    MAX_BODY_BYTES_PER_REQUEST,
	    MAX_BODY_BYTES_PER_SCAN,
	    false,
	    false,
	    false};
}

void ValidateGraphqlOperationValue(const CompiledGraphqlOperation &operation) {
	if (operation.document.empty() || operation.max_document_bytes != MAX_DOCUMENT_BYTES ||
	    operation.document.size() > operation.max_document_bytes ||
	    !IsCanonicalGraphqlDocumentProfile(operation.document_identity, operation.document, operation.digest_algorithm,
	                                       operation.document_digest)) {
		throw std::invalid_argument("compiled GraphQL document identity, bytes, digest, or size disagree");
	}
	if (!HasCanonicalOrigin(operation.endpoint_origin) || operation.endpoint_path != "/graphql" ||
	    !HasCanonicalHeaders(operation.headers)) {
		throw std::invalid_argument("compiled GraphQL endpoint or fixed request metadata drifted");
	}
	if (!HasCanonicalVariables(operation.variables)) {
		throw std::invalid_argument("compiled GraphQL variable declarations drifted");
	}
	if (!HasCanonicalResultColumns(operation.result_columns)) {
		throw std::invalid_argument("compiled GraphQL typed result mapping drifted");
	}
	if (!PathEquals(operation.response.nodes, {"data", "viewer", "repositories", "nodes"}) ||
	    !PathEquals(operation.response.errors, {"errors"}) ||
	    !PathEquals(operation.response.page_info, {"data", "viewer", "repositories", "pageInfo"}) ||
	    operation.response.partial_data != CompiledGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR) {
		throw std::invalid_argument("compiled GraphQL response paths or error disposition drifted");
	}
	const auto &cursor = operation.cursor;
	if (cursor.direction != CompiledGraphqlCursorDirection::FORWARD ||
	    cursor.dependency != CompiledGraphqlCursorDependency::SEQUENTIAL ||
	    cursor.consistency != CompiledGraphqlCursorConsistency::MUTABLE || cursor.supports_total ||
	    cursor.supports_resume || cursor.max_concurrent_pages != 1 || cursor.page_size_variable != "pageSize" ||
	    cursor.page_size != 100 || cursor.cursor_variable != "cursor" ||
	    !PathEquals(cursor.has_next_page, {"data", "viewer", "repositories", "pageInfo", "hasNextPage"}) ||
	    !PathEquals(cursor.end_cursor, {"data", "viewer", "repositories", "pageInfo", "endCursor"}) ||
	    cursor.max_pages_per_scan != MAX_PAGES_PER_SCAN) {
		throw std::invalid_argument("compiled GraphQL cursor declaration widened or drifted");
	}
	if (operation.max_serialized_request_body_bytes_per_request != MAX_BODY_BYTES_PER_REQUEST ||
	    operation.max_serialized_request_body_bytes_per_scan != MAX_BODY_BYTES_PER_SCAN ||
	    operation.max_serialized_request_body_bytes_per_request >
	        std::numeric_limits<std::uint64_t>::max() / cursor.max_pages_per_scan ||
	    operation.max_serialized_request_body_bytes_per_scan !=
	        operation.max_serialized_request_body_bytes_per_request * cursor.max_pages_per_scan ||
	    operation.retry_enabled || operation.cache_enabled || operation.providers_enabled) {
		throw std::invalid_argument("compiled GraphQL body envelope or disabled feature profile drifted");
	}
}

void ValidateCanonicalGraphqlRelation(const std::string &relation_name, const std::vector<CompiledColumn> &columns,
                                      const CompiledOperation &operation,
                                      const CompiledAuthenticationPolicy &authentication,
                                      const CompiledResourceCeilings &ceilings,
                                      const std::vector<CompiledPredicateMapping> &predicate_mappings) {
	if (relation_name != "viewer_repository_metrics" || operation.name != "github_viewer_repository_metrics" ||
	    !operation.fallback || operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY ||
	    operation.Protocol() != CompiledProtocol::GRAPHQL || !predicate_mappings.empty() ||
	    !operation.selector.RequiredInputReferences().empty() || !operation.selector.RequiredInputs().empty() ||
	    !operation.selector.AnyInputSets().empty() || !operation.selector.ForbiddenInputs().empty() ||
	    operation.selector.Priority() != 0) {
		throw std::invalid_argument("compiled GraphQL relation identity or relational authority drifted");
	}
	if (columns.size() != 8 || !HasColumn(columns[0], "id", "VARCHAR", false, "$.id") ||
	    !HasColumn(columns[1], "full_name", "VARCHAR", false, "$.nameWithOwner") ||
	    !HasColumn(columns[2], "owner_login", "VARCHAR", false, "$.owner.login") ||
	    !HasColumn(columns[3], "stars", "BIGINT", false, "$.stargazerCount") ||
	    !HasColumn(columns[4], "primary_language", "VARCHAR", true, "$.primaryLanguage.name") ||
	    !HasColumn(columns[5], "private", "BOOLEAN", false, "$.isPrivate") ||
	    !HasColumn(columns[6], "archived", "BOOLEAN", false, "$.isArchived") ||
	    !HasColumn(columns[7], "updated_at", "VARCHAR", false, "$.updatedAt")) {
		throw std::invalid_argument("compiled GraphQL relation schema or nullability drifted");
	}
	if (!ResultColumnsAlign(columns, operation.Graphql().result_columns)) {
		throw std::invalid_argument("compiled GraphQL relation and typed result mapping disagree");
	}
	const auto *destination = authentication.Destination();
	if (authentication.Requirement() != CompiledCredentialRequirement::REQUIRED ||
	    authentication.LogicalCredential() != "token" ||
	    authentication.Authenticator() != CompiledAuthenticator::BEARER ||
	    authentication.Placement() != CompiledCredentialPlacement::AUTHORIZATION_HEADER || destination == nullptr ||
	    !HasCanonicalOrigin(*destination)) {
		throw std::invalid_argument("compiled GraphQL relation authentication policy drifted");
	}
	if (!ceilings.HasResponseByteNarrowing() || ceilings.MaxResponseBytesPerPage() != 8ULL * 1024ULL * 1024ULL ||
	    ceilings.MaxResponseBytesPerScan() != 64ULL * 1024ULL * 1024ULL || ceilings.MaxRecordsPerPage() != 100 ||
	    ceilings.MaxRecordsPerScan() != 3200 || ceilings.MaxExtractedStringBytes() != 512) {
		throw std::invalid_argument("compiled GraphQL relation resource envelope drifted");
	}
	ValidateGraphqlOperationValue(operation.Graphql());
}

void AppendGraphqlOperation(std::ostream &result, const CompiledGraphqlOperation &operation) {
	result << "GRAPHQL:identity:github_viewer_repository_metrics_v1:sha256:" << operation.document_digest
	       << ";endpoint=origin:[scheme:" << SchemeName(operation.endpoint_origin.scheme)
	       << ",host:" << operation.endpoint_origin.host.Value() << ",port:" << operation.endpoint_origin.port
	       << "],path:" << operation.endpoint_path << ",headers:[";
	for (std::size_t index = 0; index < operation.headers.size(); index++) {
		result << (index == 0 ? "" : ",") << operation.headers[index].name << '=' << operation.headers[index].value;
	}
	result << "];variables:[pageSize:Int!:fixed_page_size=100,cursor:String:runtime_cursor]"
	          ";result_columns:[id:string!:id,full_name:string!:nameWithOwner,owner_login:string!:owner.login,"
	          "stars:int64!:stargazerCount,primary_language:string?:primaryLanguage.name,private:boolean!:isPrivate,"
	          "archived:boolean!:isArchived,updated_at:string!:updatedAt];response=nodes:";
	AppendPath(result, operation.response.nodes);
	result << ",errors:";
	AppendPath(result, operation.response.errors);
	result << ",page_info:";
	AppendPath(result, operation.response.page_info);
	result << ",partial_data:fail;cursor=forward:sequential:mutable,total:none,resume:none,concurrency:1,page_size:"
	       << operation.cursor.page_size_variable << '=' << operation.cursor.page_size
	       << ",cursor_variable:" << operation.cursor.cursor_variable << ",has_next:";
	AppendPath(result, operation.cursor.has_next_page);
	result << ",end_cursor:";
	AppendPath(result, operation.cursor.end_cursor);
	result << ",max_pages:" << operation.cursor.max_pages_per_scan
	       << ";body=max_document_bytes:" << operation.max_document_bytes
	       << ",serialized_bytes_per_request:" << operation.max_serialized_request_body_bytes_per_request
	       << ",serialized_bytes_per_scan:" << operation.max_serialized_request_body_bytes_per_scan
	       << ";features=retry:disabled,cache:disabled,providers:disabled";
}

} // namespace internal

bool IsCanonicalGraphqlDocumentProfile(CompiledGraphqlDocumentIdentity identity, const std::string &document,
                                       CompiledGraphqlDigestAlgorithm algorithm, const std::string &digest) {
	return identity == CompiledGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 &&
	       algorithm == CompiledGraphqlDigestAlgorithm::SHA256 &&
	       document == internal::CanonicalGithubViewerRepositoryMetricsDocument() &&
	       digest == internal::CanonicalGithubViewerRepositoryMetricsDigest() && ComputeSha256Hex(document) == digest;
}

} // namespace duckdb_api
