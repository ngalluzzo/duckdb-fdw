#pragma once

#include "duckdb_api/execution.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

struct JsonColumnPlan {
	std::string output_name;
	std::string json_field;
	ValueKind kind;
};

struct JsonDecodePlan {
	std::string records_field;
	std::vector<JsonColumnPlan> columns;
	uint64_t max_records;
	uint64_t max_string_bytes;
	uint64_t max_json_nesting;
	uint64_t max_decoded_memory_bytes;
	std::chrono::steady_clock::time_point deadline;
};

// Strictly decodes one already bounded JSON document. The decoder validates the
// complete document, requires each declared non-null field exactly once,
// retains JSON numeric spelling through BIGINT conversion, and checkpoints
// cancellation/deadline and every decode budget. It has no request authority.
std::vector<TypedRow> DecodeJsonRows(const std::string &body, const JsonDecodePlan &plan, ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
