#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {

// Identifies how the immutable metadata entered the product. The 0.3.0 native
// snapshot is repository-owned product metadata, not compiled package syntax
// and not a public connector-authoring or native ABI commitment.
enum class CompiledConnectorOrigin { NATIVE_PRODUCT_METADATA };

enum class CompiledOperationCardinality { ZERO_TO_MANY };

enum class CompiledProtocol { REST };

enum class CompiledHttpMethod { GET };

enum class CompiledReplaySafety { SAFE };

// A closed transport scheme carried as data rather than recovered by parsing a
// URL. HTTP exists for private controlled-service compositions; the installed
// native connector below selects HTTPS exclusively.
enum class CompiledUrlScheme { HTTP, HTTPS };

// One required output column in the bounded GitHub response page. The
// extractor is evaluated relative to one object selected by records_extractor.
// A non-nullable column fails schema conversion when extraction is missing or
// yields JSON null; it does not claim DuckDB-visible NOT NULL metadata.
struct CompiledColumn {
	std::string name;
	std::string logical_type;
	bool nullable;
	std::string extractor;
};

// One already-encoded fixed query field. Ordering and encoded_value are part
// of the request identity; consumers must not reinterpret this as DuckDB
// predicate or limit pushdown.
struct CompiledQueryParameter {
	std::string name;
	std::string encoded_value;
};

// One non-sensitive fixed request header. Header ordering is stable so request
// planning and deterministic controlled-service oracles share one identity.
struct CompiledHttpHeader {
	std::string name;
	std::string value;
};

// An exact lower-case DNS name or IPv4 literal. Construction rejects URL
// syntax, user information, embedded ports, empty labels, and non-canonical
// labels, so consumers cannot receive a path, query, or fragment disguised as
// a host component.
class CompiledRestHost {
public:
	explicit CompiledRestHost(std::string value);

	const std::string &Value() const;

private:
	std::string value;
};

// The authority portion of a REST request. There is deliberately no
// user-information, path, query, or fragment field; consumers compare these
// fields directly and never parse an origin string.
struct CompiledRestOrigin {
	CompiledUrlScheme scheme;
	CompiledRestHost host;
	std::uint16_t port;
};

// Structural REST request metadata. The typed origin, operation path, and
// ordered query parameters stay separate so the planner can construct an
// inspectable request without a caller-selected or prejoined URL. No field
// carries credentials.
struct CompiledRestRequest {
	CompiledRestOrigin origin;
	std::string path;
	std::vector<CompiledQueryParameter> query_parameters;
	std::vector<CompiledHttpHeader> headers;
};

// The sole base-row operation for the bounded relation. This declaration
// describes eligibility and capability; Relational Semantics owns operation
// selection and Remote Runtime owns enforcement of the resulting ScanPlan.
struct CompiledOperation {
	std::string name;
	bool fallback;
	CompiledOperationCardinality cardinality;
	CompiledProtocol protocol;
	CompiledHttpMethod method;
	CompiledReplaySafety replay_safety;
	bool retry_enabled;
	bool authentication_enabled;
	bool pagination_enabled;
	CompiledRestRequest request;
	std::string records_extractor;
};

// Connector policy narrows host authority; it never grants network access.
// Remote Runtime intersects these declarations with host policy and enforces
// the effective destination and response ceiling after planning.
struct CompiledNetworkPolicy {
	std::vector<std::string> allowed_schemes;
	std::vector<std::string> allowed_hosts;
	bool redirects_enabled;
	bool private_addresses_enabled;
	bool link_local_addresses_enabled;
	bool loopback_addresses_enabled;
	uint64_t max_response_bytes;
};

// Connector-owned extraction ceilings. Host-owned request, memory, batch,
// nesting, wall-time, and concurrency ceilings belong to the ScanPlan.
struct CompiledResourceCeilings {
	uint64_t max_records;
	uint64_t max_extracted_string_bytes;
};

// Immutable Connector Experience handoff for the one native 0.3.0 relation.
// Build it once during product composition and retain it by value for active
// scans. Consumers may rely on every value and ordering remaining stable for
// the snapshot lifetime without learning YAML, runtime, or DuckDB internals.
struct CompiledConnector {
	CompiledConnectorOrigin origin;
	std::string connector_name;
	std::string version;
	std::string relation_name;
	std::vector<CompiledColumn> columns;
	CompiledOperation operation;
	CompiledNetworkPolicy network_policy;
	CompiledResourceCeilings resource_ceilings;

	// Produces the canonical, source-explainable native metadata snapshot. `!`
	// after a logical type means extraction is required and non-null.
	std::string Snapshot() const;
};

// Constructs the exact RFC 0005 snapshot deterministically without I/O,
// environment access, package parsing, runtime construction, or DuckDB types.
CompiledConnector BuildNativeGithubConnector();

} // namespace duckdb_api
