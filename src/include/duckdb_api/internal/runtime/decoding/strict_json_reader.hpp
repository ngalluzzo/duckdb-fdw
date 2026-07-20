#pragma once

#include "duckdb_api/execution.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace duckdb_api {
namespace internal {

// Allocation-free decoded object-key token. Recognized response keys fit in
// the fixed comparison bytes. Longer valid keys become an unmatched sentinel,
// allowing ignored remote fields to remain bounded without changing envelope
// precedence (notably a later GraphQL errors field).
class StrictJsonObjectKey {
public:
	bool Equals(const char *expected) const noexcept;
	bool Equals(const std::string &expected) const noexcept;
	bool operator==(const char *expected) const noexcept;
	bool operator!=(const char *expected) const noexcept;

private:
	friend class StrictJsonReader;
	StrictJsonObjectKey() noexcept;
	void Append(char value) noexcept;
	void AppendCodePoint(uint32_t value) noexcept;

	std::array<char, 64> bytes;
	std::size_t size;
	bool overflow;
};

// Protocol-neutral strict JSON lexical reader for Runtime decoders that need a
// schema-specific traversal. It owns cursor movement, Unicode and numeric
// grammar, complete syntax validation, nesting, cancellation, and deadline
// checks—but no response paths, columns, nullability, or protocol semantics.
class StrictJsonReader {
public:
	StrictJsonReader(const std::string &input, uint64_t max_json_nesting,
	                 std::chrono::steady_clock::time_point deadline, ExecutionControl &control);

	void Reset() noexcept;
	void ValidateDocument();
	void Check() const;
	char Peek() const noexcept;
	void SkipWhitespace();
	void Expect(char expected);
	void RequireObjectKey() const;
	void ObjectSeparator();
	void ArraySeparator();
	// Materialized tokens are bounded before each decoded byte is appended.
	std::string ParseString(uint64_t max_decoded_bytes, const std::string &budget_field, const char *safe_message);
	StrictJsonObjectKey ParseObjectKey();
	std::string ParseNumberToken(uint64_t max_token_bytes, const std::string &budget_field, const char *safe_message);
	void Literal(const char *value);
	void SkipValue(uint64_t depth = 0);

private:
	uint32_t ParseHexCodeUnit();
	uint32_t ParseEscapedUnicode();
	void AppendCodePoint(uint32_t value, std::string &result, uint64_t max_decoded_bytes,
	                     const std::string &budget_field, const char *safe_message);
	void AppendUtf8(char first_character, std::string &result, uint64_t max_decoded_bytes,
	                const std::string &budget_field, const char *safe_message);
	void SkipUtf8(char first_character);
	void SkipString();
	void SkipNumber();

	const std::string &input;
	uint64_t max_json_nesting;
	std::chrono::steady_clock::time_point deadline;
	ExecutionControl &control;
	std::size_t position;
};

} // namespace internal
} // namespace duckdb_api
