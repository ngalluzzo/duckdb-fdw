#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {

static const uint64_t HOST_MAX_REQUEST_ATTEMPTS = 1;
static const uint64_t HOST_MAX_RESPONSE_BYTES = 65536;
static const uint64_t HOST_MAX_HEADER_BYTES = 16384;
static const uint64_t HOST_MAX_DECOMPRESSED_BYTES = 65536;
static const uint64_t HOST_MAX_DECODED_RECORDS = 32;
// `per_page=3` defines this relation's complete base domain. Connector
// metadata may narrow the decoded-record ceiling, but cannot widen it past the
// source definition even when the host-level decoder could accept more rows.
static const uint64_t LIVE_RELATION_MAX_RECORDS = 3;
static const uint64_t HOST_MAX_EXTRACTED_STRING_BYTES = 256;
static const uint64_t HOST_MAX_JSON_NESTING = 16;
static const uint64_t HOST_MAX_DECODED_MEMORY_BYTES = 131072;
static const uint64_t OUTPUT_BATCH_ROWS = 2;
static const uint64_t MAX_EXECUTION_MILLISECONDS = 5000;
static const uint64_t HOST_MAX_CONCURRENCY = 1;

enum class PlannedProtocol { REST };
enum class PlannedHttpMethod { GET };
enum class PlannedCardinality { ZERO_TO_MANY };
enum class PlannedReplaySafety { SAFE };
enum class PlannedUrlScheme { HTTP, HTTPS };

// The fixed request defines the complete base relation for this preview. Its
// q and per_page fields are source constants, not SQL predicate or limit
// pushdown.
enum class BaseDomain { SINGLE_RESPONSE_PAGE };

enum class PlannedPredicate { TRUE_FOR_BASE_DOMAIN };
enum class RelationalOwner { DUCKDB };
enum class RelationalDelegation { NONE };
enum class FeatureState { DISABLED };

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

// An exact executable origin copied from Connector's validated typed origin.
// Keeping scheme, host, and port separate prevents Runtime from reparsing a
// URL to recover authority.
struct PlannedRestOrigin {
	PlannedUrlScheme scheme;
	std::string host;
	std::uint16_t port;
};

// The complete protocol operation assigned to Remote Runtime. Runtime may
// reject facts outside its executable capability, but it must not derive
// relational ownership from these request fields.
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
	std::string records_extractor;
};

// Planner-owned classification. The runtime consumes none of these fields as
// authorization and never recomputes them from the request or response.
struct RelationalOwnership {
	RelationalOwner filter;
	RelationalOwner ordering;
	RelationalOwner limit;
	RelationalOwner offset;
};

// Exact destination capability carried by this plan. The executor intersects
// it with its public or private-controlled host authority and may only narrow
// it before opening the transport.
struct NetworkCapability {
	std::vector<std::string> allowed_schemes;
	std::vector<std::string> allowed_hosts;
	bool redirects_enabled;
	bool private_addresses_enabled;
	bool link_local_addresses_enabled;
	bool loopback_addresses_enabled;
};

// Effective connector/host resource intersection. The decoded-record field is
// additionally bounded by the fixed relation domain. Remote Runtime owns
// enforcement and reports exhaustion without widening any field.
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

	// True for any nonzero effective envelope at or below host caps and the
	// fixed relation-domain ceiling. Request and concurrency remain exactly one.
	bool IsWithinLiveRestBounds() const;
};

// Complete immutable handoff from Relational Semantics to Query Experience
// and Remote Runtime. Only the planner can construct this value. Consumers may
// copy it into immutable scan state and read typed accessors, but cannot mutate
// or partially initialize its meaning.
class ScanPlan {
public:
	ScanPlan(const ScanPlan &) = default;
	ScanPlan(ScanPlan &&) = default;
	ScanPlan &operator=(const ScanPlan &) = delete;
	ScanPlan &operator=(ScanPlan &&) = delete;

	const std::string &ConnectorName() const;
	const std::string &ConnectorVersion() const;
	const std::string &RelationName() const;

	// Provenance and explanation only. Runtime authorization must validate the
	// typed executable facts rather than compare or rebuild this snapshot.
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

	const NetworkCapability &Network() const;
	const ResourceBudgets &Budgets() const;
	const std::string &ClassificationReason() const;

	// Stable safe explanation of identity, relational ownership, executable
	// facts, and applied bounds. It is not a serialization format or a source of
	// runtime authority.
	std::string Snapshot() const;

private:
	ScanPlan();
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
	NetworkCapability network;
	ResourceBudgets budgets;
	std::string classification_reason;
};

// Deterministically plans one native fixed REST relation. The function uses
// only immutable connector/request values and constants above; it performs no
// network, filesystem, environment, runtime, or DuckDB callback work.
ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

} // namespace duckdb_api
