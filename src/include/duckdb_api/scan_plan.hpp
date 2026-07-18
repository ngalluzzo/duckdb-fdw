#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api_test {
class ScanPlanTestAccess;
}

namespace duckdb_api {

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

enum class PlannedProtocol { REST };
enum class PlannedHttpMethod { GET };

// Source cardinality is a semantic law over an accepted response, not an
// estimate, decoder budget, or permission to apply a remote/runtime LIMIT.
enum class PlannedCardinality { ZERO_TO_MANY, EXACTLY_ONE_ON_SUCCESS };

enum class PlannedReplaySafety { SAFE };
enum class PlannedUrlScheme { HTTP, HTTPS };

// Runtime consumes this typed source classification directly. It must not
// infer root-versus-array decoding from an extractor or cardinality value.
enum class PlannedResponseSource { JSON_PATH_MANY, ROOT_OBJECT };

// The base domain names the complete row-producing source before DuckDB-owned
// relational operators. A successful root object is one base row; failures
// and zero/multiple-object violations are errors rather than empty results.
enum class BaseDomain { JSON_PATH_RECORDS, SUCCESSFUL_ROOT_OBJECT };

enum class PlannedPredicate { TRUE_FOR_BASE_DOMAIN };
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

struct PlannedQueryParameter {
	std::string name;
	std::string encoded_value;
};

struct PlannedHttpHeader {
	std::string name;
	std::string value;
};

// Exact executable origin copied from Connector's validated typed origin.
// Keeping scheme, host, and port separate prevents consumers from reparsing a
// URL to recover authority.
struct PlannedRestOrigin {
	PlannedUrlScheme scheme;
	std::string host;
	std::uint16_t port;
};

// Complete protocol operation assigned to Remote Runtime. Runtime may reject
// unsupported executable facts, but it must not reclassify source cardinality,
// response shape, or relational ownership.
struct PlannedRestOperation {
	std::string operation_name;
	PlannedProtocol protocol;
	PlannedHttpMethod method;
	PlannedCardinality cardinality;
	PlannedReplaySafety replay_safety;
	PlannedRestOrigin origin;
	std::string path;
	std::vector<PlannedQueryParameter> query_parameters;
	std::vector<PlannedHttpHeader> headers;
	PlannedResponseSource response_source;
	std::string records_extractor;
};

// Planner-owned classification. Runtime consumes these facts without deriving
// them from the protocol request.
struct RelationalOwnership {
	RelationalOwner filter;
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

	bool IsWithinLiveRestBounds() const;
};

class ScanPlan;

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
	friend class duckdb_api_test::ScanPlanTestAccess;
	friend ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

	PlannedAuthenticationObligation();

	PlannedCredentialRequirement requirement;
	std::string logical_credential;
	PlannedAuthenticator authenticator;
	PlannedCredentialPlacement placement;
	bool has_destination;
	PlannedRestOrigin destination;
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
	const PlannedRestOperation &Operation() const;
	const std::vector<PlannedColumn> &OutputColumns() const;

	PlannedPredicate RemotePredicate() const;
	PlannedPredicate ResidualPredicate() const;
	RelationalOwner ResidualOwner() const;
	const RelationalOwnership &Ownership() const;

	RelationalDelegation RemoteOrdering() const;
	RelationalDelegation RuntimeOrdering() const;
	RelationalDelegation RemoteLimit() const;
	RelationalDelegation RemoteOffset() const;
	RelationalDelegation RuntimeLimit() const;
	RelationalDelegation RuntimeOffset() const;

	FeatureState Pagination() const;
	FeatureState Providers() const;
	FeatureState Retry() const;
	FeatureState Cache() const;
	FeatureState Authentication() const;

	const LogicalSecretReference &SecretReference() const;
	const PlannedAuthenticationObligation &AuthenticationObligation() const;
	const NetworkCapability &Network() const;
	const ResourceBudgets &Budgets() const;
	const std::string &ClassificationReason() const;

	// Stable, locale-independent explanation of semantic and executable facts.
	// Logical secret names use Query's safe hex rendering; resolved credential
	// values can never enter this type. The snapshot is neither serialization nor
	// runtime authority.
	std::string Snapshot() const;

private:
	ScanPlan();
	friend class duckdb_api_test::ScanPlanTestAccess;
	friend ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

	std::string connector_name;
	std::string connector_version;
	std::string relation_name;
	std::string source_snapshot;
	BaseDomain domain;
	PlannedRestOperation operation;
	std::vector<PlannedColumn> output_columns;
	PlannedPredicate remote_predicate;
	PlannedPredicate residual_predicate;
	RelationalOwner residual_owner;
	RelationalOwnership ownership;
	RelationalDelegation remote_ordering;
	RelationalDelegation runtime_ordering;
	RelationalDelegation remote_limit;
	RelationalDelegation remote_offset;
	RelationalDelegation runtime_limit;
	RelationalDelegation runtime_offset;
	FeatureState pagination;
	FeatureState providers;
	FeatureState retry;
	FeatureState cache;
	FeatureState authentication;
	LogicalSecretReference secret_reference;
	PlannedAuthenticationObligation authentication_obligation;
	NetworkCapability network;
	ResourceBudgets budgets;
	std::string classification_reason;
};

// Deterministically selects and plans exactly the relation named by request.
// It consumes only immutable Connector and Query team APIs and performs no
// DuckDB callback, secret lookup, network/filesystem/environment access,
// runtime construction, or other I/O.
ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

} // namespace duckdb_api
