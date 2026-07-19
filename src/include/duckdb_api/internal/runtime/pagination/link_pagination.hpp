#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

// Private, DuckDB-free failure returned by the fixed Link pagination service.
// Field and SafeMessage are bounded diagnostics chosen by Runtime; received
// Link values and target strings are never retained by or exposed through it.
enum class LinkPaginationErrorKind : uint8_t { MALFORMED, POLICY, STATE };

class LinkPaginationError : public std::exception {
public:
	LinkPaginationError(LinkPaginationErrorKind kind, std::string field, std::string safe_message);

	const char *what() const noexcept override;
	LinkPaginationErrorKind Kind() const noexcept;
	const std::string &Field() const noexcept;
	const std::string &SafeMessage() const noexcept;

private:
	LinkPaginationErrorKind kind;
	std::string field;
	std::string safe_message;
};

// The parser returns only a typed page transition. It deliberately returns no
// received URL, authority, path, query spelling, or request object. Executor
// integration reconstructs a request from its separately validated immutable
// operation and this typed page number.
struct LinkPageTransition {
	bool has_next;
	uint64_t next_page;
};

// Sequential state for RFC 0007's fixed GitHub repository profile. The first
// page is always 1; resume and caller-selected starting state are unsupported.
// Advance parses physical Link field-values in receipt order, accepts zero or
// one rel=next target, requires the exact fixed HTTPS target with page equal to
// CurrentPage()+1, and records only accepted typed page identities. Any error
// makes the state terminal so a caller cannot continue after rejected remote
// metadata. This object is scan-owned and is not thread-safe; the owning stream
// supplies synchronization and resource/cancellation authority.
class LinkPaginationState {
public:
	LinkPaginationState();

	LinkPageTransition Advance(const std::vector<std::string> &link_field_values);

	uint64_t CurrentPage() const noexcept;
	bool Exhausted() const noexcept;
	bool Failed() const noexcept;
	std::size_t SeenPageCount() const noexcept;

private:
	uint64_t current_page;
	std::vector<uint64_t> seen_pages;
	bool exhausted;
	bool failed;
};

} // namespace internal
} // namespace duckdb_api
