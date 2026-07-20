#include "graphql_operation_planner.hpp"

#include <algorithm>
#include <stdexcept>

namespace duckdb_api {
namespace scan_planner_internal {
namespace {

bool Contains(const std::vector<std::string> &values, const std::string &expected) {
	return std::find(values.begin(), values.end(), expected) != values.end();
}

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

PlannedGraphqlResponsePath PlanPath(const CompiledGraphqlResponsePath &path) {
	return {path.segments};
}

PlannedGraphqlScalarKind PlanScalar(CompiledGraphqlScalarKind kind) {
	switch (kind) {
	case CompiledGraphqlScalarKind::STRING:
		return PlannedGraphqlScalarKind::STRING;
	case CompiledGraphqlScalarKind::INT64:
		return PlannedGraphqlScalarKind::INT64;
	case CompiledGraphqlScalarKind::BOOLEAN:
		return PlannedGraphqlScalarKind::BOOLEAN;
	}
	throw std::logic_error("compiled GraphQL profile contains an unknown scalar kind");
}

PlannedGraphqlVariableType PlanVariableType(CompiledGraphqlVariableType type) {
	switch (type) {
	case CompiledGraphqlVariableType::INT_NON_NULL:
		return PlannedGraphqlVariableType::INT_NON_NULL;
	case CompiledGraphqlVariableType::STRING_NULLABLE:
		return PlannedGraphqlVariableType::STRING_NULLABLE;
	}
	throw std::logic_error("compiled GraphQL profile contains an unknown variable type");
}

PlannedGraphqlVariableSource PlanVariableSource(CompiledGraphqlVariableSource source) {
	switch (source) {
	case CompiledGraphqlVariableSource::FIXED_PAGE_SIZE:
		return PlannedGraphqlVariableSource::FIXED_PAGE_SIZE;
	case CompiledGraphqlVariableSource::RUNTIME_CURSOR:
		return PlannedGraphqlVariableSource::RUNTIME_CURSOR;
	case CompiledGraphqlVariableSource::CALLER_INPUT:
	case CompiledGraphqlVariableSource::LOGICAL_SECRET:
		break;
	}
	throw std::logic_error("compiled GraphQL profile contains an unsupported variable authority");
}

bool ColumnMatches(const CompiledColumn &column, const CompiledGraphqlResultColumn &result, const char *name,
                   const char *logical_type, const char *extractor, CompiledGraphqlScalarKind scalar, bool nullable,
                   std::initializer_list<const char *> path) {
	return column.name == name && column.logical_type == logical_type && column.extractor == extractor &&
	       column.nullable == nullable && result.name == name && result.scalar_kind == scalar &&
	       result.nullable == nullable && PathEquals(result.response_path, path);
}

bool HasCanonicalHeaders(const std::vector<CompiledHttpHeader> &headers) {
	return headers.size() == 4 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "Content-Type" && headers[1].value == "application/json" &&
	       headers[2].name == "User-Agent" && headers[2].value == "duckdb-api/0.7.0" &&
	       headers[3].name == "X-GitHub-Api-Version" && headers[3].value == "2022-11-28";
}

} // namespace

void ValidateGraphqlOperationProfile(const CompiledRelation &relation, const CompiledOperation &operation,
                                     const CompiledNetworkPolicy &network_policy) {
	if (operation.Protocol() != CompiledProtocol::GRAPHQL || operation.name != "github_viewer_repository_metrics" ||
	    operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || !operation.fallback ||
	    !operation.selector.RequiredInputs().empty() || !operation.selector.AnyInputSets().empty() ||
	    !operation.selector.ForbiddenInputs().empty() || operation.selector.Priority() != 0 ||
	    !relation.PredicateMappings().empty()) {
		throw std::logic_error("selected GraphQL relation is not the closed zero-input base operation");
	}
	const auto &graphql = operation.Graphql();
	if (graphql.document_identity != CompiledGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 ||
	    graphql.digest_algorithm != CompiledGraphqlDigestAlgorithm::SHA256 || graphql.document.empty() ||
	    graphql.max_document_bytes != 4096 || graphql.document.size() > graphql.max_document_bytes ||
	    !IsCanonicalGraphqlDocumentProfile(graphql.document_identity, graphql.document, graphql.digest_algorithm,
	                                       graphql.document_digest)) {
		throw std::logic_error("selected GraphQL document is outside the canonical query profile");
	}
	if (graphql.endpoint_origin.scheme != CompiledUrlScheme::HTTPS ||
	    graphql.endpoint_origin.host.Value() != "api.github.com" || graphql.endpoint_origin.port != 443 ||
	    graphql.endpoint_path != "/graphql" || !HasCanonicalHeaders(graphql.headers) ||
	    !Contains(network_policy.allowed_schemes, "https") ||
	    !Contains(network_policy.allowed_hosts, "api.github.com") || network_policy.redirects_enabled ||
	    network_policy.private_addresses_enabled || network_policy.link_local_addresses_enabled ||
	    network_policy.loopback_addresses_enabled) {
		throw std::logic_error("selected GraphQL endpoint exceeds its exact network authority");
	}
	if (graphql.variables.size() != 2 || graphql.variables[0].name != "pageSize" ||
	    graphql.variables[0].type != CompiledGraphqlVariableType::INT_NON_NULL ||
	    graphql.variables[0].source != CompiledGraphqlVariableSource::FIXED_PAGE_SIZE ||
	    graphql.variables[0].integer_value != 100 || graphql.variables[1].name != "cursor" ||
	    graphql.variables[1].type != CompiledGraphqlVariableType::STRING_NULLABLE ||
	    graphql.variables[1].source != CompiledGraphqlVariableSource::RUNTIME_CURSOR ||
	    graphql.variables[1].integer_value != 0) {
		throw std::logic_error("selected GraphQL variables contradict the canonical cursor profile");
	}
	if (graphql.response.partial_data != CompiledGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR ||
	    !PathEquals(graphql.response.nodes, {"data", "viewer", "repositories", "nodes"}) ||
	    !PathEquals(graphql.response.errors, {"errors"}) ||
	    !PathEquals(graphql.response.page_info, {"data", "viewer", "repositories", "pageInfo"}) ||
	    !PathEquals(graphql.cursor.has_next_page, {"data", "viewer", "repositories", "pageInfo", "hasNextPage"}) ||
	    !PathEquals(graphql.cursor.end_cursor, {"data", "viewer", "repositories", "pageInfo", "endCursor"})) {
		throw std::logic_error("selected GraphQL response profile drifted from fail-only cursor traversal");
	}
	const auto &cursor = graphql.cursor;
	if (cursor.direction != CompiledGraphqlCursorDirection::FORWARD ||
	    cursor.dependency != CompiledGraphqlCursorDependency::SEQUENTIAL ||
	    cursor.consistency != CompiledGraphqlCursorConsistency::MUTABLE || cursor.supports_total ||
	    cursor.supports_resume || cursor.max_concurrent_pages != 1 || cursor.page_size_variable != "pageSize" ||
	    cursor.page_size != 100 || cursor.cursor_variable != "cursor" || cursor.max_pages_per_scan != 32 ||
	    graphql.retry_enabled || graphql.cache_enabled || graphql.providers_enabled ||
	    graphql.max_serialized_request_body_bytes_per_request != 8ULL * 1024ULL ||
	    graphql.max_serialized_request_body_bytes_per_scan != 256ULL * 1024ULL) {
		throw std::logic_error("selected GraphQL cursor, replay, feature, or body envelope drifted");
	}
	const auto &columns = relation.Columns();
	const auto &results = graphql.result_columns;
	if (columns.size() != 8 || results.size() != 8 ||
	    !ColumnMatches(columns[0], results[0], "id", "VARCHAR", "$.id", CompiledGraphqlScalarKind::STRING, false,
	                   {"id"}) ||
	    !ColumnMatches(columns[1], results[1], "full_name", "VARCHAR", "$.nameWithOwner",
	                   CompiledGraphqlScalarKind::STRING, false, {"nameWithOwner"}) ||
	    !ColumnMatches(columns[2], results[2], "owner_login", "VARCHAR", "$.owner.login",
	                   CompiledGraphqlScalarKind::STRING, false, {"owner", "login"}) ||
	    !ColumnMatches(columns[3], results[3], "stars", "BIGINT", "$.stargazerCount", CompiledGraphqlScalarKind::INT64,
	                   false, {"stargazerCount"}) ||
	    !ColumnMatches(columns[4], results[4], "primary_language", "VARCHAR", "$.primaryLanguage.name",
	                   CompiledGraphqlScalarKind::STRING, true, {"primaryLanguage", "name"}) ||
	    !ColumnMatches(columns[5], results[5], "private", "BOOLEAN", "$.isPrivate", CompiledGraphqlScalarKind::BOOLEAN,
	                   false, {"isPrivate"}) ||
	    !ColumnMatches(columns[6], results[6], "archived", "BOOLEAN", "$.isArchived",
	                   CompiledGraphqlScalarKind::BOOLEAN, false, {"isArchived"}) ||
	    !ColumnMatches(columns[7], results[7], "updated_at", "VARCHAR", "$.updatedAt",
	                   CompiledGraphqlScalarKind::STRING, false, {"updatedAt"})) {
		throw std::logic_error("selected GraphQL planned schema or nullability drifted");
	}
	const auto &ceilings = relation.ResourceCeilings();
	if (!ceilings.HasResponseByteNarrowing() || ceilings.MaxResponseBytesPerPage() == 0 ||
	    ceilings.MaxResponseBytesPerPage() > network_policy.max_response_bytes ||
	    ceilings.MaxResponseBytesPerScan() < ceilings.MaxResponseBytesPerPage() ||
	    ceilings.MaxRecordsPerPage() != 100 || ceilings.MaxRecordsPerScan() != 3200 ||
	    ceilings.MaxExtractedStringBytes() == 0 || ceilings.MaxExtractedStringBytes() > 512) {
		throw std::logic_error("selected GraphQL response resource envelope drifted");
	}
}

PlannedGraphqlOperation PlanGraphqlOperation(const CompiledOperation &operation) {
	const auto &source = operation.Graphql();
	std::vector<PlannedGraphqlVariable> variables;
	for (const auto &variable : source.variables) {
		variables.push_back({variable.name, PlanVariableType(variable.type), PlanVariableSource(variable.source),
		                     variable.integer_value});
	}
	std::vector<PlannedGraphqlResultColumn> columns;
	for (const auto &column : source.result_columns) {
		columns.push_back(
		    {column.name, PlanScalar(column.scalar_kind), column.nullable, PlanPath(column.response_path)});
	}
	std::vector<PlannedHttpHeader> headers;
	for (const auto &header : source.headers) {
		headers.push_back({header.name, header.value});
	}
	return {operation.name,
	        PlanCardinality(operation.cardinality),
	        PlannedReplaySafety::SAFE,
	        PlannedGraphqlOperationKind::QUERY,
	        PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1,
	        source.document,
	        PlannedGraphqlDigestAlgorithm::SHA256,
	        source.document_digest,
	        {PlanUrlScheme(source.endpoint_origin.scheme), source.endpoint_origin.host.Value(),
	         source.endpoint_origin.port},
	        source.endpoint_path,
	        std::move(headers),
	        std::move(variables),
	        std::move(columns),
	        {PlanPath(source.response.nodes), PlanPath(source.response.errors), PlanPath(source.response.page_info),
	         PlannedGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR},
	        {PlannedGraphqlCursorDirection::FORWARD, PlannedGraphqlCursorDependency::SEQUENTIAL,
	         PlannedGraphqlCursorConsistency::MUTABLE, false, false, 1, source.cursor.page_size_variable,
	         source.cursor.page_size, source.cursor.cursor_variable, PlanPath(source.cursor.has_next_page),
	         PlanPath(source.cursor.end_cursor), source.cursor.max_pages_per_scan},
	        source.max_document_bytes,
	        source.max_serialized_request_body_bytes_per_request,
	        source.max_serialized_request_body_bytes_per_scan};
}

} // namespace scan_planner_internal
} // namespace duckdb_api
