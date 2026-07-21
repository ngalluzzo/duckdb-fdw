#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"

#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/policy/request_validation.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <set>

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

bool SamePath(const PlannedGraphqlResponsePath &left, const PlannedGraphqlResponsePath &right) {
	return left.segments == right.segments;
}

bool IsPrefixWithOneLeaf(const PlannedGraphqlResponsePath &prefix, const PlannedGraphqlResponsePath &path) {
	return path.segments.size() == prefix.segments.size() + 1 &&
	       std::equal(prefix.segments.begin(), prefix.segments.end(), path.segments.begin());
}

bool IsDisjoint(const PlannedGraphqlResponsePath &left, const PlannedGraphqlResponsePath &right) {
	const auto common = std::min(left.segments.size(), right.segments.size());
	return !std::equal(left.segments.begin(), left.segments.begin() + static_cast<std::ptrdiff_t>(common),
	                   right.segments.begin());
}

bool TryScalarKind(PlannedGraphqlScalarKind planned, ValueKind &kind, const char *&logical) {
	switch (planned) {
	case PlannedGraphqlScalarKind::STRING:
		kind = ValueKind::VARCHAR;
		logical = "VARCHAR";
		return true;
	case PlannedGraphqlScalarKind::INT64:
		kind = ValueKind::BIGINT;
		logical = "BIGINT";
		return true;
	case PlannedGraphqlScalarKind::BOOLEAN:
		kind = ValueKind::BOOLEAN;
		logical = "BOOLEAN";
		return true;
	}
	return false;
}

std::string Extractor(const std::vector<std::string> &segments) {
	std::string result = "$";
	for (const auto &segment : segments) {
		result += "." + segment;
	}
	return result;
}

bool HasColumns(const PlannedGraphqlOperation &operation, const std::vector<PlannedColumn> &output) {
	if (operation.result_columns.empty() || operation.result_columns.size() > 256 ||
	    output.size() != operation.result_columns.size()) {
		return false;
	}
	std::set<std::string> names;
	std::set<std::string> paths;
	for (std::size_t index = 0; index < output.size(); index++) {
		const auto &result = operation.result_columns[index];
		const auto &column = output[index];
		ValueKind kind = ValueKind::VARCHAR;
		const char *logical = nullptr;
		const auto joined_path = Extractor(result.response_path.segments);
		if (!IsSafeLogicalId(result.name) || !names.insert(result.name).second ||
		    !IsSafeGraphqlPath(result.response_path.segments, 1, 2) || !paths.insert(joined_path).second ||
		    !TryScalarKind(result.scalar_kind, kind, logical) || column.name != result.name ||
		    column.logical_type != logical || column.nullable != result.nullable || column.extractor != joined_path) {
			return false;
		}
	}
	return true;
}

bool HasVariables(const PlannedGraphqlOperation &operation) {
	if (operation.variables.size() != 2 || !IsGraphqlName(operation.cursor.page_size_variable) ||
	    !IsGraphqlName(operation.cursor.cursor_variable) ||
	    operation.cursor.page_size_variable == operation.cursor.cursor_variable || operation.cursor.page_size == 0) {
		return false;
	}
	const auto &page = operation.variables[0];
	const auto &cursor = operation.variables[1];
	return page.name == operation.cursor.page_size_variable && page.type == PlannedGraphqlVariableType::INT_NON_NULL &&
	       page.source == PlannedGraphqlVariableSource::FIXED_PAGE_SIZE &&
	       page.integer_value == operation.cursor.page_size && cursor.name == operation.cursor.cursor_variable &&
	       cursor.type == PlannedGraphqlVariableType::STRING_NULLABLE &&
	       cursor.source == PlannedGraphqlVariableSource::RUNTIME_CURSOR && cursor.integer_value == 0;
}

