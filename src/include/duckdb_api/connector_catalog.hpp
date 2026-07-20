#pragma once

#include "duckdb_api/compiled_protocol_operation.hpp"

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

// Identifies how immutable metadata entered the product. The 0.7.0 native
// catalog is repository-owned product metadata, not compiled package syntax,
// package provenance, or a public connector-authoring/native ABI commitment.
enum class CompiledConnectorOrigin { NATIVE_PRODUCT_METADATA, PACKAGE_COMPILED_METADATA };

// Closed scalar vocabulary shared by package-declared columns, relation
// inputs, typed defaults, and consumer projections. Consumers switch on this
// enum; they never parse YAML or logical-type strings to recover authority.
enum class CompiledScalarType { BOOLEAN, BIGINT, VARCHAR };

const char *CompiledScalarTypeName(CompiledScalarType type);

// Immutable typed scalar. NULL retains its declared scalar type so an absent
// default, a typed NULL default, and a concrete value remain three distinct
// states after package source has been discarded.
class CompiledScalarValue {
public:
	CompiledScalarValue(const CompiledScalarValue &) = default;
	CompiledScalarValue(CompiledScalarValue &&) = default;
	CompiledScalarValue &operator=(const CompiledScalarValue &) = delete;
	CompiledScalarValue &operator=(CompiledScalarValue &&) = delete;

	CompiledScalarType Type() const;
	bool IsNull() const;
	bool Boolean() const;
	std::int64_t Bigint() const;
	const std::string &Varchar() const;

private:
	friend class internal::CompiledModelBuilder;

	CompiledScalarValue(CompiledScalarType type, bool is_null, bool boolean_value, std::int64_t bigint_value,
	                    std::string varchar_value);

	CompiledScalarType type;
	bool is_null;
	bool boolean_value;
	std::int64_t bigint_value;
	std::string varchar_value;
};

// Default presence is structural. HasDefault() false never aliases a present
// typed NULL, and consumers may not mutate or replace the retained value.
class CompiledInputDefault {
public:
	CompiledInputDefault(const CompiledInputDefault &) = default;
	CompiledInputDefault(CompiledInputDefault &&) = default;
	CompiledInputDefault &operator=(const CompiledInputDefault &) = delete;
	CompiledInputDefault &operator=(CompiledInputDefault &&) = delete;

	bool HasDefault() const;
	const CompiledScalarValue &Value() const;

private:
	friend class internal::CompiledModelBuilder;

	CompiledInputDefault();
	explicit CompiledInputDefault(CompiledScalarValue value);

	bool has_default;
	std::shared_ptr<const CompiledScalarValue> value;
};

// One ordered relation-origin SQL argument. Nullable controls explicit SQL
// NULL acceptance; it does not make the argument binder-required. Semantics,
// not Query, applies the retained default only to an omitted input.
class CompiledRelationInput {
public:
	CompiledRelationInput(const CompiledRelationInput &) = default;
	CompiledRelationInput(CompiledRelationInput &&) = default;
	CompiledRelationInput &operator=(const CompiledRelationInput &) = delete;
	CompiledRelationInput &operator=(CompiledRelationInput &&) = delete;

	const std::string &Name() const;
	CompiledScalarType Type() const;
	bool Nullable() const;
	const CompiledInputDefault &Default() const;

private:
	friend class internal::CompiledModelBuilder;

	CompiledRelationInput(std::string name, CompiledScalarType type, bool nullable, CompiledInputDefault default_value);

	std::string name;
	CompiledScalarType type;
	bool nullable;
	CompiledInputDefault default_value;
};

enum class CompiledCredentialRequirement { NONE, REQUIRED };

enum class CompiledAuthenticator { NONE, BEARER };

// The native metadata exposes one closed placement, not a caller-supplied
// header name. Runtime owns construction of the eventual header and value.
enum class CompiledCredentialPlacement { NONE, AUTHORIZATION_HEADER };

// One output column. The extractor is evaluated relative to one object selected
// by the response source. A non-nullable declaration makes missing or JSON-null
// extraction fail; it does not claim DuckDB-visible NOT NULL metadata.
struct CompiledColumn {
	// Native 0.7 compatibility construction. Package compilation uses the
	// Connector-private structural constructor so the enum, not this spelling,
	// is authoritative.
	CompiledColumn(std::string name, std::string logical_type, bool nullable, std::string extractor);

