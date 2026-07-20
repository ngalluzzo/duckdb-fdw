#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"

#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

#include <cstddef>
#include <new>

namespace duckdb_api {
namespace internal {

static const char CANONICAL_DOCUMENT[] = "query DuckdbApiViewerRepositoryMetrics($pageSize: Int!, $cursor: String) {\n"
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

const char *CanonicalGraphqlDocumentBytes() noexcept {
	return CANONICAL_DOCUMENT;
}

namespace {

bool PathEquals(const PlannedGraphqlResponsePath &path, std::initializer_list<const char *> expected) {
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

bool HasCanonicalHeaders(const std::vector<PlannedHttpHeader> &headers) {
	return headers.size() == 4 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "Content-Type" && headers[1].value == "application/json" &&
	       headers[2].name == "User-Agent" && headers[2].value == "duckdb-api/0.7.0" &&
	       headers[3].name == "X-GitHub-Api-Version" && headers[3].value == "2022-11-28";
}

bool ColumnEquals(const PlannedGraphqlResultColumn &column, const char *name, PlannedGraphqlScalarKind kind,
                  bool nullable, std::initializer_list<const char *> path) {
	return column.name == name && column.scalar_kind == kind && column.nullable == nullable &&
	       PathEquals(column.response_path, path);
}

bool HasCanonicalOperation(const PlannedGraphqlOperation &operation, const HttpExecutionProfile &profile) {
	// Digesting is deliberately after the narrow document bound and before any
	// authority is admitted. The neutral digest service therefore never sees an
	// endpoint-sized response or supplies canonical membership by itself.
	if (operation.max_document_bytes != 4096 || operation.document.empty() ||
	    static_cast<uint64_t>(operation.document.size()) > operation.max_document_bytes ||
	    operation.digest_algorithm != PlannedGraphqlDigestAlgorithm::SHA256 ||
	    ComputeSha256Hex(operation.document) != operation.document_digest) {
		return false;
	}
	if (operation.operation_name != "github_viewer_repository_metrics" ||
	    operation.cardinality != PlannedCardinality::ZERO_TO_MANY ||
	    operation.replay_safety != PlannedReplaySafety::SAFE || operation.kind != PlannedGraphqlOperationKind::QUERY ||
	    operation.document_identity != PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 ||
	    operation.document != CanonicalGraphqlDocumentBytes() || operation.origin.scheme != PlannedUrlScheme::HTTPS ||
	    operation.origin.scheme != profile.scheme || operation.origin.host != "api.github.com" ||
	    operation.origin.host != profile.host || operation.origin.port != 443 ||
	    operation.origin.port != profile.port || operation.path != "/graphql" ||
	    !HasCanonicalHeaders(operation.headers) ||
	    operation.max_serialized_request_body_bytes_per_request != 8ULL * 1024ULL ||
	    operation.max_serialized_request_body_bytes_per_scan != 256ULL * 1024ULL) {
		return false;
	}
	if (operation.variables.size() != 2 || operation.variables[0].name != "pageSize" ||
	    operation.variables[0].type != PlannedGraphqlVariableType::INT_NON_NULL ||
	    operation.variables[0].source != PlannedGraphqlVariableSource::FIXED_PAGE_SIZE ||
	    operation.variables[0].integer_value != 100 || operation.variables[1].name != "cursor" ||
	    operation.variables[1].type != PlannedGraphqlVariableType::STRING_NULLABLE ||
	    operation.variables[1].source != PlannedGraphqlVariableSource::RUNTIME_CURSOR ||
	    operation.variables[1].integer_value != 0) {
		return false;
	}
	const auto &columns = operation.result_columns;
	if (columns.size() != 8 || !ColumnEquals(columns[0], "id", PlannedGraphqlScalarKind::STRING, false, {"id"}) ||
	    !ColumnEquals(columns[1], "full_name", PlannedGraphqlScalarKind::STRING, false, {"nameWithOwner"}) ||
	    !ColumnEquals(columns[2], "owner_login", PlannedGraphqlScalarKind::STRING, false, {"owner", "login"}) ||
	    !ColumnEquals(columns[3], "stars", PlannedGraphqlScalarKind::INT64, false, {"stargazerCount"}) ||
	    !ColumnEquals(columns[4], "primary_language", PlannedGraphqlScalarKind::STRING, true,
	                  {"primaryLanguage", "name"}) ||
	    !ColumnEquals(columns[5], "private", PlannedGraphqlScalarKind::BOOLEAN, false, {"isPrivate"}) ||
	    !ColumnEquals(columns[6], "archived", PlannedGraphqlScalarKind::BOOLEAN, false, {"isArchived"}) ||
	    !ColumnEquals(columns[7], "updated_at", PlannedGraphqlScalarKind::STRING, false, {"updatedAt"})) {
		return false;
	}
	if (!PathEquals(operation.response.nodes, {"data", "viewer", "repositories", "nodes"}) ||
	    !PathEquals(operation.response.errors, {"errors"}) ||
	    !PathEquals(operation.response.page_info, {"data", "viewer", "repositories", "pageInfo"}) ||
	    operation.response.partial_data != PlannedGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR) {
		return false;
	}
	const auto &cursor = operation.cursor;
	return cursor.direction == PlannedGraphqlCursorDirection::FORWARD &&
	       cursor.dependency == PlannedGraphqlCursorDependency::SEQUENTIAL &&
	       cursor.consistency == PlannedGraphqlCursorConsistency::MUTABLE && !cursor.supports_total &&
	       !cursor.supports_resume && cursor.max_concurrent_pages == 1 && cursor.page_size_variable == "pageSize" &&
	       cursor.page_size == 100 && cursor.cursor_variable == "cursor" &&
	       PathEquals(cursor.has_next_page, {"data", "viewer", "repositories", "pageInfo", "hasNextPage"}) &&
	       PathEquals(cursor.end_cursor, {"data", "viewer", "repositories", "pageInfo", "endCursor"}) &&
	       cursor.max_pages_per_scan == 32;
}

bool OutputColumnEquals(const PlannedColumn &column, const char *name, const char *logical_type, bool nullable,
                        const char *extractor) {
	return column.name == name && column.logical_type == logical_type && column.nullable == nullable &&
	       column.extractor == extractor;
}

bool HasCanonicalOutput(const std::vector<PlannedColumn> &columns) {
	return columns.size() == 8 && OutputColumnEquals(columns[0], "id", "VARCHAR", false, "$.id") &&
	       OutputColumnEquals(columns[1], "full_name", "VARCHAR", false, "$.nameWithOwner") &&
	       OutputColumnEquals(columns[2], "owner_login", "VARCHAR", false, "$.owner.login") &&
	       OutputColumnEquals(columns[3], "stars", "BIGINT", false, "$.stargazerCount") &&
	       OutputColumnEquals(columns[4], "primary_language", "VARCHAR", true, "$.primaryLanguage.name") &&
	       OutputColumnEquals(columns[5], "private", "BOOLEAN", false, "$.isPrivate") &&
	       OutputColumnEquals(columns[6], "archived", "BOOLEAN", false, "$.isArchived") &&
	       OutputColumnEquals(columns[7], "updated_at", "VARCHAR", false, "$.updatedAt");
}

bool SamePageBudgets(const ResourceBudgets &left, const ResourceBudgets &right) {
	return left.request_attempts == right.request_attempts && left.response_bytes == right.response_bytes &&
	       left.header_bytes == right.header_bytes && left.decompressed_bytes == right.decompressed_bytes &&
	       left.decoded_records == right.decoded_records &&
	       left.extracted_string_bytes == right.extracted_string_bytes && left.json_nesting == right.json_nesting &&
	       left.decoded_memory_bytes == right.decoded_memory_bytes && left.batch_rows == right.batch_rows &&
	       left.wall_milliseconds == right.wall_milliseconds && left.concurrency == right.concurrency &&
	       left.serialized_request_body_bytes == right.serialized_request_body_bytes;
}

bool HasCanonicalBudgets(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	const auto &page = plan.Pagination().PageBudgets();
	const auto &scan = plan.Pagination().ScanBudgets();
	return page.request_attempts == 1 && page.response_bytes == 8ULL * 1024ULL * 1024ULL &&
	       page.header_bytes == 16ULL * 1024ULL && page.decompressed_bytes == 8ULL * 1024ULL * 1024ULL &&
	       page.decoded_records == 100 && page.decoded_records <= profile.max_decoded_records &&
	       page.extracted_string_bytes == 512 && page.json_nesting == 16 &&
	       page.decoded_memory_bytes == 2ULL * 1024ULL * 1024ULL && page.batch_rows == 64 &&
	       page.wall_milliseconds == 30000 && page.concurrency == 1 &&
	       page.serialized_request_body_bytes == 8ULL * 1024ULL && scan.request_attempts == 32 && scan.pages == 32 &&
	       scan.response_bytes == 64ULL * 1024ULL * 1024ULL && scan.header_bytes == 512ULL * 1024ULL &&
	       scan.decompressed_bytes == 64ULL * 1024ULL * 1024ULL && scan.decoded_records == 3200 &&
	       scan.extracted_string_bytes == 512 && scan.json_nesting == 16 &&
	       scan.decoded_memory_bytes == 2ULL * 1024ULL * 1024ULL && scan.batch_rows == 64 &&
	       scan.wall_milliseconds == 30000 && scan.concurrency == 1 &&
	       scan.serialized_request_body_bytes == 256ULL * 1024ULL && SamePageBudgets(plan.Budgets(), page);
}

bool HasCanonicalPagination(const ScanPlan &plan) {
	const auto &pagination = plan.Pagination();
	if (pagination.Strategy() != PlannedPaginationStrategy::GRAPHQL_CURSOR) {
		return false;
	}
	const auto &cursor = pagination.GraphqlCursor();
	return cursor.direction == PlannedGraphqlCursorDirection::FORWARD &&
	       cursor.dependency == PlannedGraphqlCursorDependency::SEQUENTIAL &&
	       cursor.consistency == PlannedGraphqlCursorConsistency::MUTABLE && !cursor.supports_total &&
	       !cursor.supports_resume && cursor.max_concurrent_pages == 1 && cursor.page_size_variable == "pageSize" &&
	       cursor.page_size == 100 && cursor.cursor_variable == "cursor" &&
	       PathEquals(cursor.has_next_page, {"data", "viewer", "repositories", "pageInfo", "hasNextPage"}) &&
	       PathEquals(cursor.end_cursor, {"data", "viewer", "repositories", "pageInfo", "endCursor"}) &&
	       cursor.max_pages_per_scan == 32;
}

bool HasCanonicalRelationalEnvelope(const ScanPlan &plan) {
	const auto &ownership = plan.Ownership();
	return plan.RemotePredicate() == PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	       plan.RemoteAccuracy() == RemotePredicateAccuracy::UNSUPPORTED &&
	       plan.ResidualPredicate() == PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	       plan.ResidualOwner() == RelationalOwner::DUCKDB &&
	       plan.ConditionalInput() == PlannedConditionalInput::NONE &&
	       plan.PredicateCategory() == PredicateDecisionCategory::UNSUPPORTED &&
	       plan.PredicateReason() == PredicateDecisionReason::NO_REMOTE_CANDIDATE &&
	       ownership.filter == RelationalOwner::DUCKDB && ownership.projection == RelationalOwner::DUCKDB &&
	       ownership.ordering == RelationalOwner::DUCKDB && ownership.limit == RelationalOwner::DUCKDB &&
	       ownership.offset == RelationalOwner::DUCKDB && plan.RemoteOrdering() == RelationalDelegation::NONE &&
	       plan.RuntimeOrdering() == RelationalDelegation::NONE && plan.RemoteLimit() == RelationalDelegation::NONE &&
	       plan.RemoteOffset() == RelationalDelegation::NONE && plan.RuntimeLimit() == RelationalDelegation::NONE &&
	       plan.RuntimeOffset() == RelationalDelegation::NONE;
}

bool HasCanonicalAuthority(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	const auto &obligation = plan.AuthenticationObligation();
	const auto *destination = obligation.Destination();
	const auto &network = plan.Network();
	return plan.Providers() == FeatureState::DISABLED && plan.Retry() == FeatureState::DISABLED &&
	       plan.Cache() == FeatureState::DISABLED && plan.Authentication() == FeatureState::ENABLED &&
	       plan.SecretReference().IsPresent() && obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination &&
	       destination->scheme == PlannedUrlScheme::HTTPS && destination->scheme == profile.scheme &&
	       destination->host == "api.github.com" && destination->host == profile.host && destination->port == 443 &&
	       destination->port == profile.port && network.allowed_schemes.size() == 1 &&
	       network.allowed_schemes[0] == "https" && network.allowed_hosts.size() == 1 &&
	       network.allowed_hosts[0] == "api.github.com" && !network.redirects_enabled &&
	       network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool IsCanonicalGraphqlPlan(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	if (plan.Operation().Protocol() != PlannedProtocol::GRAPHQL) {
		return false;
	}
	const auto &operation = plan.Operation().Graphql();
	// Connector/version/relation labels and explanation prose are provenance,
	// not execution authority. The typed domain and complete executable handoff
	// below are the closed admission surface.
	return plan.Domain() == BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
	       HasCanonicalOperation(operation, profile) && HasCanonicalOutput(plan.OutputColumns()) &&
	       HasCanonicalRelationalEnvelope(plan) && HasCanonicalAuthority(plan, profile) &&
	       HasCanonicalPagination(plan) && HasCanonicalBudgets(plan, profile);
}

} // namespace

AdmittedGraphqlRequestProfile::AdmittedGraphqlRequestProfile()
    : method("POST"), scheme("https"), host("api.github.com"), port(443), path("/graphql"),
      headers({{"Accept", "application/vnd.github+json"},
               {"User-Agent", "duckdb-api/0.7.0"},
               {"X-GitHub-Api-Version", "2022-11-28"}}),
      document(CanonicalGraphqlDocumentBytes()),
      columns({{"id", ValueKind::VARCHAR, false, {"id"}},
               {"full_name", ValueKind::VARCHAR, false, {"nameWithOwner"}},
               {"owner_login", ValueKind::VARCHAR, false, {"owner", "login"}},
               {"stars", ValueKind::BIGINT, false, {"stargazerCount"}},
               {"primary_language", ValueKind::VARCHAR, true, {"primaryLanguage", "name"}},
               {"private", ValueKind::BOOLEAN, false, {"isPrivate"}},
               {"archived", ValueKind::BOOLEAN, false, {"isArchived"}},
               {"updated_at", ValueKind::VARCHAR, false, {"updatedAt"}}}),
      page_size(100), max_pages(32), max_request_body_bytes(8ULL * 1024ULL), max_scan_body_bytes(256ULL * 1024ULL) {
}

const std::string &AdmittedGraphqlRequestProfile::Method() const {
	return method;
}
const std::string &AdmittedGraphqlRequestProfile::Scheme() const {
	return scheme;
}
const std::string &AdmittedGraphqlRequestProfile::Host() const {
	return host;
}
uint16_t AdmittedGraphqlRequestProfile::Port() const {
	return port;
}
const std::string &AdmittedGraphqlRequestProfile::Path() const {
	return path;
}
const std::vector<HttpHeader> &AdmittedGraphqlRequestProfile::Headers() const {
	return headers;
}
const std::string &AdmittedGraphqlRequestProfile::Document() const {
	return document;
}
const std::vector<AdmittedGraphqlColumn> &AdmittedGraphqlRequestProfile::Columns() const {
	return columns;
}
uint64_t AdmittedGraphqlRequestProfile::PageSize() const {
	return page_size;
}
uint64_t AdmittedGraphqlRequestProfile::MaxPages() const {
	return max_pages;
}
uint64_t AdmittedGraphqlRequestProfile::MaxRequestBodyBytes() const {
	return max_request_body_bytes;
}
uint64_t AdmittedGraphqlRequestProfile::MaxScanBodyBytes() const {
	return max_scan_body_bytes;
}

std::unique_ptr<const AdmittedGraphqlRequestProfile> TryAdmitGraphqlPlan(const ScanPlan &plan,
                                                                         const HttpExecutionProfile &profile) {
	try {
		if (!IsCanonicalGraphqlPlan(plan, profile)) {
			return std::unique_ptr<const AdmittedGraphqlRequestProfile>();
		}
		return std::unique_ptr<const AdmittedGraphqlRequestProfile>(new AdmittedGraphqlRequestProfile());
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
		                     "GraphQL request profile could not be allocated within its memory budget");
	} catch (const ExecutionError &) {
		return std::unique_ptr<const AdmittedGraphqlRequestProfile>();
	} catch (...) {
		return std::unique_ptr<const AdmittedGraphqlRequestProfile>();
	}
}

} // namespace internal
} // namespace duckdb_api