bool HasResponseShape(const PlannedGraphqlOperation &operation) {
	const auto &nodes = operation.response.nodes.segments;
	const auto &errors = operation.response.errors.segments;
	const auto &page_info = operation.response.page_info.segments;
	if (!IsSafeGraphqlPath(errors, 1, 1) || errors[0] != "errors" || !IsSafeGraphqlPath(nodes, 3, 18) ||
	    !IsSafeGraphqlPath(page_info, 3, 18) || nodes[0] != "data" || nodes.size() != page_info.size() ||
	    nodes.back() != "nodes" || page_info.back() == "nodes" ||
	    !std::equal(nodes.begin(), nodes.end() - 1, page_info.begin())) {
		return false;
	}
	return IsPrefixWithOneLeaf(operation.response.page_info, operation.cursor.has_next_page) &&
	       IsPrefixWithOneLeaf(operation.response.page_info, operation.cursor.end_cursor);
}

bool HasCursor(const PlannedGraphqlCursor &cursor) {
	return cursor.direction == PlannedGraphqlCursorDirection::FORWARD &&
	       cursor.dependency == PlannedGraphqlCursorDependency::SEQUENTIAL &&
	       cursor.consistency == PlannedGraphqlCursorConsistency::MUTABLE && !cursor.supports_total &&
	       !cursor.supports_resume && cursor.max_concurrent_pages == 1 && cursor.max_pages_per_scan > 0 &&
	       cursor.max_pages_per_scan <= PAGINATION_MAX_PAGES_PER_SCAN &&
	       IsSafeGraphqlPath(cursor.has_next_page.segments, 4, 19) &&
	       IsSafeGraphqlPath(cursor.end_cursor.segments, 4, 19) &&
	       cursor.has_next_page.segments != cursor.end_cursor.segments;
}

