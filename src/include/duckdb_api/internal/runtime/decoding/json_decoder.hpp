#pragma once

#include "duckdb_api/execution.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {

struct JsonColumnPlan {
	JsonColumnPlan(std::string output_name_p, std::string json_field_p, ValueKind kind_p, bool nullable_p = false)
	    : output_name(std::move(output_name_p)), kind(kind_p), nullable(nullable_p),
	      json_path(1, std::move(json_field_p)) {
	}
	JsonColumnPlan(std::string output_name_p, std::vector<std::string> json_path_p, ValueKind kind_p,
	               bool nullable_p = false)
	    : output_name(std::move(output_name_p)), kind(kind_p), nullable(nullable_p), json_path(std::move(json_path_p)) {
	}
	std::string output_name;
	ValueKind kind;
	bool nullable;
	std::vector<std::string> json_path;
};

// Selected explicitly from the already validated operation. Records and
// columns retain structural path segments rather than reparsing extractor
// strings. Duplicate or prefix-conflicting selected paths are invalid because
// they would give one JSON value more than one schema owner. The decoder does
// not derive response shape or relational cardinality.
enum class JsonResponseSource { JSON_PATH_MANY, ROOT_ARRAY, ROOT_OBJECT };

struct JsonDecodePlan {
	JsonResponseSource response_source;
	std::vector<std::string> records_path;
	std::vector<JsonColumnPlan> columns;
	uint64_t max_records;
	uint64_t max_string_bytes;
	uint64_t max_json_nesting;
	uint64_t max_decoded_memory_bytes;
	std::chrono::steady_clock::time_point deadline;
};

struct DecodedJsonPage {
	std::vector<TypedRow> rows;
	// Retained storage owned by rows, including vector capacity, typed-value
	// capacity, and owned VARCHAR capacity. The executor adds normalized
	// response-metadata storage before committing the page budget.
	uint64_t retained_memory_bytes;
};

// Strictly decodes one already bounded JSON document. The decoder validates the
// complete document, requires each declared non-null field exactly once,
// retains JSON numeric spelling through BIGINT conversion, and checkpoints
// cancellation/deadline and every decode budget. ROOT_OBJECT produces exactly
// one record from the successful response object. It has no request authority.
DecodedJsonPage DecodeJsonPage(const std::string &body, const JsonDecodePlan &plan, ExecutionControl &control);

// Compatibility convenience for existing one-response consumers and focused
// decoder tests that do not need retained-memory accounting.
std::vector<TypedRow> DecodeJsonRows(const std::string &body, const JsonDecodePlan &plan, ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api
