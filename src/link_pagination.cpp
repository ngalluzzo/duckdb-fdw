#include "duckdb_api/internal/link_pagination.hpp"
#include "duckdb_api/internal/uri_reference.hpp"

#include <limits>
#include <new>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

const char *const PAGINATION_FIELD = "pagination.next";
constexpr std::size_t MAX_IGNORED_EMPTY_LIST_ELEMENTS = 128;

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

bool IsOws(char value) noexcept {
	return value == ' ' || value == '\t';
}

bool IsAsciiAlpha(unsigned char value) noexcept {
	return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

bool IsAsciiDigit(unsigned char value) noexcept {
	return value >= '0' && value <= '9';
}

unsigned char ToAsciiLower(unsigned char value) noexcept {
	return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

void SkipOws(const std::string &value, std::size_t &offset) noexcept {
	while (offset < value.size() && IsOws(value[offset])) {
		offset++;
	}
}

bool IsTokenCharacter(unsigned char value) noexcept {
	return IsAsciiAlpha(value) || IsAsciiDigit(value) || value == '!' || value == '#' || value == '$' || value == '%' ||
	       value == '&' || value == '\'' || value == '*' || value == '+' || value == '-' || value == '.' ||
	       value == '^' || value == '_' || value == '`' || value == '|' || value == '~';
}

std::string ParseToken(const std::string &value, std::size_t &offset) {
	const auto begin = offset;
	while (offset < value.size() && IsTokenCharacter(static_cast<unsigned char>(value[offset]))) {
		offset++;
	}
	if (offset == begin) {
		ThrowMalformed();
	}
	return value.substr(begin, offset - begin);
}

std::string ParseQuotedString(const std::string &value, std::size_t &offset) {
	if (offset >= value.size() || value[offset] != '"') {
		ThrowMalformed();
	}
	offset++;
	std::string result;
	while (offset < value.size()) {
		const auto byte = static_cast<unsigned char>(value[offset++]);
		if (byte == '"') {
			return result;
		}
		if (byte == '\\') {
			if (offset >= value.size()) {
				ThrowMalformed();
			}
			const auto escaped = static_cast<unsigned char>(value[offset++]);
			if (escaped < 0x20 || escaped > 0x7e) {
				ThrowMalformed();
			}
			result.push_back(static_cast<char>(escaped));
			continue;
		}
		if ((byte < 0x20 && byte != '\t') || byte == 0x7f) {
			ThrowMalformed();
		}
		result.push_back(static_cast<char>(byte));
	}
	ThrowMalformed();
}

std::string ParseParameterValue(const std::string &value, std::size_t &offset) {
	return offset < value.size() && value[offset] == '"' ? ParseQuotedString(value, offset) : ParseToken(value, offset);
}

bool EqualsAsciiIgnoreCase(const std::string &left, const char *right) noexcept {
	std::size_t length = 0;
	while (right[length] != '\0') {
		length++;
	}
	if (left.size() != length) {
		return false;
	}
	for (std::size_t index = 0; index < length; index++) {
		const auto left_byte = static_cast<unsigned char>(left[index]);
		const auto right_byte = static_cast<unsigned char>(right[index]);
		if (ToAsciiLower(left_byte) != ToAsciiLower(right_byte)) {
			return false;
		}
	}
	return true;
}

bool IsRegisteredRelation(const std::string &relation) noexcept {
	if (relation.empty() || !IsAsciiAlpha(static_cast<unsigned char>(relation[0]))) {
		return false;
	}
	for (std::size_t index = 1; index < relation.size(); index++) {
		const auto value = static_cast<unsigned char>(relation[index]);
		if (!IsAsciiAlpha(value) && !IsAsciiDigit(value) && value != '.' && value != '-') {
			return false;
		}
	}
	return true;
}

bool RelationContainsNext(const std::string &relation) {
	if (relation.empty() || relation.front() == ' ' || relation.back() == ' ') {
		ThrowMalformed();
	}
	std::size_t offset = 0;
	bool has_next = false;
	while (offset < relation.size()) {
		const auto begin = offset;
		while (offset < relation.size() && relation[offset] != ' ') {
			offset++;
		}
		const auto token = relation.substr(begin, offset - begin);
		// RFC 8288 relation-types are registered names or absolute-URI
		// extensions. Relative URI-references are not relation-types and must not
		// silently convert advertised pagination into clean exhaustion.
		if (!IsRegisteredRelation(token)) {
			if (!IsValidUri(token)) {
				ThrowMalformed();
			}
		}
		has_next = has_next || EqualsAsciiIgnoreCase(token, "next");
		while (offset < relation.size() && relation[offset] == ' ') {
			offset++;
		}
	}
	return has_next;
}

struct ParsedLinkValue {
	std::string target;
	bool has_next_relation;
};

ParsedLinkValue ParseLinkValue(const std::string &field, std::size_t &offset) {
	SkipOws(field, offset);
	if (offset >= field.size() || field[offset] != '<') {
		ThrowMalformed();
	}
	offset++;
	const auto target_begin = offset;
	while (offset < field.size() && field[offset] != '>') {
		offset++;
	}
	if (offset >= field.size()) {
		ThrowMalformed();
	}
	ParsedLinkValue result {field.substr(target_begin, offset - target_begin), false};
	if (!IsValidUriReference(result.target)) {
		ThrowMalformed();
	}
	offset++;

	bool saw_rel = false;
	bool saw_anchor = false;
	while (true) {
		SkipOws(field, offset);
		if (offset >= field.size() || field[offset] == ',') {
			break;
		}
		if (field[offset] != ';') {
			ThrowMalformed();
		}
		offset++;
		SkipOws(field, offset);
		const auto name = ParseToken(field, offset);
		SkipOws(field, offset);
		if (offset >= field.size() || field[offset] != '=') {
			if (EqualsAsciiIgnoreCase(name, "rel")) {
				if (!saw_rel) {
					ThrowMalformed();
				}
				// Even a valueless later rel occurrence is syntactically a
				// link-param and is ignored after the authoritative first rel.
				continue;
			}
			if (EqualsAsciiIgnoreCase(name, "anchor")) {
				ThrowMalformed();
			}
			// Link extension parameters may be valueless. Their presence has no
			// effect on continuation selection.
			continue;
		}
		offset++;
		SkipOws(field, offset);
		const auto parameter_value = ParseParameterValue(field, offset);
		if (EqualsAsciiIgnoreCase(name, "rel")) {
			// RFC 8288 defines only the first rel parameter on a link-value;
			// later occurrences are consumed as grammar but otherwise ignored.
			if (!saw_rel) {
				saw_rel = true;
				result.has_next_relation = RelationContainsNext(parameter_value);
			}
		} else if (EqualsAsciiIgnoreCase(name, "anchor")) {
			if (!IsValidUriReference(parameter_value)) {
				ThrowMalformed();
			}
			saw_anchor = true;
		}
	}
	if (!saw_rel) {
		ThrowMalformed();
	}
	if (saw_anchor) {
		// This fixed relation has no authority to resolve an alternate link
		// context. RFC 8288 requires consumers that cannot apply `anchor` to
		// ignore the entire link rather than treat it as response-relative.
		result.has_next_relation = false;
	}
	return result;
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

LinkPageTransition ParseTransition(const std::vector<std::string> &fields, uint64_t current_page,
                                   const std::vector<uint64_t> &seen_pages) {
	bool found_next = false;
	std::string next_target;
	std::size_t ignored_empty_elements = 0;
	const auto ignore_empty_element = [&ignored_empty_elements]() {
		ignored_empty_elements++;
		if (ignored_empty_elements > MAX_IGNORED_EMPTY_LIST_ELEMENTS) {
			ThrowMalformed();
		}
	};
	for (const auto &field : fields) {
		std::size_t offset = 0;
		SkipOws(field, offset);
		while (offset < field.size() && field[offset] == ',') {
			ignore_empty_element();
			offset++;
			SkipOws(field, offset);
		}
		while (offset < field.size()) {
			const auto parsed = ParseLinkValue(field, offset);
			if (parsed.has_next_relation) {
				if (found_next) {
					ThrowPolicy();
				}
				found_next = true;
				next_target = parsed.target;
			}
			SkipOws(field, offset);
			if (offset == field.size()) {
				break;
			}
			if (field[offset] != ',') {
				ThrowMalformed();
			}
			offset++;
			SkipOws(field, offset);
			if (offset == field.size()) {
				ignore_empty_element();
				break;
			}
			while (field[offset] == ',') {
				ignore_empty_element();
				offset++;
				SkipOws(field, offset);
				if (offset == field.size()) {
					break;
				}
			}
		}
	}
	if (!found_next) {
		return {false, 0};
	}
	return {true, ValidateNextTarget(next_target, current_page, seen_pages)};
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