	std::string name;
	std::string logical_type;
	bool nullable;
	std::string extractor;

	// Returns retained structural authority without parsing logical_type.
	CompiledScalarType ScalarType() const;

private:
	friend class internal::CompiledModelBuilder;

	CompiledColumn(std::string name, CompiledScalarType type, bool nullable, std::string extractor);

	CompiledScalarType scalar_type;
};

// Closed native predicate vocabulary. This first private pre-1.0 service is
// deliberately narrower than connector-package predicate syntax: it can state
// only equality to the required VARCHAR literal `private`, applied as one REST
// query input. Relational Semantics owns implication, classification use, and
// residual ownership; Connector only supplies validated immutable source facts.
enum class CompiledPredicateOperator { EQUALS };
enum class CompiledPredicateLiteral { VARCHAR_PRIVATE };
enum class CompiledPredicateInputPlacement { REST_QUERY_PARAMETER };
enum class CompiledPredicateAccuracy { EXACT, SUPERSET };

// A reviewed proof profile, not an arbitrary evidence label. Keeping identity
// closed prevents accuracy from being relabeled independently of the operation,
// base domain, occurrence guarantee, and executable encoding that justify it.
// The controlled profile is available only through Connector-owned test
// fixtures; the installed native catalog publishes the GitHub profile alone.
enum class CompiledPredicateProofIdentity {
	GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
	CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY
};

// Compatibility spelling for existing pre-1.0 consumers. ProofIdentity() is
// the preferred accessor because the value identifies a complete proof
// profile rather than unstructured evidence.
using CompiledPredicateEvidence = CompiledPredicateProofIdentity;

// Identifies the duplicate-preserving base occurrence bag against which a
// restriction is proven. A similarly shaped operation on another domain is
// not interchangeable and cannot inherit a mapping's accuracy.
enum class CompiledPredicateBaseDomain {
	GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES,
	CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES
};

// Source-level occurrence guarantee supplied by Connector. Semantics still
// owns implication and three-valued predicate equivalence. Exact occurrence
// preservation additionally excludes extra matching occurrences and changes
// in multiplicity within the declared base domain.
enum class CompiledPredicateOccurrencePreservation {
	PRESERVES_ALL_MATCHING_BASE_OCCURRENCES,
	PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES
};

// Executable request-encoding envelope for a mapping. The current native and
// controlled profiles can emit one positive REST query input only. They do not
// declare compound conjunction, union/disjunction, or complement/negation
// encodings; logical safety alone therefore cannot invent one.
enum class CompiledPredicateEncodingCapability { SINGLE_POSITIVE_REST_QUERY_INPUT };

// Immutable Connector Experience handoff for one conditional request input.
// It carries no SQL text, DuckDB object, credential, mutable request, received
// URL, pagination state, or runtime authority. Construction is restricted to
// the native catalog and Connector-owned non-installable tests.
class CompiledPredicateMapping {
public:
	CompiledPredicateMapping(const CompiledPredicateMapping &) = default;
	CompiledPredicateMapping(CompiledPredicateMapping &&) = default;
	CompiledPredicateMapping &operator=(const CompiledPredicateMapping &) = delete;
	CompiledPredicateMapping &operator=(CompiledPredicateMapping &&) = delete;

	const std::string &ColumnName() const;
	CompiledPredicateOperator Operator() const;
	CompiledPredicateLiteral Literal() const;
	const std::string &OperationName() const;
	CompiledPredicateInputPlacement InputPlacement() const;
	const std::string &RemoteInputName() const;
	const std::string &EncodedRemoteValue() const;
	CompiledPredicateAccuracy Accuracy() const;
	CompiledPredicateProofIdentity ProofIdentity() const;
	CompiledPredicateEvidence Evidence() const;
	CompiledPredicateBaseDomain BaseDomain() const;
	CompiledPredicateOccurrencePreservation OccurrencePreservation() const;
	CompiledPredicateEncodingCapability EncodingCapability() const;
	std::uint64_t MaximumConditionalInputs() const;
	bool SupportsCompoundConjunctionEncoding() const;
	bool SupportsDisjunctionEncoding() const;
	bool SupportsComplementEncoding() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class internal::CompiledModelBuilder;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	CompiledPredicateMapping(std::string column_name, CompiledPredicateOperator predicate_operator,
	                         CompiledPredicateLiteral literal, std::string operation_name,
	                         CompiledPredicateInputPlacement input_placement, std::string remote_input_name,
	                         std::string encoded_remote_value, CompiledPredicateAccuracy accuracy,
	                         CompiledPredicateProofIdentity proof_identity, CompiledPredicateBaseDomain base_domain,
	                         CompiledPredicateOccurrencePreservation occurrence_preservation,
	                         CompiledPredicateEncodingCapability encoding_capability);

