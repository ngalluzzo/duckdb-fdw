#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {

static const uint64_t MAX_FIXTURE_BYTES = 4096;
static const uint64_t MAX_DECODED_RECORDS = 32;
static const uint64_t MAX_NAME_BYTES = 128;
static const uint64_t MAX_JSON_NESTING = 16;
static const uint64_t OUTPUT_BATCH_ROWS = 2;
static const uint64_t MAX_EXECUTION_MILLISECONDS = 5000;

// Host-enforced ceilings recorded in the immutable plan. The internal example
// connector cannot widen them; Remote Runtime owns enforcement and exhaustion.
struct ResourceBudgets {
	uint64_t fixture_bytes;
	uint64_t decoded_records;
	uint64_t name_bytes;
	uint64_t json_nesting;
	uint64_t batch_rows;
	uint64_t wall_milliseconds;
	uint64_t concurrency;

	bool IsPreviewBudget() const;
};

// Complete immutable handoff from Relational Semantics to Remote Runtime for
// the accepted preview. TRUE residuals and empty runtime ordering/bounds mean
// DuckDB retains all filtering, ordering, limit, and offset work. Planning is
// deterministic and performs no I/O.
struct ScanPlan {
	std::string operation_name;
	std::string executor_name;
	std::string method;
	std::string path;
	std::string extractor;
	std::string fixture_digest;
	std::vector<std::string> output_columns;
	std::string remote_predicate;
	std::string runtime_residual_predicate;
	std::vector<std::string> remote_ordering;
	std::vector<std::string> runtime_ordering;
	bool has_remote_limit;
	bool has_remote_offset;
	bool has_runtime_limit;
	bool has_runtime_offset;
	std::vector<std::string> duckdb_owned_operations;
	bool pagination_enabled;
	bool providers_enabled;
	bool retry_enabled;
	bool cache_enabled;
	bool network_enabled;
	ResourceBudgets budgets;

	std::string Snapshot() const;
};

ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

} // namespace duckdb_api
