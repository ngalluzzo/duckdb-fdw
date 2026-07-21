#include "duckdb_api/internal/runtime/decoding/graphql_response_decoder.hpp"
#include "duckdb_api/internal/runtime/decoding/strict_json_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <functional>
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
	GraphqlJsonParser(const std::string &input, const AdmittedGraphqlRequestProfile &profile_p,
	                  const GraphqlDecodeLimits &limits_p, ExecutionControl &control)
	    : StrictJsonReader(input, limits_p.max_json_nesting, limits_p.deadline, control), profile(profile_p),
	      limits(limits_p), decoded_memory(0), pending_string_memory(0), has_next(false), end_cursor_is_null(false) {
	}

	DecodedGraphqlPage Parse() {
		ValidateProfile();
		ValidateDocument();
		CheckErrors();
		auto rows = ParseRows();
		ParseHasNext();
		ParseEndCursor();
		if (has_next && (end_cursor_is_null || end_cursor.empty())) {
			throw ExecutionError(ErrorStage::SCHEMA, "pagination.end_cursor",
			                     "GraphQL continuation cursor is missing or empty");
		}
		uint64_t retained = 0;
		if (!CheckedAdd(decoded_memory, static_cast<uint64_t>(end_cursor.capacity()), retained) ||
		    retained > limits.max_decoded_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL response exceeded its decoded-memory budget");
		}
		return {std::move(rows), has_next, std::move(end_cursor), retained};
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

	void ValidateProfile() {
		if (limits.max_records == 0 || limits.max_records > 100 || limits.max_string_bytes == 0 ||
		    limits.max_string_bytes > 512 || limits.max_json_nesting == 0 || limits.max_json_nesting > 16 ||
		    limits.max_decoded_memory_bytes == 0 || profile.Columns().empty() || profile.Columns().size() > 256) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "GraphQL decoder received an invalid profile");
		}
	}

	std::string PathName(const std::vector<std::string> &path) const {
		std::string result;
		for (const auto &segment : path) {
			if (!result.empty()) {
				result += ".";
			}
			result += segment;
		}
		return result;
	}

	bool Traverse(const std::vector<std::string> &path, std::size_t index, bool required,
	              const std::function<void()> &leaf) {
		SkipWhitespace();
		if (Peek() != '{') {
			SkipValue();
			if (required) {
				throw ExecutionError(ErrorStage::SCHEMA, PathName(path),
				                     "required GraphQL response path is missing or invalid");
			}
			return false;
		}
		Expect('{');
		SkipWhitespace();
		bool found = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key.Equals(path[index])) {
				if (found) {
					throw ExecutionError(ErrorStage::SCHEMA, PathName(path), "GraphQL response path is duplicated");
				}
				found = true;
				if (index + 1 == path.size()) {
					leaf();
				} else {
					(void)Traverse(path, index + 1, required, leaf);
				}
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!found && required) {
			throw ExecutionError(ErrorStage::SCHEMA, PathName(path), "required GraphQL response path is missing");
		}
		return found;
	}

	void CheckErrors() {
		Reset();
		(void)Traverse(profile.ErrorsPath(), 0, false, [this]() {
			SkipWhitespace();
			if (Peek() != '[') {
				SkipValue();
				throw ExecutionError(ErrorStage::SCHEMA, PathName(profile.ErrorsPath()),
				                     "GraphQL errors field must be an array");
			}
			Expect('[');
			SkipWhitespace();
			const bool nonempty = Peek() != ']';
			while (Peek() != ']') {
				SkipValue();
				ArraySeparator();
			}
			Expect(']');
			if (nonempty) {
				throw ExecutionError(ErrorStage::REMOTE_PROTOCOL, "errors",
				                     "remote protocol response reported application errors");
			}
		});
	}

	std::vector<TypedRow> AllocateRows() {
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

	std::vector<TypedRow> ParseRows() {
		auto rows = AllocateRows();
		Reset();
		(void)Traverse(profile.NodesPath(), 0, true, [this, &rows]() {
			SkipWhitespace();
			if (Peek() != '[') {
				SkipValue();
				throw ExecutionError(ErrorStage::SCHEMA, PathName(profile.NodesPath()),
				                     "required GraphQL nodes field must be an array");
			}
			Expect('[');
			SkipWhitespace();
			while (Peek() != ']') {
				if (static_cast<uint64_t>(rows.size()) >= limits.max_records) {
					throw ExecutionError(ErrorStage::RESOURCE, PathName(profile.NodesPath()),
					                     "GraphQL response exceeded its record budget");
				}
				const auto begin = Position();
				if (Peek() != '{') {
					SkipValue();
					throw ExecutionError(ErrorStage::SCHEMA, PathName(profile.NodesPath()),
					                     "GraphQL node does not match the declared schema");
				}
				SkipValue();
				const auto end = Position();
				rows.push_back(ParseNode(begin, end));
				SetPosition(end);
				ArraySeparator();
			}
			Expect(']');
		});
		return rows;
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
		const auto remaining = limits.max_decoded_memory_bytes - decoded_memory - pending_string_memory;
		auto value =
		    ParseString(std::min(limits.max_string_bytes, remaining),
		                remaining < limits.max_string_bytes ? "decoded_memory_bytes" : field,
		                remaining < limits.max_string_bytes ? "GraphQL response exceeded its decoded-memory budget"
		                                                    : "GraphQL response string exceeded its byte budget");
		if (static_cast<uint64_t>(value.capacity()) > remaining) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL response exceeded its decoded-memory budget");
		}
		pending_string_memory += static_cast<uint64_t>(value.capacity());
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

	void ParseColumn(const AdmittedGraphqlColumn &column, std::size_t path_index, Slot &slot) {
		SkipWhitespace();
		if (Peek() == 'n') {
			Literal("null");
			if (!column.nullable) {
				throw ExecutionError(ErrorStage::SCHEMA, column.name, "required GraphQL response field is null");
			}
			slot.seen = true;
			slot.valid = false;
			return;
		}
		if (Peek() != '{') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, column.name,
			                     "GraphQL response field has an incompatible parent type");
		}
		Expect('{');
		SkipWhitespace();
		bool found = false;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseObjectKey();
			SkipWhitespace();
			Expect(':');
			if (key.Equals(column.response_path[path_index])) {
				if (found) {
					throw ExecutionError(ErrorStage::SCHEMA, column.name, "GraphQL response field is duplicated");
				}
				found = true;
				if (path_index + 1 < column.response_path.size()) {
					ParseColumn(column, path_index + 1, slot);
				} else {
					SkipWhitespace();
					if (Peek() == 'n') {
						Literal("null");
						if (!column.nullable) {
							throw ExecutionError(ErrorStage::SCHEMA, column.name,
							                     "required GraphQL response field is null");
						}
						slot.valid = false;
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
					slot.seen = true;
				}
			} else {
				SkipValue();
			}
			ObjectSeparator();
		}
		Expect('}');
		if (!found) {
			throw ExecutionError(ErrorStage::SCHEMA, column.name, "required GraphQL response field is missing");
		}
	}

	TypedRow ParseNode(std::size_t begin, std::size_t end) {
		std::vector<Slot> slots(profile.Columns().size());
		for (std::size_t index = 0; index < profile.Columns().size(); index++) {
			SetPosition(begin);
			ParseColumn(profile.Columns()[index], 0, slots[index]);
			if (Position() != end || !slots[index].seen) {
				throw ExecutionError(ErrorStage::SCHEMA, profile.Columns()[index].name,
				                     "required GraphQL response field is missing");
			}
		}
		TypedRow row;
		row.values.reserve(profile.Columns().size());
		uint64_t row_memory = 0;
		if (!CheckedMultiply(static_cast<uint64_t>(row.values.capacity()), static_cast<uint64_t>(sizeof(TypedValue)),
		                     row_memory)) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL row exceeded its decoded-memory budget");
		}
		uint64_t row_strings = 0;
		for (std::size_t index = 0; index < slots.size(); index++) {
			const auto &column = profile.Columns()[index];
			auto &slot = slots[index];
			if (!slot.valid) {
				row.values.push_back(TypedValue::Null(column.kind));
			} else if (column.kind == ValueKind::VARCHAR) {
				auto value = TypedValue::Varchar(std::move(slot.varchar_value));
				uint64_t updated = 0;
				if (!CheckedAdd(row_memory, static_cast<uint64_t>(value.varchar_value.capacity()), updated) ||
				    updated > limits.max_decoded_memory_bytes) {
					throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
					                     "GraphQL row exceeded its decoded-memory budget");
				}
				row_memory = updated;
				if (!CheckedAdd(row_strings, static_cast<uint64_t>(value.varchar_value.capacity()), updated)) {
					throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
					                     "GraphQL row exceeded its decoded-memory budget");
				}
				row_strings = updated;
				row.values.push_back(std::move(value));
			} else if (column.kind == ValueKind::BIGINT) {
				row.values.push_back(TypedValue::BigInt(slot.bigint_value));
			} else {
				row.values.push_back(TypedValue::Boolean(slot.boolean_value));
			}
		}
		uint64_t updated = 0;
		if (!CheckedAdd(decoded_memory, row_memory, updated) || updated > limits.max_decoded_memory_bytes ||
		    row_strings > pending_string_memory) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL rows exceeded their decoded-memory budget");
		}
		decoded_memory = updated;
		pending_string_memory -= row_strings;
		return row;
	}

	void ParseHasNext() {
		Reset();
		(void)Traverse(profile.HasNextPagePath(), 0, true,
		               [this]() { has_next = ParseBoolean("pagination.has_next"); });
	}

	void ParseEndCursor() {
		Reset();
		(void)Traverse(profile.EndCursorPath(), 0, true, [this]() {
			SkipWhitespace();
			if (Peek() == 'n') {
				Literal("null");
				end_cursor_is_null = true;
				return;
			}
			end_cursor = ParseBoundedString("pagination.end_cursor");
		});
	}

	const AdmittedGraphqlRequestProfile &profile;
	const GraphqlDecodeLimits &limits;
	uint64_t decoded_memory;
	uint64_t pending_string_memory;
	bool has_next;
	bool end_cursor_is_null;
	std::string end_cursor;
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
