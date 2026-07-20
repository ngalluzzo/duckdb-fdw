#pragma once

#include "duckdb_api/planned_protocol_operation.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api_test {
class ScanPlanFixtureBuilder;
class ScanPlanTestAccess;
} // namespace duckdb_api_test

namespace duckdb_api {

class ScanPlanBuilder;

static const uint64_t HOST_MAX_REQUEST_ATTEMPTS = 1;
static const uint64_t HOST_MAX_RESPONSE_BYTES = 65536;
static const uint64_t HOST_MAX_HEADER_BYTES = 16384;
static const uint64_t HOST_MAX_DECOMPRESSED_BYTES = 65536;
static const uint64_t HOST_MAX_DECODED_RECORDS = 32;
static const uint64_t HOST_MAX_EXTRACTED_STRING_BYTES = 256;
static const uint64_t HOST_MAX_JSON_NESTING = 16;
static const uint64_t HOST_MAX_DECODED_MEMORY_BYTES = 131072;
static const uint64_t OUTPUT_BATCH_ROWS = 2;
static const uint64_t MAX_EXECUTION_MILLISECONDS = 5000;
static const uint64_t HOST_MAX_CONCURRENCY = 1;
static const uint64_t HOST_MAX_SERIALIZED_REQUEST_BODY_BYTES = 16 * 1024;

// RFC 0007's paginated execution profile is separate from the accepted
// single-response profile above. Raising these ceilings must not widen either
// existing relation's effective plan.
static const uint64_t PAGINATION_MAX_REQUEST_ATTEMPTS_PER_PAGE = 1;
static const uint64_t PAGINATION_MAX_REQUEST_ATTEMPTS_PER_SCAN = 32;
static const uint64_t PAGINATION_MAX_PAGES_PER_SCAN = 32;
static const uint64_t PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE = 8 * 1024 * 1024;
static const uint64_t PAGINATION_MAX_RESPONSE_BYTES_PER_SCAN = 64 * 1024 * 1024;
static const uint64_t PAGINATION_MAX_HEADER_BYTES_PER_PAGE = 16 * 1024;
static const uint64_t PAGINATION_MAX_HEADER_BYTES_PER_SCAN = 512 * 1024;
static const uint64_t PAGINATION_MAX_DECOMPRESSED_BYTES_PER_PAGE = 8 * 1024 * 1024;
static const uint64_t PAGINATION_MAX_DECOMPRESSED_BYTES_PER_SCAN = 64 * 1024 * 1024;
static const uint64_t PAGINATION_MAX_DECODED_RECORDS_PER_PAGE = 100;
static const uint64_t PAGINATION_MAX_DECODED_RECORDS_PER_SCAN = 3200;
static const uint64_t PAGINATION_MAX_EXTRACTED_STRING_BYTES = 512;
static const uint64_t PAGINATION_MAX_JSON_NESTING = 16;
static const uint64_t PAGINATION_MAX_DECODED_MEMORY_BYTES = 2 * 1024 * 1024;
static const uint64_t PAGINATION_OUTPUT_BATCH_ROWS = 64;
static const uint64_t PAGINATION_MAX_EXECUTION_MILLISECONDS = 30000;
static const uint64_t PAGINATION_MAX_CONCURRENCY = 1;
static const uint64_t PAGINATION_MAX_SERIALIZED_REQUEST_BODY_BYTES_PER_SCAN = 256 * 1024;

// The base domain names the complete row-producing source before DuckDB-owned
// relational operators. ROOT_ARRAY_RECORDS is admitted only for the controlled
// complete-array proof profile. Each PAGINATED_* domain is the
// duplicate-preserving bag from every accepted page, not an ordered or
// snapshot-isolated relation. A successful root object is one base row;
// failures and zero/multiple-object violations are errors rather than empty
// results.
enum class BaseDomain {
	JSON_PATH_RECORDS,
	ROOT_ARRAY_RECORDS,
	PAGINATED_JSON_PATH_RECORDS,
	PAGINATED_ROOT_ARRAY_RECORDS,
	GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES,
	SUCCESSFUL_ROOT_OBJECT
};

// Closed executable predicate vocabulary for the accepted native profile. A
// visibility predicate is meaningful only for the bound required VARCHAR
// response field admitted by the planner. COMPLETE_DUCKDB_FILTER records that
// the retained residual is larger than the typed candidate without carrying an
// expression or SQL text. Runtime must never infer meaning from explanation or
// output values.
enum class PlannedPredicate { TRUE_FOR_BASE_DOMAIN, VISIBILITY_EQUALS_PRIVATE, COMPLETE_DUCKDB_FILTER };

// Accuracy describes the relationship between the complete DuckDB predicate D
// and emitted remote restriction R. It never transfers residual ownership or
// bound authority in the native profile.
enum class RemotePredicateAccuracy { UNSUPPORTED, SUPERSET, EXACT };

// Successful semantic outcomes remain distinct even when Unsupported and
// Ambiguous both execute the unrestricted base operation. Invalid contracts do
// not produce a ScanPlan; the separate planner service returns a typed error.
enum class PredicateDecisionCategory { EXACT, SUPERSET, UNSUPPORTED, AMBIGUOUS };

// Structured safe reason consumed by Query explanation. Consumers may render
// this value but must not parse ClassificationReason() or derive authority from
// either representation.
enum class PredicateDecisionReason {
	NO_REMOTE_CANDIDATE,
	SELECTED_EXACT_MAPPING,
	SELECTED_SUPERSET_MAPPING,
	STRUCTURE_UNSUPPORTED,
	CAPABILITY_UNAVAILABLE,
	MAPPING_UNAVAILABLE,
	DISJUNCTION_ENCODING_UNAVAILABLE,
	COMPLEMENT_ENCODING_UNAVAILABLE,
	AMBIGUOUS_CONDITIONAL_INPUT
};

// The sole predicate-derived execution authority. Runtime consumes this typed
// value with the base operation and pagination target; no raw query parameter,
// snapshot, or Connector declaration is an alternative authority.
enum class PlannedConditionalInput { NONE, VISIBILITY_PRIVATE };
enum class RelationalOwner { DUCKDB };
enum class RelationalDelegation { NONE };
enum class FeatureState { DISABLED, ENABLED };

enum class PlannedCredentialRequirement { NONE, REQUIRED };
enum class PlannedAuthenticator { NONE, BEARER };
enum class PlannedCredentialPlacement { NONE, AUTHORIZATION_HEADER };

struct PlannedColumn {
	std::string name;
	std::string logical_type;
	bool nullable;
	std::string extractor;
};

// Planner-owned classification. Runtime consumes these facts without deriving
// them from the protocol request.
struct RelationalOwnership {
	RelationalOwner filter;
	RelationalOwner projection;
	RelationalOwner ordering;
	RelationalOwner limit;
	RelationalOwner offset;
};

// Selected-operation network capability after Semantics narrows Connector
// policy to the exact operation origin. Runtime intersects it with its own host
// authority and may only narrow it further.
struct NetworkCapability {
	std::vector<std::string> allowed_schemes;
	std::vector<std::string> allowed_hosts;
	bool redirects_enabled;
	bool private_addresses_enabled;
	bool link_local_addresses_enabled;
	bool loopback_addresses_enabled;
};

// Effective Connector/host resource intersection. The decoded-record ceiling
// remains separate from PlannedCardinality: a value of one does not implement
// EXACTLY_ONE_ON_SUCCESS, and a larger zero-to-many ceiling is not a row
// estimate. Runtime owns enforcement and cannot widen any field.
struct ResourceBudgets {
	uint64_t request_attempts;
	uint64_t response_bytes;
	uint64_t header_bytes;
	uint64_t decompressed_bytes;
	uint64_t decoded_records;
	uint64_t extracted_string_bytes;
	uint64_t json_nesting;
	uint64_t decoded_memory_bytes;
	uint64_t batch_rows;
	uint64_t wall_milliseconds;
	uint64_t concurrency;
	// Zero for bodyless REST GET operations. A nonzero value is an outbound
	// serialized-body ceiling, never row, limit, or retry authority.
	uint64_t serialized_request_body_bytes;

