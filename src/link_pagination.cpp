#include "duckdb_api/internal/link_pagination.hpp"

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
	throw LinkPaginationError(LinkPaginationErrorKind::STATE, PAGINATION_FIELD,
	                          "Link pagination state cannot advance");
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
	return IsAsciiAlpha(value) || IsAsciiDigit(value) || value == '!' || value == '#' || value == '$' || value == '%' || value == '&' ||
	       value == '\'' || value == '*' || value == '+' || value == '-' || value == '.' || value == '^' ||
	       value == '_' || value == '`' || value == '|' || value == '~';
}

bool IsHexDigit(unsigned char value) noexcept {
	return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

bool IsUriCharacter(unsigned char value) noexcept {
	if (IsAsciiAlpha(value) || IsAsciiDigit(value) || value == '-' || value == '.' || value == '_' || value == '~') {
		return true;
	}
	switch (value) {
	case ':':
	case '/':
	case '?':
	case '#':
	case '[':
	case ']':
	case '@':
	case '!':
	case '$':
	case '&':
	case '\'':
	case '(':
	case ')':
	case '*':
	case '+':
	case ',':
	case ';':
	case '=':
	case '%':
		return true;
	default:
		return false;
	}
}

void ValidateUriReference(const std::string &target) {
	if (target.empty()) {
		ThrowMalformed();
	}
	for (std::size_t index = 0; index < target.size(); index++) {
		const auto byte = static_cast<unsigned char>(target[index]);
		if (!IsUriCharacter(byte)) {
			ThrowMalformed();
		}
		if (byte == '%') {
			if (index + 2 >= target.size() || !IsHexDigit(static_cast<unsigned char>(target[index + 1])) ||
			    !IsHexDigit(static_cast<unsigned char>(target[index + 2]))) {
				ThrowMalformed();
			}
			index += 2;
		}
	}
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
	return offset < value.size() && value[offset] == '"' ? ParseQuotedString(value, offset)
	                                                       : ParseToken(value, offset);
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

bool RelationContainsNext(const std::string &relation) {
	if (relation.empty()) {
		ThrowMalformed();
	}
	std::size_t offset = 0;
	bool has_relation = false;
	bool has_next = false;
	while (offset < relation.size()) {
		while (offset < relation.size() && relation[offset] == ' ') {
			offset++;
		}
		if (offset == relation.size()) {
			break;
		}
		const auto begin = offset;
		while (offset < relation.size() && relation[offset] != ' ') {
			const auto byte = static_cast<unsigned char>(relation[offset]);
			if (!IsTokenCharacter(byte)) {
				ThrowMalformed();
			}
			offset++;
		}
		const auto token = relation.substr(begin, offset - begin);
		has_relation = true;
		has_next = has_next || token == "next";
	}
	if (!has_relation) {
		ThrowMalformed();
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
	ValidateUriReference(result.target);
	offset++;

	bool saw_rel = false;
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
			ThrowMalformed();
		}
		offset++;
		SkipOws(field, offset);
		const auto parameter_value = ParseParameterValue(field, offset);
		if (EqualsAsciiIgnoreCase(name, "rel")) {
			if (saw_rel) {
				ThrowMalformed();
			}
			saw_rel = true;
			result.has_next_relation = RelationContainsNext(parameter_value);
		}
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

uint64_t ValidateNextTarget(const std::string &target, uint64_t current_page,
	                        const std::vector<uint64_t> &seen_pages) {
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
	for (const auto &field : fields) {
		std::size_t offset = 0;
		SkipOws(field, offset);
		if (offset == field.size()) {
			ThrowMalformed();
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
				ThrowMalformed();
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
		throw LinkPaginationError(LinkPaginationErrorKind::STATE, PAGINATION_FIELD,
		                          "Link pagination state failed");
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
