#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api_test {
class ConnectorCatalogTestAccess;
}

namespace duckdb_api {

namespace internal {
class CompiledModelBuilder;
}

class CompiledConnector;
CompiledConnector BuildNativeGithubConnector();

// Source cardinality is a declaration for Relational Semantics to interpret.
// EXACTLY_ONE_ON_SUCCESS is neither a row estimate nor permission to push a
// limit; authentication, transport, decode, and schema failures remain errors.
enum class CompiledOperationCardinality { ZERO_TO_MANY, EXACTLY_ONE_ON_SUCCESS };

enum class CompiledProtocol { REST, GRAPHQL };
enum class CompiledHttpMethod { GET };
enum class CompiledReplaySafety { SAFE };

// Closed identity for the one repository-owned GraphQL document admitted by
// the 0.7 preview. Identity is not query-kind or replay authority by itself:
// Connector validation requires the exact bytes and recomputed SHA-256 digest
// to agree with this profile before the immutable catalog is published.
enum class CompiledGraphqlDocumentIdentity { GITHUB_VIEWER_REPOSITORY_METRICS_V1 };
enum class CompiledGraphqlDigestAlgorithm { SHA256 };
enum class CompiledGraphqlVariableType { INT_NON_NULL, STRING_NULLABLE };
enum class CompiledGraphqlVariableSource { FIXED_PAGE_SIZE, RUNTIME_CURSOR, CALLER_INPUT, LOGICAL_SECRET };
enum class CompiledGraphqlScalarKind { STRING, INT64, BOOLEAN };
enum class CompiledGraphqlPartialDataPolicy { FAIL_ON_ANY_ERROR };
enum class CompiledGraphqlCursorDirection { FORWARD };
enum class CompiledGraphqlCursorDependency { SEQUENTIAL, INDEPENDENT };
enum class CompiledGraphqlCursorConsistency { MUTABLE, STABLE_ORDERING, STABLE_SNAPSHOT };

// Distinguishes a nested JSONPath collection, a root array, and a single root
// object without asking a consumer to infer response shape from an extractor.
enum class CompiledResponseSource { JSON_PATH_MANY, ROOT_ARRAY, ROOT_OBJECT };

// A closed transport scheme carried as data rather than recovered by parsing a
// URL. HTTP remains available to private non-installable test compositions; the
// installed native catalog selects HTTPS exclusively.
enum class CompiledUrlScheme { HTTP, HTTPS };

// One already-encoded fixed REST query field. Ordering and encoded_value are
// part of source identity, not DuckDB predicate or limit pushdown.
struct CompiledQueryParameter {
	std::string name;
	std::string encoded_value;
};

// One non-sensitive fixed HTTP header. Authorization is deliberately
// unrepresentable here by catalog validation; credential placement lives in
// the relation authentication policy.
struct CompiledHttpHeader {
	std::string name;
	std::string value;
};

// An exact lower-case DNS name or IPv4 literal. Construction rejects URL
// syntax, user information, embedded ports, empty labels, and non-canonical
// labels, so consumers never need to parse host authority from a string.
class CompiledHttpHost {
public:
	explicit CompiledHttpHost(std::string value);

