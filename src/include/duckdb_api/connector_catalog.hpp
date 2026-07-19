#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api_test {
class ConnectorCatalogTestAccess;
}

namespace duckdb_api {

class CompiledConnector;
CompiledConnector BuildNativeGithubConnector();

// Identifies how immutable metadata entered the product. The 0.5.0 native
// catalog is repository-owned product metadata, not compiled package syntax,
// package provenance, or a public connector-authoring/native ABI commitment.
enum class CompiledConnectorOrigin { NATIVE_PRODUCT_METADATA };

// Source cardinality is a declaration for Relational Semantics to interpret.
// EXACTLY_ONE_ON_SUCCESS is neither a row estimate nor permission to push a
// limit; authentication, transport, decode, and schema failures remain errors.
enum class CompiledOperationCardinality { ZERO_TO_MANY, EXACTLY_ONE_ON_SUCCESS };

enum class CompiledProtocol { REST };

enum class CompiledHttpMethod { GET };

enum class CompiledReplaySafety { SAFE };

// Distinguishes a nested JSONPath collection, a root array, and a single root
// object without asking a consumer to infer response shape from an extractor
// string. ROOT_ARRAY uses the canonical root extractor `$`; its enum value is
// the executable array contract.
enum class CompiledResponseSource { JSON_PATH_MANY, ROOT_ARRAY, ROOT_OBJECT };

enum class CompiledCredentialRequirement { NONE, REQUIRED };

enum class CompiledAuthenticator { NONE, BEARER };

// The native metadata exposes one closed placement, not a caller-supplied
// header name. Runtime owns construction of the eventual header and value.
enum class CompiledCredentialPlacement { NONE, AUTHORIZATION_HEADER };

// A closed transport scheme carried as data rather than recovered by parsing a
// URL. HTTP remains available to private non-installable test compositions; the
// installed native catalog selects HTTPS exclusively.
enum class CompiledUrlScheme { HTTP, HTTPS };

// One output column. The extractor is evaluated relative to one object selected
// by the response source. A non-nullable declaration makes missing or JSON-null
// extraction fail; it does not claim DuckDB-visible NOT NULL metadata.
struct CompiledColumn {
	std::string name;
	std::string logical_type;
	bool nullable;
	std::string extractor;
};

// One already-encoded fixed query field. Ordering and encoded_value are part of
// source identity, not DuckDB predicate or limit pushdown.
struct CompiledQueryParameter {
	std::string name;
	std::string encoded_value;
};

// One non-sensitive fixed request header. Authorization is deliberately
// unrepresentable here by catalog validation; credential placement lives only
// in CompiledAuthenticationPolicy.
struct CompiledHttpHeader {
	std::string name;
	std::string value;
};

// An exact lower-case DNS name or IPv4 literal. Construction rejects URL
// syntax, user information, embedded ports, empty labels, and non-canonical
// labels, so consumers never need to parse host authority from a string.
class CompiledRestHost {
public:
	explicit CompiledRestHost(std::string value);

	const std::string &Value() const;

private:
	std::string value;
};

// Typed request authority. It contains no user information, path, query, or
// fragment, and it is also the complete destination carried by bearer policy.
struct CompiledRestOrigin {
	CompiledUrlScheme scheme;
	CompiledRestHost host;
	std::uint16_t port;
};

// Structural REST request metadata. No field can carry a credential value.
struct CompiledRestRequest {
	CompiledRestOrigin origin;
	std::string path;
	std::vector<CompiledQueryParameter> query_parameters;
	std::vector<CompiledHttpHeader> headers;
};

// Pagination is a closed source declaration, not executable page state. A
// disabled value has no payload. The only enabled native shape is a sequential
// Link transition whose typed query bindings must agree with the fixed initial
// request. Semantics consumes these facts without discovering pagination from
// request strings; Runtime receives only the resulting immutable plan.
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

// One base-row operation declaration. Connector owns declared source facts;
// Semantics owns their conservative meaning and Runtime owns enforcement of the
// resulting plan. Authentication is derived from the relation policy rather
// than duplicated as a mutable boolean here.
struct CompiledOperation {
	std::string name;
	bool fallback;
	CompiledOperationCardinality cardinality;
	CompiledProtocol protocol;
	CompiledHttpMethod method;
	CompiledReplaySafety replay_safety;
	bool retry_enabled;
	CompiledPagination pagination;
	CompiledRestRequest request;
	CompiledResponseSource response_source;
	std::string records_extractor;
};

// Connector policy only narrows host authority. Runtime intersects these facts
// with host policy; the catalog does not grant network access.
struct CompiledNetworkPolicy {
	std::vector<std::string> allowed_schemes;
	std::vector<std::string> allowed_hosts;
	bool redirects_enabled;
	bool private_addresses_enabled;
	bool link_local_addresses_enabled;
	bool loopback_addresses_enabled;
	uint64_t max_response_bytes;
};

// Immutable relation-owned resource narrowings. Page and scan scopes are
// distinct so a per-page decoder ceiling cannot silently truncate a complete
// paginated source. Absence of a response-byte narrowing inherits the
// connector-wide per-response policy; records and extracted strings are always
// explicit. Semantics intersects these declarations with host policy and
// Runtime owns counters and enforcement. Header, decompression, decode-memory,
// batch, wall-time, and concurrency mechanics do not enter this type.
class CompiledResourceCeilings {
public:
	CompiledResourceCeilings(const CompiledResourceCeilings &) = default;
	CompiledResourceCeilings(CompiledResourceCeilings &&) = default;
	CompiledResourceCeilings &operator=(const CompiledResourceCeilings &) = delete;
	CompiledResourceCeilings &operator=(CompiledResourceCeilings &&) = delete;