	bool IsWithinLiveRestBounds() const;
	bool IsWithinPaginatedPageBounds() const;
};

// Aggregate bounds over one sequential paginated scan. Request attempts count
// distinct page replay units; they are not retry authority. Memory, batch, and
// concurrency fields are retained-at-once ceilings rather than additive totals.
struct ScanResourceBudgets {
	uint64_t request_attempts;
	uint64_t pages;
	uint64_t response_bytes;
	uint64_t header_bytes;
	uint64_t decompressed_bytes;
	uint64_t decoded_records;
	uint64_t extracted_string_bytes;
	uint64_t json_nesting;
	uint64_t decoded_memory_bytes;
	uint64_t batch_rows;
	uint64_t wall_milliseconds;
	uint64_t concurrency;
	uint64_t serialized_request_body_bytes;

	bool IsWithinPaginatedScanBounds() const;
};

enum class PlannedPaginationStrategy { DISABLED, LINK_HEADER, GRAPHQL_CURSOR };
enum class PlannedPageDependency { SEQUENTIAL };
enum class PlannedPageConsistency { MUTABLE };
enum class PlannedLinkRelation { NEXT };
enum class PlannedContinuationTargetScope { EXACT_OPERATION_ORIGIN_AND_PATH };

class ScanPlan;

// Immutable target and typed transition facts. Runtime validates received
// metadata against this profile and reconstructs from the planned operation;
// this value never contains a received URL or mutable page state.
struct PlannedPaginationTarget {
	PlannedRestOrigin origin;
	std::string path;
	std::string page_size_parameter;
	uint64_t page_size;
	std::string page_number_parameter;
	uint64_t first_page;
	uint64_t page_increment;
};

// Relational Semantics' closed pagination handoff. Disabled pagination has no
// accessible payload. The only enabled profile is sequential mutable Link
// traversal with explicit page and scan budgets. The value is immutable after
// planner construction, contains no response/credential state, and is a
// private pre-1.0 team API rather than a public native ABI.
class PaginationPlan {
public:
	PaginationPlan(const PaginationPlan &) = default;
	PaginationPlan(PaginationPlan &&) = default;
	PaginationPlan &operator=(const PaginationPlan &) = delete;
	PaginationPlan &operator=(PaginationPlan &&) = delete;

