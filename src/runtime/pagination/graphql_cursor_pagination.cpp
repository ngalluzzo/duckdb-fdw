#include "duckdb_api/internal/runtime/pagination/graphql_cursor_pagination.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace duckdb_api {
namespace internal {

GraphqlCursorError::GraphqlCursorError(GraphqlCursorErrorKind kind_p, std::string field_p, std::string safe_message_p)
    : kind(kind_p), field(std::move(field_p)), safe_message(std::move(safe_message_p)) {
}

const char *GraphqlCursorError::what() const noexcept {
	return safe_message.c_str();
}

const std::string &GraphqlCursorError::Field() const noexcept {
	return field;
}

const std::string &GraphqlCursorError::SafeMessage() const noexcept {
	return safe_message;
}

GraphqlCursorErrorKind GraphqlCursorError::Kind() const noexcept {
	return kind;
}

GraphqlCursorState::GraphqlCursorState(uint64_t max_pages_p, uint64_t max_cursor_bytes_p)
    : max_pages(max_pages_p), max_cursor_bytes(max_cursor_bytes_p), requested_pages(0), exhausted(false), failed(false),
      seen() {
	if (max_pages == 0 || max_pages > 32 || max_cursor_bytes == 0 || max_cursor_bytes > 512) {
		throw GraphqlCursorError(GraphqlCursorErrorKind::PROFILE, "pagination.cursor",
		                         "GraphQL cursor profile is invalid");
	}
	try {
		seen.reserve(static_cast<std::size_t>(max_pages));
	} catch (const std::bad_alloc &) {
		throw GraphqlCursorError(GraphqlCursorErrorKind::RESOURCE_BUDGET, "decoded_memory_bytes",
		                         "GraphQL cursor state exceeded available memory");
	}
}

const std::string *GraphqlCursorState::CurrentCursor() const noexcept {
	return exhausted || failed || seen.empty() ? nullptr : &seen.back();
}

uint64_t GraphqlCursorState::RequestedPages() const noexcept {
	return requested_pages;
}

uint64_t GraphqlCursorState::RetainedMemoryBytes() const noexcept {
	const auto overhead = static_cast<uint64_t>(seen.capacity()) * static_cast<uint64_t>(sizeof(std::string));
	uint64_t result = overhead;
	for (std::size_t index = 0; index < seen.size(); index++) {
		const auto capacity = static_cast<uint64_t>(seen[index].capacity());
		if (capacity > std::numeric_limits<uint64_t>::max() - result) {
			return std::numeric_limits<uint64_t>::max();
		}
		result += capacity;
	}
	return result;
}

bool GraphqlCursorState::IsExhausted() const noexcept {
	return exhausted;
}

bool GraphqlCursorState::IsFailed() const noexcept {
	return failed;
}

[[noreturn]] void GraphqlCursorState::Reject(GraphqlCursorErrorKind kind, std::string field, std::string safe_message) {
	failed = true;
	std::vector<std::string>().swap(seen);
	throw GraphqlCursorError(kind, std::move(field), std::move(safe_message));
}

void GraphqlCursorState::MarkRequestStarted() {
	if (failed || exhausted || requested_pages >= max_pages) {
		Reject(GraphqlCursorErrorKind::RESOURCE_BUDGET, "pagination.cursor",
		       "GraphQL cursor traversal exceeded its page authority");
	}
	requested_pages++;
}

void GraphqlCursorState::Advance(bool has_next, std::string end_cursor) {
	if (failed || exhausted || requested_pages == 0) {
		Reject(GraphqlCursorErrorKind::PROTOCOL, "pagination.cursor", "GraphQL cursor state cannot advance");
	}
	if (!has_next) {
		exhausted = true;
		std::vector<std::string>().swap(seen);
		return;
	}
	if (end_cursor.empty()) {
		Reject(GraphqlCursorErrorKind::PROTOCOL, "pagination.cursor", "GraphQL continuation cursor is missing");
	}
	if (static_cast<uint64_t>(end_cursor.size()) > max_cursor_bytes) {
		Reject(GraphqlCursorErrorKind::RESOURCE_BUDGET, "pagination.cursor",
		       "GraphQL continuation cursor exceeded its byte budget");
	}
	if (std::find(seen.begin(), seen.end(), end_cursor) != seen.end()) {
		Reject(GraphqlCursorErrorKind::PROTOCOL, "pagination.cursor", "GraphQL continuation cursor repeated");
	}
	try {
		seen.push_back(std::move(end_cursor));
	} catch (const std::bad_alloc &) {
		Reject(GraphqlCursorErrorKind::RESOURCE_BUDGET, "decoded_memory_bytes",
		       "GraphQL cursor state exceeded available memory");
	}
}

void GraphqlCursorState::Fail() noexcept {
	failed = true;
	Release();
}

void GraphqlCursorState::Release() noexcept {
	std::vector<std::string>().swap(seen);
}

} // namespace internal
} // namespace duckdb_api