	const std::string &Value() const;

private:
	std::string value;
};

// Protocol-neutral HTTP request authority. It contains no user information,
// path, query, or fragment. REST requests, GraphQL endpoints, and relation
// authentication policy all reuse this value without owning its semantics.
struct CompiledHttpOrigin {
	CompiledUrlScheme scheme;
	CompiledHttpHost host;
	std::uint16_t port;
};

// Temporary pre-1.0 compatibility spellings for accepted REST consumers.
using CompiledRestHost = CompiledHttpHost;
using CompiledRestOrigin = CompiledHttpOrigin;

// Structural REST request metadata. No field can carry a credential value.
struct CompiledRestRequest {
	CompiledHttpOrigin origin;
	std::string path;
	std::vector<CompiledQueryParameter> query_parameters;
	std::vector<CompiledHttpHeader> headers;
};

struct CompiledGraphqlVariable {
	std::string name;
	CompiledGraphqlVariableType type;
	CompiledGraphqlVariableSource source;
	std::uint64_t integer_value;
};

// Structural field segments, not a JSONPath program. Consumers do not parse a
// snapshot to recover row, error, pageInfo, cursor, or result-column authority.
struct CompiledGraphqlResponsePath {
	std::vector<std::string> segments;
};

// Protocol-typed result mapping consumed without parsing relation logical-type
// or extractor strings. Canonical catalog validation requires exact agreement
// with relation columns in order, scalar kind, nullability, and structural path.
struct CompiledGraphqlResultColumn {
	std::string name;
	CompiledGraphqlScalarKind scalar_kind;
	bool nullable;
	CompiledGraphqlResponsePath response_path;
};

struct CompiledGraphqlResponse {
	CompiledGraphqlResponsePath nodes;
	CompiledGraphqlResponsePath errors;
	CompiledGraphqlResponsePath page_info;
	CompiledGraphqlPartialDataPolicy partial_data;
};

// Immutable forward-only Relay cursor declaration. It contains no received
// cursor, row, request body, or execution lifecycle.
struct CompiledGraphqlCursorPagination {
	CompiledGraphqlCursorDirection direction;
	CompiledGraphqlCursorDependency dependency;
	CompiledGraphqlCursorConsistency consistency;
	bool supports_total;
	bool supports_resume;
	std::uint64_t max_concurrent_pages;
	std::string page_size_variable;
	std::uint64_t page_size;
	std::string cursor_variable;
	CompiledGraphqlResponsePath has_next_page;
	CompiledGraphqlResponsePath end_cursor;
	std::uint64_t max_pages_per_scan;
};

// Complete immutable Connector-owned GraphQL source declaration. Runtime must
// serialize the retained document; safe snapshots omit it and all live state.
struct CompiledGraphqlOperation {
	CompiledGraphqlDocumentIdentity document_identity;
	std::string document;
	CompiledGraphqlDigestAlgorithm digest_algorithm;
	std::string document_digest;
	CompiledHttpOrigin endpoint_origin;
	std::string endpoint_path;
	std::vector<CompiledHttpHeader> headers;
	std::vector<CompiledGraphqlVariable> variables;
	std::vector<CompiledGraphqlResultColumn> result_columns;
	CompiledGraphqlResponse response;
	CompiledGraphqlCursorPagination cursor;
	std::uint64_t max_document_bytes;
	std::uint64_t max_serialized_request_body_bytes_per_request;
	std::uint64_t max_serialized_request_body_bytes_per_scan;
	bool retry_enabled;
	bool cache_enabled;
	bool providers_enabled;
};

// Bounded canonical-profile verifier for the Connector-to-Semantics handoff.
// Changed bytes plus their recomputed digest still fail reviewed membership.
// The verifier is deterministic, thread-safe, performs no I/O, and grants no
// request, replay, credential, or relational authority.
bool IsCanonicalGraphqlDocumentProfile(CompiledGraphqlDocumentIdentity identity, const std::string &document,
                                       CompiledGraphqlDigestAlgorithm algorithm, const std::string &digest);

// REST pagination is a closed source declaration, never executable page state.
enum class CompiledPaginationStrategy { DISABLED, LINK_HEADER };
enum class CompiledPageDependency { SEQUENTIAL };
enum class CompiledPageConsistency { MUTABLE };
enum class CompiledLinkRelation { NEXT };
enum class CompiledContinuationTargetScope { EXACT_OPERATION_ORIGIN_AND_PATH };

class CompiledPagination {
public:
	CompiledPagination(const CompiledPagination &) = default;
	CompiledPagination(CompiledPagination &&) = default;
	CompiledPagination &operator=(const CompiledPagination &) = delete;
	CompiledPagination &operator=(CompiledPagination &&) = delete;

	CompiledPaginationStrategy Strategy() const;
	CompiledPageDependency Dependency() const;
	CompiledPageConsistency Consistency() const;
	CompiledLinkRelation LinkRelation() const;
	CompiledContinuationTargetScope TargetScope() const;
	bool SupportsTotal() const;
	bool SupportsResume() const;
	const std::string &PageSizeParameter() const;
	std::uint64_t PageSize() const;
	const std::string &PageNumberParameter() const;
	std::uint64_t FirstPage() const;
	std::uint64_t PageIncrement() const;
	std::uint64_t MaxPagesPerScan() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class internal::CompiledModelBuilder;
	friend class CompiledOperation;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	static CompiledPagination Disabled();
	CompiledPagination();
	CompiledPagination(std::string page_size_parameter, std::uint64_t page_size, std::string page_number_parameter,
	                   std::uint64_t first_page, std::uint64_t page_increment, std::uint64_t max_pages_per_scan);
	void RequireLinkHeader() const;

	CompiledPaginationStrategy strategy;
	std::string page_size_parameter;
	std::uint64_t page_size;
	std::string page_number_parameter;
	std::uint64_t first_page;
	std::uint64_t page_increment;
	std::uint64_t max_pages_per_scan;
};

// REST retains its accepted 0.6 values as one alternative in the permanent
// protocol sum.
struct CompiledRestOperation {
	CompiledHttpMethod method;
	CompiledReplaySafety replay_safety;
	bool retry_enabled;
	CompiledPagination pagination;
	CompiledRestRequest request;
	CompiledResponseSource response_source;
	std::string records_extractor;
};

// Exhaustive immutable built-in protocol value. Wrong-variant access fails;
// copies share const payload storage and assignment cannot replace authority.
class CompiledProtocolOperation {
public:
	CompiledProtocolOperation(const CompiledProtocolOperation &) = default;
	CompiledProtocolOperation(CompiledProtocolOperation &&) = default;
	CompiledProtocolOperation &operator=(const CompiledProtocolOperation &) = delete;
	CompiledProtocolOperation &operator=(CompiledProtocolOperation &&) = delete;