bool SameCursor(const PlannedGraphqlCursor &left, const PlannedGraphqlCursor &right) {
	return left.direction == right.direction && left.dependency == right.dependency &&
	       left.consistency == right.consistency && left.supports_total == right.supports_total &&
	       left.supports_resume == right.supports_resume && left.max_concurrent_pages == right.max_concurrent_pages &&
	       left.page_size_variable == right.page_size_variable && left.page_size == right.page_size &&
	       left.cursor_variable == right.cursor_variable && SamePath(left.has_next_page, right.has_next_page) &&
	       SamePath(left.end_cursor, right.end_cursor) && left.max_pages_per_scan == right.max_pages_per_scan;
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

bool HasBudgets(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	const auto &operation = plan.Operation().Graphql();
	const auto &page = plan.Pagination().PageBudgets();
	const auto &scan = plan.Pagination().ScanBudgets();
	return page.IsWithinPaginatedPageBounds() && scan.IsWithinPaginatedScanBounds() &&
	       SamePageBudgets(plan.Budgets(), page) && page.decoded_records <= profile.max_decoded_records &&
	       page.serialized_request_body_bytes > 0 &&
	       page.serialized_request_body_bytes == operation.max_serialized_request_body_bytes_per_request &&
	       scan.serialized_request_body_bytes == operation.max_serialized_request_body_bytes_per_scan &&
	       scan.pages == operation.cursor.max_pages_per_scan && scan.request_attempts == scan.pages &&
	       scan.response_bytes >= page.response_bytes && scan.header_bytes >= page.header_bytes &&
	       scan.decompressed_bytes >= page.decompressed_bytes && scan.decoded_records >= page.decoded_records &&
	       scan.decoded_memory_bytes >= page.decoded_memory_bytes;
}

bool HasRelationalEnvelope(const ScanPlan &plan) {
	const auto &ownership = plan.Ownership();
	return plan.RemotePredicate() == PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	       plan.RemoteAccuracy() == RemotePredicateAccuracy::UNSUPPORTED &&
	       plan.ResidualOwner() == RelationalOwner::DUCKDB &&
	       plan.ConditionalInput() == PlannedConditionalInput::NONE && ownership.filter == RelationalOwner::DUCKDB &&
	       ownership.projection == RelationalOwner::DUCKDB && ownership.ordering == RelationalOwner::DUCKDB &&
	       ownership.limit == RelationalOwner::DUCKDB && ownership.offset == RelationalOwner::DUCKDB &&
	       plan.RemoteOrdering() == RelationalDelegation::NONE &&
	       plan.RuntimeOrdering() == RelationalDelegation::NONE && plan.RemoteLimit() == RelationalDelegation::NONE &&
	       plan.RemoteOffset() == RelationalDelegation::NONE && plan.RuntimeLimit() == RelationalDelegation::NONE &&
	       plan.RuntimeOffset() == RelationalDelegation::NONE;
}

// Document identity and base domain are closed Semantics discriminators. They
// are not request or relational authority, but Runtime still rejects unknown
// values and mismatched native/package pairs so a newly introduced planner
// profile cannot execute before its handoff is reviewed here.
bool HasKnownExecutionProfile(const ScanPlan &plan) {
	const auto identity = plan.Operation().Graphql().document_identity;
	switch (identity) {
	case PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1:
		return plan.Domain() == BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES;
	case PlannedGraphqlDocumentIdentity::PACKAGE_GENERATED_V1:
		return false;
	}
	return false;
}

bool HasAuthority(const ScanPlan &plan, const HttpExecutionProfile &profile, bool &requires_bearer) {
	const auto &operation = plan.Operation().Graphql();
	const auto &obligation = plan.AuthenticationObligation();
	const auto *destination = obligation.Destination();
	const auto &network = plan.Network();
	const bool bearer = plan.Authentication() == FeatureState::ENABLED && plan.SecretReference().IsPresent() &&
	                    obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	                    obligation.LogicalCredential() == "token" &&
	                    obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	                    obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination &&
	                    destination->scheme == operation.origin.scheme && destination->host == operation.origin.host &&
	                    destination->port == operation.origin.port;
	const bool anonymous = plan.Authentication() == FeatureState::DISABLED && !plan.SecretReference().IsPresent() &&
	                       obligation.Requirement() == PlannedCredentialRequirement::NONE &&
	                       obligation.LogicalCredential().empty() &&
	                       obligation.Authenticator() == PlannedAuthenticator::NONE &&
	                       obligation.Placement() == PlannedCredentialPlacement::NONE && destination == nullptr;
	requires_bearer = bearer;
	return plan.Providers() == FeatureState::DISABLED && plan.Retry() == FeatureState::DISABLED &&
	       plan.Cache() == FeatureState::DISABLED && (bearer || anonymous) &&
	       operation.origin.scheme == PlannedUrlScheme::HTTPS && operation.origin.scheme == profile.scheme &&
	       operation.origin.host == profile.host && operation.origin.port == profile.port &&
	       network.allowed_schemes.size() == 1 && network.allowed_schemes[0] == "https" &&
	       network.allowed_hosts.size() == 1 && network.allowed_hosts[0] == profile.host &&
	       network.port == operation.origin.port && network.port == profile.port && !network.redirects_enabled &&
	       network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool IsAdmissible(const ScanPlan &plan, const HttpExecutionProfile &profile, bool &requires_bearer) {
	if (plan.Operation().Protocol() != PlannedProtocol::GRAPHQL ||
	    plan.Pagination().Strategy() != PlannedPaginationStrategy::GRAPHQL_CURSOR) {
		return false;
	}
	const auto &operation = plan.Operation().Graphql();
	std::vector<HttpHeader> headers;
	if (operation.cardinality != PlannedCardinality::ZERO_TO_MANY ||
	    operation.replay_safety != PlannedReplaySafety::SAFE || operation.kind != PlannedGraphqlOperationKind::QUERY ||
	    operation.digest_algorithm != PlannedGraphqlDigestAlgorithm::SHA256 || operation.document.empty() ||
	    operation.max_document_bytes == 0 || operation.max_document_bytes > 64ULL * 1024ULL ||
	    static_cast<uint64_t>(operation.document.size()) > operation.max_document_bytes ||
	    !IsValidUtf8(operation.document) || ComputeSha256Hex(operation.document) != operation.document_digest ||
	    operation.document != CANONICAL_DOCUMENT || !IsSafeDnsHost(operation.origin.host) ||
	    operation.origin.port == 0 || !IsSafeRequestPath(operation.path) || !HasKnownExecutionProfile(plan) ||
	    !TryCopyFixedHeaders(operation.headers, true, plan.Pagination().PageBudgets().header_bytes, headers) ||
	    !HasVariables(operation) || !HasColumns(operation, plan.OutputColumns()) || !HasCursor(operation.cursor) ||
	    !SameCursor(operation.cursor, plan.Pagination().GraphqlCursor()) ||
	    operation.response.partial_data != PlannedGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR ||
	    !HasResponseShape(operation) || !IsDisjoint(operation.response.nodes, operation.response.errors) ||
	    !IsDisjoint(operation.response.page_info, operation.response.errors) ||
	    operation.response.nodes.segments == operation.response.page_info.segments) {
		return false;
	}
	return HasBudgets(plan, profile) && HasRelationalEnvelope(plan) && HasAuthority(plan, profile, requires_bearer);
}

} // namespace

AdmittedGraphqlRequestProfile::AdmittedGraphqlRequestProfile(const ScanPlan &plan, bool requires_bearer_p)
    : method("POST"), scheme("https"), port(plan.Operation().Graphql().origin.port),
      page_size(plan.Operation().Graphql().cursor.page_size),
      max_pages(plan.Operation().Graphql().cursor.max_pages_per_scan),
      max_request_body_bytes(plan.Operation().Graphql().max_serialized_request_body_bytes_per_request),
      max_scan_body_bytes(plan.Operation().Graphql().max_serialized_request_body_bytes_per_scan),
      requires_bearer(requires_bearer_p), page_budgets(plan.Pagination().PageBudgets()),
      scan_budgets(plan.Pagination().ScanBudgets()) {
	const auto &operation = plan.Operation().Graphql();
	host = operation.origin.host;
	path = operation.path;
	(void)TryCopyFixedHeaders(operation.headers, true, page_budgets.header_bytes, headers);
	document = operation.document;
	for (const auto &column : operation.result_columns) {
		ValueKind kind = ValueKind::VARCHAR;
		const char *logical = nullptr;
		(void)TryScalarKind(column.scalar_kind, kind, logical);
		columns.push_back({column.name, kind, column.nullable, column.response_path.segments});
	}
	page_size_variable = operation.cursor.page_size_variable;
	cursor_variable = operation.cursor.cursor_variable;
	nodes_path = operation.response.nodes.segments;
	errors_path = operation.response.errors.segments;
	page_info_path = operation.response.page_info.segments;
	has_next_page_path = operation.cursor.has_next_page.segments;
	end_cursor_path = operation.cursor.end_cursor.segments;
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
const std::string &AdmittedGraphqlRequestProfile::PageSizeVariable() const {
	return page_size_variable;
}
const std::string &AdmittedGraphqlRequestProfile::CursorVariable() const {
	return cursor_variable;
}
const std::vector<std::string> &AdmittedGraphqlRequestProfile::NodesPath() const {
	return nodes_path;
}
const std::vector<std::string> &AdmittedGraphqlRequestProfile::ErrorsPath() const {
	return errors_path;
}
const std::vector<std::string> &AdmittedGraphqlRequestProfile::PageInfoPath() const {
	return page_info_path;
}
const std::vector<std::string> &AdmittedGraphqlRequestProfile::HasNextPagePath() const {
	return has_next_page_path;
}
const std::vector<std::string> &AdmittedGraphqlRequestProfile::EndCursorPath() const {
	return end_cursor_path;
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
bool AdmittedGraphqlRequestProfile::RequiresBearer() const {
	return requires_bearer;
}
const ResourceBudgets &AdmittedGraphqlRequestProfile::PageBudgets() const {
	return page_budgets;
}
const ScanResourceBudgets &AdmittedGraphqlRequestProfile::ScanBudgets() const {
	return scan_budgets;
}

std::unique_ptr<const AdmittedGraphqlRequestProfile> TryAdmitGraphqlPlan(const ScanPlan &plan,
                                                                         const HttpExecutionProfile &profile) {
	try {
		bool requires_bearer = false;
		if (!IsAdmissible(plan, profile, requires_bearer)) {
			return {};
		}
		return std::unique_ptr<const AdmittedGraphqlRequestProfile>(
		    new AdmittedGraphqlRequestProfile(plan, requires_bearer));
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
		                     "GraphQL request profile could not be allocated within its memory budget");
	} catch (const ExecutionError &) {
		return {};
	} catch (...) {
		return {};
	}
}

} // namespace internal
} // namespace duckdb_api
