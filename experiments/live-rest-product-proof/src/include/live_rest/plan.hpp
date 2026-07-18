#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace live_rest {

enum class ColumnType { BIGINT, VARCHAR, BOOLEAN };

// One non-null output column in the trial's fixed GitHub users relation.
// `json_field` is evaluated on an object from `response_array_field`; the
// decoder owns strict conversion to `type` and must fail on missing, null, or
// incompatible values.
struct Column {
	std::string name;
	ColumnType type;
	std::string json_field;
};

// Complete offline handoff from Relational Semantics to the trial runtime.
// BuildLiveScanPlan is the sole constructor used by the experiment. Callers
// treat the returned value as immutable once execution begins.
//
// This bounded profile plans one full scan and exposes no predicate, ordering,
// limit, or offset capability. The remote predicate and runtime residual are
// therefore TRUE, while DuckDB retains any relational operators above the
// scan. Runtime execution owns only the fixed GET and strict row production;
// it must not infer or reclassify relational meaning from the URL or response.
struct LiveScanPlan {
	std::string connector_name;
	std::string relation_name;
	std::string method;
	std::string url;
	std::string response_array_field;
	std::vector<Column> columns;
	uint64_t max_response_bytes;
	uint64_t max_records;
	uint64_t max_string_bytes;
	uint64_t wall_milliseconds;
	uint64_t batch_rows;
	bool redirects_enabled;
	bool retries_enabled;
	bool authentication_enabled;
	bool pagination_enabled;

	// Stable, safe explanation of every execution and semantic invariant in
	// this proof plan. It contains no response data or network-derived state.
	std::string Snapshot() const;
};

// Constructs the fixed plan without filesystem or network I/O. `authority`
// must be exactly https://api.github.com or a loopback test authority of the
// form http://127.0.0.1:<decimal port>, where the port is in [1, 65535].
LiveScanPlan BuildLiveScanPlan(const std::string &authority);

} // namespace live_rest
