#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

struct GraphqlDecodeLimits {
	uint64_t max_records;
	uint64_t max_string_bytes;
	uint64_t max_json_nesting;
	uint64_t max_decoded_memory_bytes;
	std::chrono::steady_clock::time_point deadline;
};

struct DecodedGraphqlPage {
	std::vector<TypedRow> rows;
	bool has_next;
	std::string end_cursor;
	// Retained rows and cursor storage. The executor adds any normalized
	// transport metadata before committing the page-memory allowance.
	uint64_t retained_memory_bytes;
};

// Strictly validates and decodes one complete GraphQL response envelope. A
// nonempty top-level errors array always becomes REMOTE_PROTOCOL before data
// can be published, without inspecting or copying remote error contents.
DecodedGraphqlPage DecodeGraphqlResponse(const std::string &body, const AdmittedGraphqlRequestProfile &profile,
                                         const GraphqlDecodeLimits &limits, ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