	std::string column_name;
	CompiledPredicateOperator predicate_operator;
	CompiledPredicateLiteral literal;
	std::string operation_name;
	CompiledPredicateInputPlacement input_placement;
	std::string remote_input_name;
	std::string encoded_remote_value;
	CompiledPredicateAccuracy accuracy;
	CompiledPredicateProofIdentity proof_identity;
	CompiledPredicateBaseDomain base_domain;
	CompiledPredicateOccurrencePreservation occurrence_preservation;
	CompiledPredicateEncodingCapability encoding_capability;
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
	friend class internal::CompiledModelBuilder;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	// An unpaginated declaration may inherit the connector response ceiling and
	// always has identical page/scan record scopes. The native 0.6.0 relations
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
	const CompiledHttpOrigin *Destination() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class internal::CompiledModelBuilder;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	static CompiledAuthenticationPolicy Anonymous();
	static CompiledAuthenticationPolicy RequiredBearer();

	CompiledAuthenticationPolicy(CompiledCredentialRequirement requirement, std::string logical_credential,
	                             CompiledAuthenticator authenticator, CompiledCredentialPlacement placement,
	                             std::vector<CompiledHttpOrigin> destinations);

	CompiledCredentialRequirement requirement;
	std::string logical_credential;
	CompiledAuthenticator authenticator;
	CompiledCredentialPlacement placement;
	std::vector<CompiledHttpOrigin> destinations;
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
	const std::vector<CompiledRelationInput> &Inputs() const;
	const std::vector<CompiledPredicateMapping> &PredicateMappings() const;
	const std::vector<CompiledOperation> &Operations() const;
	bool HasSingleOperation() const;

	// Backward-compatible convenience for the installed and other singleton
	// relations. Multi-operation consumers must inspect Operations(); this
	// accessor fails instead of choosing an arbitrary eligible operation.
	const CompiledOperation &Operation() const;
	const CompiledAuthenticationPolicy &Authentication() const;
	const CompiledResourceCeilings &ResourceCeilings() const;

	// Stable safe explanation of relation-owned source metadata. It is neither a
	// serialization format nor runtime authority and contains no secret name or
	// credential value.
	std::string Snapshot() const;

private:
	friend CompiledConnector BuildNativeGithubConnector();
	friend class internal::CompiledModelBuilder;
	friend class duckdb_api_test::ConnectorCatalogTestAccess;

	CompiledRelation(std::string name, std::vector<CompiledColumn> columns,
	                 std::vector<CompiledPredicateMapping> predicate_mappings, CompiledOperation operation,
	                 CompiledAuthenticationPolicy authentication, CompiledResourceCeilings resource_ceilings);
	CompiledRelation(std::string name, std::vector<CompiledColumn> columns,
	                 std::vector<CompiledPredicateMapping> predicate_mappings,
	                 std::vector<CompiledOperation> operations, CompiledAuthenticationPolicy authentication,
	                 CompiledResourceCeilings resource_ceilings);
	CompiledRelation(std::string name, std::vector<CompiledColumn> columns, std::vector<CompiledRelationInput> inputs,
	                 std::vector<CompiledPredicateMapping> predicate_mappings,
	                 std::vector<CompiledOperation> operations, CompiledAuthenticationPolicy authentication,
	                 CompiledResourceCeilings resource_ceilings);

	std::string name;
	std::vector<CompiledColumn> columns;
	std::vector<CompiledRelationInput> inputs;
	std::vector<CompiledPredicateMapping> predicate_mappings;
	std::vector<CompiledOperation> operations;
	CompiledAuthenticationPolicy authentication;
	CompiledResourceCeilings resource_ceilings;
};

// Immutable Connector Experience service for the native 0.7.0 relation
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
	friend class internal::CompiledModelBuilder;
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