	PlannedPaginationStrategy Strategy() const;
	PlannedPageDependency Dependency() const;
	PlannedPageConsistency Consistency() const;
	PlannedLinkRelation LinkRelation() const;
	PlannedContinuationTargetScope TargetScope() const;
	bool SupportsTotal() const;
	bool SupportsResume() const;
	const PlannedPaginationTarget &Target() const;
	const PlannedGraphqlCursor &GraphqlCursor() const;
	const ResourceBudgets &PageBudgets() const;
	const ScanResourceBudgets &ScanBudgets() const;

private:
	friend class ScanPlan;
	friend class duckdb_api_test::ScanPlanFixtureBuilder;
	friend class duckdb_api_test::ScanPlanTestAccess;
	friend class ScanPlanBuilder;

	PaginationPlan();
	void RequireLinkHeader() const;

	PlannedPaginationStrategy strategy;
	PlannedPageDependency dependency;
	PlannedPageConsistency consistency;
	PlannedLinkRelation link_relation;
	PlannedContinuationTargetScope target_scope;
	bool supports_total;
	bool supports_resume;
	PlannedPaginationTarget target;
	PlannedGraphqlCursor graphql_cursor;
	ResourceBudgets page_budgets;
	ScanResourceBudgets scan_budgets;
};

// Relational Semantics' normalized authorization obligation. It deliberately
// does not expose Connector policy representation, a DuckDB secret fact, or a
// Runtime capability. The optional destination is present only when a logical
// credential is required and contains no credential bytes.
class PlannedAuthenticationObligation {
public:
	PlannedAuthenticationObligation(const PlannedAuthenticationObligation &) = default;
	PlannedAuthenticationObligation(PlannedAuthenticationObligation &&) = default;
	PlannedAuthenticationObligation &operator=(const PlannedAuthenticationObligation &) = delete;
	PlannedAuthenticationObligation &operator=(PlannedAuthenticationObligation &&) = delete;

	PlannedCredentialRequirement Requirement() const;
	const std::string &LogicalCredential() const;
	PlannedAuthenticator Authenticator() const;
	PlannedCredentialPlacement Placement() const;
	const PlannedRestOrigin *Destination() const;

private:
	friend class ScanPlan;
	friend class duckdb_api_test::ScanPlanFixtureBuilder;
	friend class duckdb_api_test::ScanPlanTestAccess;
	friend class ScanPlanBuilder;

	PlannedAuthenticationObligation();

	PlannedCredentialRequirement requirement;
	std::string logical_credential;
	PlannedAuthenticator authenticator;
	PlannedCredentialPlacement placement;
	bool has_destination;
	PlannedRestOrigin destination;
};

// Relational Semantics' normalized logical-secret selector. Planning copies
// only the exact DuckDB catalog name supplied by Query; the value contains no
// secret bytes, catalog/provider handles, storage facts, or execution
// authority. Only the planner and closed Semantics fixtures may construct it.
class PlannedSecretReference {
public:
	PlannedSecretReference(const PlannedSecretReference &) = default;
	PlannedSecretReference(PlannedSecretReference &&) = default;
	PlannedSecretReference &operator=(const PlannedSecretReference &) = default;
	PlannedSecretReference &operator=(PlannedSecretReference &&) = default;

