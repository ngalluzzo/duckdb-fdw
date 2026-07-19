#include "duckdb_api/internal/runtime/pagination/link_pagination.hpp"

#include "duckdb_api/internal/runtime/pagination/link_header.hpp"

#include <limits>
#include <new>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

const char *const PAGINATION_FIELD = "pagination.next";

[[noreturn]] void ThrowMalformed() {
	throw LinkPaginationError(LinkPaginationErrorKind::MALFORMED, PAGINATION_FIELD,
	                          "Link pagination metadata is malformed");
}

[[noreturn]] void ThrowPolicy() {
	throw LinkPaginationError(LinkPaginationErrorKind::POLICY, PAGINATION_FIELD,
	                          "Link pagination target is outside the accepted policy");
}

[[noreturn]] void ThrowState() {
	throw LinkPaginationError(LinkPaginationErrorKind::STATE, PAGINATION_FIELD, "Link pagination state cannot advance");
}

uint64_t ParsePositiveDecimal(const std::string &value) {
	if (value.empty() || value[0] < '1' || value[0] > '9') {
		ThrowPolicy();
	}
	uint64_t result = 0;
	for (const auto character : value) {
		if (character < '0' || character > '9') {
			ThrowPolicy();
		}
		const auto digit = static_cast<uint64_t>(character - '0');
		if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
			ThrowPolicy();
		}
		result = result * 10 + digit;
	}
	return result;
}

uint64_t ValidateNextTarget(const std::string &target, uint64_t current_page, const std::vector<uint64_t> &seen_pages) {
	const std::string authority = "https://api.github.com";
	if (target.compare(0, authority.size(), authority) != 0) {
		ThrowPolicy();
	}
	std::size_t offset = authority.size();
	if (target.compare(offset, 4, ":443") == 0) {
		offset += 4;
	}
	const std::string path_and_query = "/user/repos?";
	if (target.compare(offset, path_and_query.size(), path_and_query) != 0) {
		ThrowPolicy();
	}
	offset += path_and_query.size();
	if (offset >= target.size() || target.find('#', offset) != std::string::npos ||
	    target.find('%', offset) != std::string::npos) {
		ThrowPolicy();
	}

	bool saw_per_page = false;
	bool saw_page = false;
	uint64_t parsed_page = 0;
	std::size_t field_count = 0;
	while (offset < target.size()) {
		const auto separator = target.find('&', offset);
		const auto field_end = separator == std::string::npos ? target.size() : separator;
		if (field_end == offset) {
			ThrowPolicy();
		}
		const auto equals = target.find('=', offset);
		if (equals == std::string::npos || equals >= field_end || equals == offset ||
		    target.find('=', equals + 1) < field_end) {
			ThrowPolicy();
		}
		const auto name = target.substr(offset, equals - offset);
		const auto value = target.substr(equals + 1, field_end - equals - 1);
		if (name == "per_page") {
			if (saw_per_page || value != "100") {
				ThrowPolicy();
			}
			saw_per_page = true;
		} else if (name == "page") {
			if (saw_page) {
				ThrowPolicy();
			}
			saw_page = true;
			parsed_page = ParsePositiveDecimal(value);
		} else {
			ThrowPolicy();
		}
		field_count++;
		if (separator == std::string::npos) {
			break;
		}
		if (separator + 1 == target.size()) {
			ThrowPolicy();
		}
		offset = separator + 1;
	}
	if (field_count != 2 || !saw_per_page || !saw_page || current_page == std::numeric_limits<uint64_t>::max() ||
	    parsed_page != current_page + 1) {
		ThrowPolicy();
	}
	for (const auto seen_page : seen_pages) {
		if (seen_page == parsed_page) {
			ThrowPolicy();
		}
	}
	return parsed_page;
}

class NextTargetSelector final : public LinkHeaderValueVisitor {
public:
	NextTargetSelector() : found_next(false) {
	}

	void Visit(const std::string &target, bool has_next_relation) override {
		if (!has_next_relation) {
			return;
		}
		if (found_next) {
			ThrowPolicy();
		}
		found_next = true;
		next_target = target;
	}

	bool FoundNext() const noexcept {
		return found_next;
	}

	const std::string &NextTarget() const noexcept {
		return next_target;
	}

private:
	bool found_next;
	std::string next_target;
};

LinkPageTransition ParseTransition(const std::vector<std::string> &fields, uint64_t current_page,
                                   const std::vector<uint64_t> &seen_pages) {
	NextTargetSelector selector;
	try {
		ParseLinkHeaderFields(fields, selector);
	} catch (const LinkHeaderSyntaxError &) {
		ThrowMalformed();
	}
	if (!selector.FoundNext()) {
		return {false, 0};
	}
	return {true, ValidateNextTarget(selector.NextTarget(), current_page, seen_pages)};
}

} // namespace

LinkPaginationError::LinkPaginationError(LinkPaginationErrorKind kind_p, std::string field_p,
                                         std::string safe_message_p)
    : kind(kind_p), field(std::move(field_p)), safe_message(std::move(safe_message_p)) {
}

const char *LinkPaginationError::what() const noexcept {
	return safe_message.c_str();
}

LinkPaginationErrorKind LinkPaginationError::Kind() const noexcept {
	return kind;
}

const std::string &LinkPaginationError::Field() const noexcept {
	return field;
}

const std::string &LinkPaginationError::SafeMessage() const noexcept {
	return safe_message;
}

LinkPaginationState::LinkPaginationState() : current_page(1), seen_pages(1, 1), exhausted(false), failed(false) {
}

LinkPageTransition LinkPaginationState::Advance(const std::vector<std::string> &link_field_values) {
	if (failed || exhausted) {
		ThrowState();
	}
	try {
		const auto transition = ParseTransition(link_field_values, current_page, seen_pages);
		if (!transition.has_next) {
			exhausted = true;
			return transition;
		}
		seen_pages.push_back(transition.next_page);
		current_page = transition.next_page;
		return transition;
	} catch (const LinkPaginationError &) {
		failed = true;
		throw;
	} catch (const std::bad_alloc &) {
		failed = true;
		throw LinkPaginationError(LinkPaginationErrorKind::STATE, PAGINATION_FIELD,
		                          "Link pagination state exceeded available memory");
	} catch (...) {
		failed = true;
		throw LinkPaginationError(LinkPaginationErrorKind::STATE, PAGINATION_FIELD, "Link pagination state failed");
	}
}

uint64_t LinkPaginationState::CurrentPage() const noexcept {
	return current_page;
}

bool LinkPaginationState::Exhausted() const noexcept {
	return exhausted;
}

bool LinkPaginationState::Failed() const noexcept {
	return failed;
}

std::size_t LinkPaginationState::SeenPageCount() const noexcept {
	return seen_pages.size();
}

} // namespace internal
} // namespace duckdb_api
