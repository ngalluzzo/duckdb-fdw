#pragma once

#include "duckdb_api/connector.hpp"

#include <string>
#include <vector>

namespace duckdb_api {

// Capabilities actually exposed by the accepted DuckDB 1.5.4 native adapter.
// A false value is a conservative absence, never permission to reconstruct SQL
// text or infer unavailable query structure.
struct AdapterCapabilities {
	bool projection;
	bool filter;
	bool ordering;
	bool limit;
	bool offset;
	bool progress;
	bool cancellation;
	bool secrets;

	bool IsConservativePreview() const;
};

// Protocol-neutral input from Query Experience to Relational Semantics.
// Construction is deterministic, side-effect free, and grants no I/O
// authority. DuckDB retains every unavailable relational operation.
struct ScanRequest {
	std::string connector_name;
	std::string relation_name;
	std::vector<std::string> explicit_inputs;
	std::vector<std::string> projected_columns;
	std::string predicate;
	std::vector<std::string> orderings;
	bool has_limit;
	bool has_offset;
	AdapterCapabilities capabilities;

	std::string Snapshot() const;
};

// Builds the accepted native adapter request from immutable connector
// metadata. The current DuckDB profile exposes no relational scan metadata, so
// the request carries the complete declared projection and conservative
// unavailable values. This function performs no validation, I/O, environment
// access, or SQL reconstruction; Relational Semantics validates the request
// against the connector when constructing ScanPlan.
ScanRequest BuildConservativeScanRequest(const CompiledConnector &connector);

} // namespace duckdb_api