	bool IsPresent() const noexcept;
	const std::string &Name() const;

	// Stable safe rendering for plan explanation. This is not encryption,
	// hashing, or a secrecy boundary.
	std::string Snapshot() const;

private:
	PlannedSecretReference();
	explicit PlannedSecretReference(std::string exact_duckdb_secret_name);

	friend class ScanPlan;
	friend class ScanPlanBuilder;
	friend class duckdb_api_test::ScanPlanFixtureBuilder;
	friend class duckdb_api_test::ScanPlanTestAccess;

	std::string exact_duckdb_secret_name;
};

// Complete immutable Semantics handoff to Query and Remote Runtime. Only the
// side-effect-free planner can construct it. Copies may be retained by prepared
// bind state and concurrent executions; no plan field is mutable after
// construction. The logical reference identifies the DuckDB secret to resolve
// at each execution but carries no secret value, provider, handle, or authority.
// This private pre-1.0 team API is not a public native ABI.
class ScanPlan {
public:
	ScanPlan(const ScanPlan &) = default;
	ScanPlan(ScanPlan &&) = default;
	ScanPlan &operator=(const ScanPlan &) = delete;
	ScanPlan &operator=(ScanPlan &&) = delete;

	const std::string &ConnectorName() const;
	const std::string &ConnectorVersion() const;
	const std::string &RelationName() const;

	// Selected relation provenance for safe explanation only. Runtime authority
	// comes from typed operation, network, budget, and obligation fields.
	const std::string &SourceSnapshot() const;

	BaseDomain Domain() const;
	const PlannedProtocolOperation &Operation() const;
	const std::vector<PlannedColumn> &OutputColumns() const;

	PlannedPredicate RemotePredicate() const;
	RemotePredicateAccuracy RemoteAccuracy() const;
	PlannedPredicate ResidualPredicate() const;
	RelationalOwner ResidualOwner() const;
	PlannedConditionalInput ConditionalInput() const;
	PredicateDecisionCategory PredicateCategory() const;
	PredicateDecisionReason PredicateReason() const;
	const RelationalOwnership &Ownership() const;

	RelationalDelegation RemoteOrdering() const;
	RelationalDelegation RuntimeOrdering() const;
	RelationalDelegation RemoteLimit() const;
	RelationalDelegation RemoteOffset() const;
	RelationalDelegation RuntimeLimit() const;
	RelationalDelegation RuntimeOffset() const;

	const PaginationPlan &Pagination() const;
	FeatureState Providers() const;
	FeatureState Retry() const;
	FeatureState Cache() const;
	FeatureState Authentication() const;

	const PlannedSecretReference &SecretReference() const;
	const PlannedAuthenticationObligation &AuthenticationObligation() const;
	const NetworkCapability &Network() const;
	const ResourceBudgets &Budgets() const;
	const std::string &ClassificationReason() const;

	// Stable, locale-independent explanation of semantic and executable facts.
	// Logical secret names use Semantics' safe hex rendering; resolved credential
	// values can never enter this type. The snapshot is neither serialization nor
	// runtime authority.
	std::string Snapshot() const;

private:
	ScanPlan();
	friend class duckdb_api_test::ScanPlanFixtureBuilder;
	friend class duckdb_api_test::ScanPlanTestAccess;
	friend class ScanPlanBuilder;

	std::string connector_name;
	std::string connector_version;
	std::string relation_name;
	std::string source_snapshot;
	BaseDomain domain;
	std::shared_ptr<const PlannedProtocolOperation> operation;
	std::vector<PlannedColumn> output_columns;
	PlannedPredicate remote_predicate;
	RemotePredicateAccuracy remote_accuracy;
	PlannedPredicate residual_predicate;
	RelationalOwner residual_owner;
	PlannedConditionalInput conditional_input;
	PredicateDecisionCategory predicate_category;
	PredicateDecisionReason predicate_reason;
	RelationalOwnership ownership;
	RelationalDelegation remote_ordering;
	RelationalDelegation runtime_ordering;
	RelationalDelegation remote_limit;
	RelationalDelegation remote_offset;
	RelationalDelegation runtime_limit;
	RelationalDelegation runtime_offset;
	PaginationPlan pagination;
	FeatureState providers;
	FeatureState retry;
	FeatureState cache;
	FeatureState authentication;
	PlannedSecretReference secret_reference;
	PlannedAuthenticationObligation authentication_obligation;
	NetworkCapability network;
	ResourceBudgets budgets;
	std::string classification_reason;
};

} // namespace duckdb_api
