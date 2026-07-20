#include "duckdb_api/internal/runtime/decoding/graphql_response_decoder.hpp"
#include "duckdb_api/internal/runtime/decoding/strict_json_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

void Checkpoint(ExecutionControl &control, std::chrono::steady_clock::time_point deadline) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
	if (std::chrono::steady_clock::now() >= deadline) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
	}
}

bool CheckedAdd(uint64_t left, uint64_t right, uint64_t &result) noexcept {
	if (right > std::numeric_limits<uint64_t>::max() - left) {
		return false;
	}
	result = left + right;
	return true;
}

bool CheckedMultiply(uint64_t left, uint64_t right, uint64_t &result) noexcept {
	if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left) {
		return false;
	}
	result = left * right;
	return true;
}

class GraphqlJsonParser : private StrictJsonReader {
public:
	GraphqlJsonParser(const std::string &input_p, const AdmittedGraphqlRequestProfile &profile_p,
	                  const GraphqlDecodeLimits &limits_p, ExecutionControl &control_p)
	    : StrictJsonReader(input_p, limits_p.max_json_nesting, limits_p.deadline, control_p), profile(profile_p),
	      limits(limits_p), decoded_memory(0), pending_string_memory(0), page_has_next(false), page_info_seen(false) {
	}

	DecodedGraphqlPage Parse() {
		ValidateDocument();
		CheckRemoteErrors();
		Reset();
		SkipWhitespace();
		if (Peek() != '{') {
			throw ExecutionError(ErrorStage::SCHEMA, "data", "GraphQL response root must be an object");
		}
		auto rows = AllocateRows();
		ParseEnvelope(rows);
		uint64_t retained = decoded_memory;
		uint64_t updated = 0;
		if (!CheckedAdd(retained, static_cast<uint64_t>(page_end_cursor.capacity()), updated) ||
		    updated > limits.max_decoded_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL response exceeded its decoded-memory budget");
		}
		return {std::move(rows), page_has_next, std::move(page_end_cursor), updated};
	}

private:
	struct Slot {
		Slot() : seen(false), valid(true), bigint_value(0), boolean_value(false) {
		}
		bool seen;
		bool valid;
		int64_t bigint_value;
		std::string varchar_value;
		bool boolean_value;
	};

