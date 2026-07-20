#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

class GraphqlCursorError : public std::exception {
public:
	GraphqlCursorError(std::string field, std::string safe_message);
	const char *what() const noexcept override;
	const std::string &Field() const noexcept;
	const std::string &SafeMessage() const noexcept;

private:
	std::string field;
	std::string safe_message;
};

// One stream owns one forward cursor state. The first request uses null. Every
// accepted continuation is nonempty, unseen, and bounded; exhaustion and any
// rejected transition are terminal. No cursor grants ordering or resume.
class GraphqlCursorState {
public:
	GraphqlCursorState(uint64_t max_pages, uint64_t max_cursor_bytes);

	const std::string *CurrentCursor() const noexcept;
	uint64_t RequestedPages() const noexcept;
	uint64_t RetainedMemoryBytes() const noexcept;
	bool IsExhausted() const noexcept;
	bool IsFailed() const noexcept;

	void MarkRequestStarted();
	void Advance(bool has_next, std::string end_cursor);
	void Fail() noexcept;
	void Release() noexcept;

private:
	[[noreturn]] void Reject(std::string field, std::string safe_message);

	uint64_t max_pages;
	uint64_t max_cursor_bytes;
	uint64_t requested_pages;
	bool exhausted;
	bool failed;
	std::vector<std::string> seen;
};

} // namespace internal
} // namespace duckdb_api