	CompiledProtocol Protocol() const;
	const CompiledRestOperation &Rest() const;
	const CompiledGraphqlOperation &Graphql() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class CompiledOperation;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	static CompiledProtocolOperation FromRest(CompiledRestOperation operation);
	static CompiledProtocolOperation FromGraphql(CompiledGraphqlOperation operation);
	CompiledProtocolOperation(CompiledProtocol protocol, std::shared_ptr<const CompiledRestOperation> rest,
	                          std::shared_ptr<const CompiledGraphqlOperation> graphql);

	CompiledProtocol protocol;
	std::shared_ptr<const CompiledRestOperation> rest;
	std::shared_ptr<const CompiledGraphqlOperation> graphql;
};

// A required selector reference retains its compiled namespace. Relation
// inputs and operation-local predicate inputs may share an identifier without
// becoming interchangeable or requiring a consumer to parse `input.` or
// `conditional.` prefixes.
enum class CompiledRequiredInputKind { RELATION_INPUT, CONDITIONAL_INPUT };

class CompiledRequiredInputReference {
public:
	CompiledRequiredInputReference(const CompiledRequiredInputReference &) = default;
	CompiledRequiredInputReference(CompiledRequiredInputReference &&) = default;
	CompiledRequiredInputReference &operator=(const CompiledRequiredInputReference &) = delete;
	CompiledRequiredInputReference &operator=(CompiledRequiredInputReference &&) = delete;

	CompiledRequiredInputKind Kind() const;
	const std::string &Id() const;

private:
	friend class internal::CompiledModelBuilder;

	CompiledRequiredInputReference(CompiledRequiredInputKind kind, std::string id);

	CompiledRequiredInputKind kind;
	std::string id;
};

// Immutable operation-selection facts. duckdb_api/v1 carries only tagged
// required references and fallback. Relational Semantics alone resolves
// bindings, determines eligibility, ranks by satisfied-reference count, and
// chooses or fails a tie.
class CompiledOperationSelector {
public:
	// Temporary native/controlled-fixture compatibility bridge. It creates an
	// empty legacy selector; package generations must use Connector's private v1
	// builder, including for an empty required-reference set.
	CompiledOperationSelector();
	CompiledOperationSelector(const CompiledOperationSelector &) = default;
	CompiledOperationSelector(CompiledOperationSelector &&) = default;
	CompiledOperationSelector &operator=(const CompiledOperationSelector &) = delete;
	CompiledOperationSelector &operator=(CompiledOperationSelector &&) = delete;

	const std::vector<CompiledRequiredInputReference> &RequiredInputReferences() const;
	bool IsLegacyCompatibilityBridge() const;

	// Temporary bridge for the pre-v1 controlled selector fixtures consumed by
	// Semantics. Package generations always return empty values and priority
	// zero here. Delete these accessors and the matching test constructor after
	// Semantics migrates to RequiredInputReferences().
	const std::vector<std::string> &RequiredInputs() const;
	const std::vector<std::vector<std::string>> &AnyInputSets() const;
	const std::vector<std::string> &ForbiddenInputs() const;
	std::int32_t Priority() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class internal::CompiledModelBuilder;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	CompiledOperationSelector(std::vector<std::string> required_inputs,
	                          std::vector<std::vector<std::string>> any_input_sets,
	                          std::vector<std::string> forbidden_inputs, std::int32_t priority);
	CompiledOperationSelector(std::vector<CompiledRequiredInputReference> required_input_references);

	std::vector<CompiledRequiredInputReference> required_input_references;
	std::vector<std::string> required_inputs;
	std::vector<std::vector<std::string>> any_input_sets;
	std::vector<std::string> forbidden_inputs;
	std::int32_t priority;
	bool legacy_compatibility_bridge;
};

// One base-row operation declaration. Connector owns source facts, Semantics
// owns conservative meaning, and Runtime owns enforcement of the resulting plan.
class CompiledOperation {
public:
	// Compatibility construction for accepted REST metadata and tests.
	CompiledOperation(std::string name, bool fallback, CompiledOperationCardinality cardinality,
	                  CompiledProtocol protocol, CompiledHttpMethod method, CompiledReplaySafety replay_safety,
	                  bool retry_enabled, CompiledPagination pagination, CompiledRestRequest request,
	                  CompiledResponseSource response_source, std::string records_extractor,
	                  CompiledOperationSelector selector);
	CompiledOperation(const CompiledOperation &) = default;
	CompiledOperation(CompiledOperation &&) = default;
	CompiledOperation &operator=(const CompiledOperation &) = delete;
	CompiledOperation &operator=(CompiledOperation &&) = delete;

	CompiledProtocol Protocol() const;
	const CompiledProtocolOperation &ProtocolOperation() const;
	const CompiledRestOperation &Rest() const;
	const CompiledGraphqlOperation &Graphql() const;

	std::string name;
	bool fallback;
	CompiledOperationCardinality cardinality;
	CompiledOperationSelector selector;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	CompiledOperation(std::string name, bool fallback, CompiledOperationCardinality cardinality,
	                  CompiledGraphqlOperation operation, CompiledOperationSelector selector);

	CompiledProtocolOperation protocol_operation;
};

} // namespace duckdb_api
