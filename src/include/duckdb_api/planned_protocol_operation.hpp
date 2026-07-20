#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api_test {
class ScanPlanFixtureBuilder;
class ScanPlanTestAccess;
} // namespace duckdb_api_test

namespace duckdb_api {

class ScanPlanBuilder;

enum class PlannedProtocol { REST, GRAPHQL };
enum class PlannedHttpMethod { GET };
enum class PlannedCardinality { ZERO_TO_MANY, EXACTLY_ONE_ON_SUCCESS };
enum class PlannedReplaySafety { SAFE };
enum class PlannedUrlScheme { HTTP, HTTPS };
enum class PlannedResponseSource { JSON_PATH_MANY, ROOT_ARRAY, ROOT_OBJECT };

struct PlannedQueryParameter {
	std::string name;
	std::string encoded_value;
};

struct PlannedHttpHeader {
	std::string name;
	std::string value;
};

// Typed request authority shared by REST and GraphQL. It is copied from the
// validated Connector origin; consumers never recover it from a URL string.
struct PlannedHttpOrigin {
	PlannedUrlScheme scheme;
	std::string host;
	std::uint16_t port;
};

using PlannedRestOrigin = PlannedHttpOrigin;

struct PlannedRestOperation {
	std::string operation_name;
	PlannedHttpMethod method;
	PlannedCardinality cardinality;
	PlannedReplaySafety replay_safety;
	PlannedHttpOrigin origin;
	std::string path;
	std::vector<PlannedQueryParameter> query_parameters;
	std::vector<PlannedHttpHeader> headers;
	PlannedResponseSource response_source;
	std::string records_extractor;
};

enum class PlannedGraphqlDocumentIdentity { GITHUB_VIEWER_REPOSITORY_METRICS_V1 };
enum class PlannedGraphqlDigestAlgorithm { SHA256 };
enum class PlannedGraphqlOperationKind { QUERY };
enum class PlannedGraphqlVariableType { INT_NON_NULL, STRING_NULLABLE };
enum class PlannedGraphqlVariableSource { FIXED_PAGE_SIZE, RUNTIME_CURSOR };
enum class PlannedGraphqlScalarKind { STRING, INT64, BOOLEAN };
enum class PlannedGraphqlPartialDataPolicy { FAIL_ON_ANY_ERROR };
enum class PlannedGraphqlCursorDirection { FORWARD };
enum class PlannedGraphqlCursorDependency { SEQUENTIAL };
enum class PlannedGraphqlCursorConsistency { MUTABLE };

struct PlannedGraphqlResponsePath {
	std::vector<std::string> segments;
};

struct PlannedGraphqlVariable {
	std::string name;
	PlannedGraphqlVariableType type;
	PlannedGraphqlVariableSource source;
	std::uint64_t integer_value;
};

struct PlannedGraphqlResultColumn {
	std::string name;
	PlannedGraphqlScalarKind scalar_kind;
	bool nullable;
	PlannedGraphqlResponsePath response_path;
};

struct PlannedGraphqlResponse {
	PlannedGraphqlResponsePath nodes;
	PlannedGraphqlResponsePath errors;
	PlannedGraphqlResponsePath page_info;
	PlannedGraphqlPartialDataPolicy partial_data;
};

// Immutable cursor authority. Current and seen cursors remain Runtime state;
// the planned value only describes the admitted sequential transition.
struct PlannedGraphqlCursor {
	PlannedGraphqlCursorDirection direction;
	PlannedGraphqlCursorDependency dependency;
	PlannedGraphqlCursorConsistency consistency;
	bool supports_total;
	bool supports_resume;
	std::uint64_t max_concurrent_pages;
	std::string page_size_variable;
	std::uint64_t page_size;
	std::string cursor_variable;
	PlannedGraphqlResponsePath has_next_page;
	PlannedGraphqlResponsePath end_cursor;
	std::uint64_t max_pages_per_scan;
};

// Complete Semantics-owned executable GraphQL handoff. The exact document is
// retained for Runtime serialization, but explanation must never render it.
// Replay safety is derived by the planner only after canonical profile
// membership, exact bytes, digest, variables, response paths, and domain agree.
struct PlannedGraphqlOperation {
	std::string operation_name;
	PlannedCardinality cardinality;
	PlannedReplaySafety replay_safety;
	PlannedGraphqlOperationKind kind;
	PlannedGraphqlDocumentIdentity document_identity;
	std::string document;
	PlannedGraphqlDigestAlgorithm digest_algorithm;
	std::string document_digest;
	PlannedHttpOrigin origin;
	std::string path;
	std::vector<PlannedHttpHeader> headers;
	std::vector<PlannedGraphqlVariable> variables;
	std::vector<PlannedGraphqlResultColumn> result_columns;
	PlannedGraphqlResponse response;
	PlannedGraphqlCursor cursor;
	std::uint64_t max_document_bytes;
	std::uint64_t max_serialized_request_body_bytes_per_request;
	std::uint64_t max_serialized_request_body_bytes_per_scan;
};

// Exhaustive private pre-1.0 team API produced by Relational Semantics and
// consumed by Query and Remote Runtime. Wrong-variant access fails. Copies
// share immutable payload storage and are safe for prepared and concurrent
// execution lifetimes; the value owns no cursor, cancellation, close, secret,
// response, or transport state.
class PlannedProtocolOperation {
public:
	PlannedProtocolOperation(const PlannedProtocolOperation &) = default;
	PlannedProtocolOperation(PlannedProtocolOperation &&) = default;
	PlannedProtocolOperation &operator=(const PlannedProtocolOperation &) = delete;
	PlannedProtocolOperation &operator=(PlannedProtocolOperation &&) = delete;

	PlannedProtocol Protocol() const;
	const PlannedRestOperation &Rest() const;
	const PlannedGraphqlOperation &Graphql() const;

private:
	friend class ScanPlanBuilder;
	friend class duckdb_api_test::ScanPlanFixtureBuilder;
	friend class duckdb_api_test::ScanPlanTestAccess;

	static PlannedProtocolOperation FromRest(PlannedRestOperation operation);
	static PlannedProtocolOperation FromGraphql(PlannedGraphqlOperation operation);
	PlannedProtocolOperation(PlannedProtocol protocol, std::shared_ptr<const PlannedRestOperation> rest,
	                         std::shared_ptr<const PlannedGraphqlOperation> graphql);

	PlannedProtocol protocol;
	std::shared_ptr<const PlannedRestOperation> rest;
	std::shared_ptr<const PlannedGraphqlOperation> graphql;
};

} // namespace duckdb_api