	bool HasResponseByteNarrowing() const;
	std::uint64_t MaxResponseBytesPerPage() const;
	std::uint64_t MaxResponseBytesPerScan() const;
	std::uint64_t MaxRecordsPerPage() const;
	std::uint64_t MaxRecordsPerScan() const;
	std::uint64_t MaxExtractedStringBytes() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	// An unpaginated declaration may inherit the connector response ceiling and
	// always has identical page/scan record scopes. The native 0.5.0 relations
	// instead use explicit response narrowings.
	CompiledResourceCeilings(std::uint64_t max_records, std::uint64_t max_extracted_string_bytes);
	CompiledResourceCeilings(std::uint64_t max_response_bytes_per_page, std::uint64_t max_response_bytes_per_scan,
	                         std::uint64_t max_records_per_page, std::uint64_t max_records_per_scan,
	                         std::uint64_t max_extracted_string_bytes);
	void RequireResponseByteNarrowing() const;

	bool has_response_byte_narrowing;
	std::uint64_t max_response_bytes_per_page;
	std::uint64_t max_response_bytes_per_scan;
	std::uint64_t max_records_per_page;
	std::uint64_t max_records_per_scan;
	std::uint64_t max_extracted_string_bytes;
};

// Closed logical credential policy. Its private Anonymous() state carries no
// binding; its private RequiredBearer() state always carries logical `token`
// and exact GitHub use policy but no DuckDB secret name, provider object,
// handle, or credential bytes. Values are immutable after validated
// construction and safe to retain with a snapshot.
class CompiledAuthenticationPolicy {
public:
	CompiledAuthenticationPolicy(const CompiledAuthenticationPolicy &) = default;
	CompiledAuthenticationPolicy(CompiledAuthenticationPolicy &&) = default;
	CompiledAuthenticationPolicy &operator=(const CompiledAuthenticationPolicy &) = delete;
	CompiledAuthenticationPolicy &operator=(CompiledAuthenticationPolicy &&) = delete;

	CompiledCredentialRequirement Requirement() const;
	const std::string &LogicalCredential() const;
	CompiledAuthenticator Authenticator() const;
	CompiledCredentialPlacement Placement() const;
	const CompiledRestOrigin *Destination() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	static CompiledAuthenticationPolicy Anonymous();
	static CompiledAuthenticationPolicy RequiredBearer();

	CompiledAuthenticationPolicy(CompiledCredentialRequirement requirement, std::string logical_credential,
	                             CompiledAuthenticator authenticator, CompiledCredentialPlacement placement,
	                             std::vector<CompiledRestOrigin> destinations);

	CompiledCredentialRequirement requirement;
	std::string logical_credential;
	CompiledAuthenticator authenticator;
	CompiledCredentialPlacement placement;
	std::vector<CompiledRestOrigin> destinations;
};

// Immutable relation-level Connector handoff. Construction belongs only to
// the native provider and Connector-owned non-installable test access;
// production consumers use the canonical catalog and const accessors. Values
// stay independent of DuckDB, planning, transport, and runtime lifecycle.
class CompiledRelation {
public:
	CompiledRelation(const CompiledRelation &) = default;
	CompiledRelation(CompiledRelation &&) = default;
	CompiledRelation &operator=(const CompiledRelation &) = delete;
	CompiledRelation &operator=(CompiledRelation &&) = delete;

	const std::string &Name() const;
	const std::vector<CompiledColumn> &Columns() const;
	const CompiledOperation &Operation() const;
	const CompiledAuthenticationPolicy &Authentication() const;
	const CompiledResourceCeilings &ResourceCeilings() const;

	// Stable safe explanation of relation-owned source metadata. It is neither a
	// serialization format nor runtime authority and contains no secret name or
	// credential value.
	std::string Snapshot() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	CompiledRelation(std::string name, std::vector<CompiledColumn> columns, CompiledOperation operation,
	                 CompiledAuthenticationPolicy authentication, CompiledResourceCeilings resource_ceilings);

	std::string name;
	std::vector<CompiledColumn> columns;
	CompiledOperation operation;
	CompiledAuthenticationPolicy authentication;
	CompiledResourceCeilings resource_ceilings;
};

// Immutable Connector Experience service for the native 0.5.0 relation
// catalog. Query and Semantics consume exact lookup and const accessors without
// constructing policy internals. Copy construction supports immutable bind and
// composition lifetimes; assignment and partial aggregate mutation are
// forbidden. The native builder is its sole production construction point.
// This private C++ team API is not package compatibility or a public ABI.
class CompiledConnector {
public:
	CompiledConnector(const CompiledConnector &) = default;
	CompiledConnector(CompiledConnector &&) = default;
	CompiledConnector &operator=(const CompiledConnector &) = delete;
	CompiledConnector &operator=(CompiledConnector &&) = delete;

	CompiledConnectorOrigin Origin() const;
	const std::string &ConnectorName() const;
	const std::string &Version() const;
	const std::vector<CompiledRelation> &Relations() const;
	const CompiledRelation *FindRelation(const std::string &relation_name) const;
	const CompiledNetworkPolicy &NetworkPolicy() const;

	// Canonical source-explainable catalog snapshot. Relation ordering is stable
	// for this metadata version. The output is safe for plan provenance but is
	// not a parser input or source of execution authority.
	std::string Snapshot() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	CompiledConnector(CompiledConnectorOrigin origin, std::string connector_name, std::string version,
	                  std::vector<CompiledRelation> relations, CompiledNetworkPolicy network_policy);

	CompiledConnectorOrigin origin;
	std::string connector_name;
	std::string version;
	std::vector<CompiledRelation> relations;
	CompiledNetworkPolicy network_policy;
};

} // namespace duckdb_api