	void CheckRemoteErrors() {
		Reset();
		SkipWhitespace();
		if (Peek() != '{') {
			return;
		}
		Expect('{');
		SkipWhitespace();
		bool errors_seen = false;
		bool has_remote_errors = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			SkipWhitespace();
			if (key != "errors") {
				SkipValue();
			} else {
				if (errors_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "errors", "GraphQL errors field is duplicated");
				}
				errors_seen = true;
				if (Peek() != '[') {
					SkipValue();
					throw ExecutionError(ErrorStage::SCHEMA, "errors", "GraphQL errors field must be an array");
				}
				Expect('[');
				SkipWhitespace();
				while (Peek() != ']') {
					has_remote_errors = true;
					SkipValue();
					ArraySeparator();
				}
				Expect(']');
			}
			ObjectSeparator();
		}
		Expect('}');
		if (has_remote_errors) {
			throw ExecutionError(ErrorStage::REMOTE_PROTOCOL, "errors",
			                     "remote protocol response reported application errors");
		}
	}

	std::vector<TypedRow> AllocateRows() {
		if (limits.max_records == 0 || limits.max_records > 100 || limits.max_string_bytes == 0 ||
		    limits.max_string_bytes > 512 || limits.max_json_nesting == 0 || limits.max_json_nesting > 16 ||
		    limits.max_decoded_memory_bytes == 0 || profile.Columns().size() != 8) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "GraphQL decoder received an invalid profile");
		}
		std::vector<TypedRow> rows;
		try {
			rows.reserve(static_cast<std::size_t>(limits.max_records));
		} catch (const std::bad_alloc &) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL rows exceeded their decoded-memory budget");
		}
		uint64_t bytes = 0;
		if (!CheckedMultiply(static_cast<uint64_t>(rows.capacity()), static_cast<uint64_t>(sizeof(TypedRow)), bytes) ||
		    bytes > limits.max_decoded_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL rows exceeded their decoded-memory budget");
		}
		decoded_memory = bytes;
		return rows;
	}

	void RequireObject(const char *field) {
		SkipWhitespace();
		if (Peek() != '{') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, field, "required GraphQL response object is missing or invalid");
		}
	}

	void ParseEnvelope(std::vector<TypedRow> &rows) {
		Expect('{');
		SkipWhitespace();
		bool data_seen = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key == "data") {
				if (data_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "data", "required GraphQL response field is duplicated");
				}
				data_seen = true;
				ParseData(rows);
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!data_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "data", "required GraphQL response field is missing");
		}
		if (!page_info_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories.pageInfo",
			                     "required GraphQL page information is missing");
		}
	}

	void ParseData(std::vector<TypedRow> &rows) {
		RequireObject("data");
		Expect('{');
		SkipWhitespace();
		bool viewer_seen = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key == "viewer") {
				if (viewer_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "data.viewer",
					                     "required GraphQL response field is duplicated");
				}
				viewer_seen = true;
				ParseViewer(rows);
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!viewer_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "data.viewer", "required GraphQL response field is missing");
		}
	}

	void ParseViewer(std::vector<TypedRow> &rows) {
		RequireObject("data.viewer");
		Expect('{');
		SkipWhitespace();
		bool repositories_seen = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key == "repositories") {
				if (repositories_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories",
					                     "required GraphQL response field is duplicated");
				}
				repositories_seen = true;
				ParseRepositories(rows);
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!repositories_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories",
			                     "required GraphQL response field is missing");
		}
	}

	void ParseRepositories(std::vector<TypedRow> &rows) {
		RequireObject("data.viewer.repositories");
		Expect('{');
		SkipWhitespace();
		bool nodes_seen = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key == "nodes") {
				if (nodes_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories.nodes",
					                     "required GraphQL response field is duplicated");
				}
				nodes_seen = true;
				ParseNodes(rows);
			} else if (key == "pageInfo") {
				if (page_info_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories.pageInfo",
					                     "required GraphQL response field is duplicated");
				}
				page_info_seen = true;
				ParsePageInfo();
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!nodes_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories.nodes",
			                     "required GraphQL response field is missing");
		}
	}

	void ParseNodes(std::vector<TypedRow> &rows) {
		SkipWhitespace();
		if (Peek() != '[') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories.nodes",
			                     "required GraphQL nodes field must be an array");
		}
		Expect('[');
		SkipWhitespace();
		while (Peek() != ']') {
			if (static_cast<uint64_t>(rows.size()) >= limits.max_records) {
				throw ExecutionError(ErrorStage::RESOURCE, "data.viewer.repositories.nodes",
				                     "GraphQL response exceeded its record budget");
			}
			rows.push_back(ParseNode());
			ArraySeparator();
		}
		Expect(']');
	}

	std::size_t FindColumn(const StrictJsonObjectKey &key) const noexcept {
		const auto &columns = profile.Columns();
		for (std::size_t index = 0; index < columns.size(); index++) {
			if (!columns[index].response_path.empty() && key.Equals(columns[index].response_path[0])) {
				return index;
			}
		}
		return columns.size();
	}

	std::string ParseBoundedString(const std::string &field) {
		SkipWhitespace();
		if (Peek() != '"') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL response field has an incompatible type");
		}
		if (decoded_memory > limits.max_decoded_memory_bytes ||
		    pending_string_memory > limits.max_decoded_memory_bytes - decoded_memory) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL response exceeded its decoded-memory budget");
		}
		const auto remaining_memory = limits.max_decoded_memory_bytes - decoded_memory - pending_string_memory;
		const auto materialized_limit = std::min(limits.max_string_bytes, remaining_memory);
		const bool memory_is_narrower = remaining_memory < limits.max_string_bytes;
		auto value = ParseString(materialized_limit, memory_is_narrower ? "decoded_memory_bytes" : field,
		                         memory_is_narrower ? "GraphQL response exceeded its decoded-memory budget"
		                                            : "GraphQL response string exceeded its byte budget");
		const auto retained = static_cast<uint64_t>(value.capacity());
		if (retained > remaining_memory) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL response exceeded its decoded-memory budget");
		}
		pending_string_memory += retained;
		return value;
	}

	int64_t ParseBigInt(const std::string &field) {
		SkipWhitespace();
		if (Peek() != '-' && (Peek() < '0' || Peek() > '9')) {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL response field has an incompatible type");
		}
		const auto token = ParseNumberToken(32, field, "GraphQL integer exceeded its lexical byte budget");
		if (token.find_first_of(".eE") != std::string::npos) {
			throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL response field has an incompatible type");
		}
		errno = 0;
		char *end = nullptr;
		const auto value = std::strtoll(token.c_str(), &end, 10);
		if (errno == ERANGE || !end || *end != '\0') {
			throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL integer is outside the BIGINT range");
		}
		return static_cast<int64_t>(value);
	}

	bool ParseBoolean(const std::string &field) {
		SkipWhitespace();
		if (Peek() == 't') {
			Literal("true");
			return true;
		}
		if (Peek() == 'f') {
			Literal("false");
			return false;
		}
		SkipValue();
		throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL response field has an incompatible type");
	}

	std::string ParseNestedRequiredString(const std::string &field, const char *nested_key, bool allow_null_parent,
	                                      bool &valid) {
		SkipWhitespace();
		if (allow_null_parent && Peek() == 'n') {
			Literal("null");
			valid = false;
			return std::string();
		}
		if (Peek() != '{') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL response field has an incompatible type");
		}
		Expect('{');
		SkipWhitespace();
		bool nested_seen = false;
		std::string result;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key == nested_key) {
				if (nested_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, field, "GraphQL response field is duplicated");
				}
				nested_seen = true;
				result = ParseBoundedString(field);
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!nested_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, field, "required GraphQL response field is missing");
		}
		valid = true;
		return result;
	}

	TypedRow ParseNode() {
		SkipWhitespace();
		if (Peek() != '{') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, "data.viewer.repositories.nodes",
			                     "GraphQL node does not match the declared schema");
		}
		std::vector<Slot> slots(profile.Columns().size());
		Expect('{');
		SkipWhitespace();
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			const auto index = FindColumn(key);
			if (index == profile.Columns().size()) {
				SkipValue();
			} else {
				auto &slot = slots[index];
				const auto &column = profile.Columns()[index];
				if (slot.seen) {
					throw ExecutionError(ErrorStage::SCHEMA, column.name, "GraphQL response field is duplicated");
				}
				slot.seen = true;
				if (column.response_path.size() == 2) {
					slot.varchar_value = ParseNestedRequiredString(column.name, column.response_path[1].c_str(),
					                                               column.nullable, slot.valid);
				} else {
					switch (column.kind) {
					case ValueKind::VARCHAR:
						slot.varchar_value = ParseBoundedString(column.name);
						break;
					case ValueKind::BIGINT:
						slot.bigint_value = ParseBigInt(column.name);
						break;
					case ValueKind::BOOLEAN:
						slot.boolean_value = ParseBoolean(column.name);
						break;
					}
				}
			}
			ObjectSeparator();
		}
		Expect('}');

		TypedRow row;
		try {
			row.values.reserve(profile.Columns().size());
		} catch (const std::bad_alloc &) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL row exceeded its decoded-memory budget");
		}
		uint64_t row_memory = 0;
		uint64_t row_string_memory = 0;
		if (!CheckedMultiply(static_cast<uint64_t>(row.values.capacity()), static_cast<uint64_t>(sizeof(TypedValue)),
		                     row_memory)) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL row exceeded its decoded-memory budget");
		}
		for (std::size_t index = 0; index < slots.size(); index++) {
			const auto &column = profile.Columns()[index];
			auto &slot = slots[index];
			if (!slot.seen) {
				throw ExecutionError(ErrorStage::SCHEMA, column.name, "required GraphQL response field is missing");
			}
			if (!slot.valid) {
				if (!column.nullable) {
					throw ExecutionError(ErrorStage::SCHEMA, column.name, "required GraphQL response field is null");
				}
				row.values.push_back(TypedValue::Null(column.kind));
			} else if (column.kind == ValueKind::VARCHAR) {
				auto value = TypedValue::Varchar(std::move(slot.varchar_value));
				const auto retained_string = static_cast<uint64_t>(value.varchar_value.capacity());
				uint64_t updated = 0;
				if (!CheckedAdd(row_memory, retained_string, updated) || updated > limits.max_decoded_memory_bytes) {
					throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
					                     "GraphQL row exceeded its decoded-memory budget");
				}
				row_memory = updated;
				if (!CheckedAdd(row_string_memory, retained_string, updated)) {
					throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
					                     "GraphQL row exceeded its decoded-memory budget");
				}
				row_string_memory = updated;
				row.values.push_back(std::move(value));
			} else if (column.kind == ValueKind::BIGINT) {
				row.values.push_back(TypedValue::BigInt(slot.bigint_value));
			} else {
				row.values.push_back(TypedValue::Boolean(slot.boolean_value));
			}
		}
		uint64_t updated = 0;
		if (!CheckedAdd(decoded_memory, row_memory, updated) || updated > limits.max_decoded_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL rows exceeded their decoded-memory budget");
		}
		decoded_memory = updated;
		if (row_string_memory > pending_string_memory) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "GraphQL string-memory accounting became inconsistent");
		}
		pending_string_memory -= row_string_memory;
		return row;
	}

	void ParsePageInfo() {
		RequireObject("data.viewer.repositories.pageInfo");
		Expect('{');
		SkipWhitespace();
		bool has_next_seen = false;
		bool end_cursor_seen = false;
		bool cursor_is_null = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key == "hasNextPage") {
				if (has_next_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "pageInfo.hasNextPage",
					                     "GraphQL page field is duplicated");
				}
				has_next_seen = true;
				page_has_next = ParseBoolean("pageInfo.hasNextPage");
			} else if (key == "endCursor") {
				if (end_cursor_seen) {
					throw ExecutionError(ErrorStage::SCHEMA, "pageInfo.endCursor", "GraphQL page field is duplicated");
				}
				end_cursor_seen = true;
				SkipWhitespace();
				if (Peek() == 'n') {
					Literal("null");
					cursor_is_null = true;
				} else {
					page_end_cursor = ParseBoundedString("pageInfo.endCursor");
				}
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!has_next_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "pageInfo.hasNextPage", "required GraphQL page field is missing");
		}
		if (!end_cursor_seen) {
			throw ExecutionError(ErrorStage::SCHEMA, "pageInfo.endCursor", "required GraphQL page field is missing");
		}
		if (page_has_next && (cursor_is_null || page_end_cursor.empty())) {
			throw ExecutionError(ErrorStage::SCHEMA, "pageInfo.endCursor",
			                     "GraphQL continuation cursor is missing or empty");
		}
	}

	const AdmittedGraphqlRequestProfile &profile;
	const GraphqlDecodeLimits &limits;
	uint64_t decoded_memory;
	// String capacities retained in the node slots or page cursor before they
	// are transferred into the page's durable memory accounting.
	uint64_t pending_string_memory;
	bool page_has_next;
	bool page_info_seen;
	std::string page_end_cursor;
};

} // namespace

DecodedGraphqlPage DecodeGraphqlResponse(const std::string &body, const AdmittedGraphqlRequestProfile &profile,
                                         const GraphqlDecodeLimits &limits, ExecutionControl &control) {
	Checkpoint(control, limits.deadline);
	try {
		GraphqlJsonParser parser(body, profile, limits, control);
		return parser.Parse();
	} catch (const ExecutionCancelled &) {
		throw;
	} catch (const ExecutionError &) {
		throw;
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
		                     "GraphQL response exceeded available decoded memory");
	} catch (...) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "GraphQL response decoder failed");
	}
}

} // namespace internal
} // namespace duckdb_api
