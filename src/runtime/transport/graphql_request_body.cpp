#include "duckdb_api/internal/runtime/transport/graphql_request_body.hpp"

#include "duckdb_api/execution.hpp"

#include <cstddef>
#include <new>

namespace duckdb_api {
namespace internal {
namespace {

void AppendHexByte(unsigned char value, std::string &result) {
	static const char HEX[] = "0123456789abcdef";
	result += "\\u00";
	result.push_back(HEX[(value >> 4) & 0x0f]);
	result.push_back(HEX[value & 0x0f]);
}

void AppendJsonString(const std::string &value, std::string &result) {
	result.push_back('"');
	for (std::size_t index = 0; index < value.size(); index++) {
		const auto byte = static_cast<unsigned char>(value[index]);
		switch (byte) {
		case '"':
			result += "\\\"";
			break;
		case '\\':
			result += "\\\\";
			break;
		case '\b':
			result += "\\b";
			break;
		case '\f':
			result += "\\f";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			if (byte < 0x20) {
				AppendHexByte(byte, result);
			} else {
				result.push_back(static_cast<char>(byte));
			}
		}
	}
	result.push_back('"');
}

bool IsLowerHex(char value) noexcept {
	return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

bool ConsumeUtf8(const std::string &value, std::size_t &index, std::size_t end, uint64_t &decoded_bytes) noexcept {
	const auto first = static_cast<unsigned char>(value[index]);
	if (first < 0x80) {
		decoded_bytes++;
		return true;
	}
	std::size_t continuation_count = 0;
	uint32_t code_point = 0;
	uint32_t minimum = 0;
	if ((first & 0xe0) == 0xc0) {
		continuation_count = 1;
		code_point = first & 0x1f;
		minimum = 0x80;
	} else if ((first & 0xf0) == 0xe0) {
		continuation_count = 2;
		code_point = first & 0x0f;
		minimum = 0x800;
	} else if ((first & 0xf8) == 0xf0) {
		continuation_count = 3;
		code_point = first & 0x07;
		minimum = 0x10000;
	} else {
		return false;
	}
	if (continuation_count >= end - index) {
		return false;
	}
	for (std::size_t offset = 0; offset < continuation_count; offset++) {
		const auto continuation = static_cast<unsigned char>(value[++index]);
		if ((continuation & 0xc0) != 0x80) {
			return false;
		}
		code_point = (code_point << 6) | (continuation & 0x3f);
	}
	if (code_point < minimum || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
		return false;
	}
	decoded_bytes += continuation_count + 1;
	return true;
}

bool IsValidBoundedCursorBytes(const std::string &cursor) noexcept {
	if (cursor.empty() || cursor.size() > 512) {
		return false;
	}
	uint64_t decoded_bytes = 0;
	for (std::size_t index = 0; index < cursor.size(); index++) {
		if (!ConsumeUtf8(cursor, index, cursor.size(), decoded_bytes)) {
			return false;
		}
	}
	return decoded_bytes <= 512;
}

uint8_t LowerHexValue(char value) noexcept {
	return value >= '0' && value <= '9' ? static_cast<uint8_t>(value - '0') : static_cast<uint8_t>(value - 'a' + 10);
}

bool IsCanonicalCursorToken(const std::string &body, std::size_t begin, std::size_t size) noexcept {
	if (size <= 2 || body[begin] != '"' || body[begin + size - 1] != '"') {
		return false;
	}
	const auto end = begin + size - 1;
	uint64_t decoded_bytes = 0;
	for (std::size_t index = begin + 1; index < end; index++) {
		const auto byte = static_cast<unsigned char>(body[index]);
		if (byte < 0x20 || body[index] == '"') {
			return false;
		}
		if (body[index] != '\\') {
			if (!ConsumeUtf8(body, index, end, decoded_bytes) || decoded_bytes > 512) {
				return false;
			}
			continue;
		}
		if (++index >= end) {
			return false;
		}
		const auto escaped = body[index];
		if (escaped == '"' || escaped == '\\' || escaped == 'b' || escaped == 'f' || escaped == 'n' || escaped == 'r' ||
		    escaped == 't') {
			if (++decoded_bytes > 512) {
				return false;
			}
			continue;
		}
		if (escaped != 'u' || index + 4 >= end || body[index + 1] != '0' || body[index + 2] != '0' ||
		    !IsLowerHex(body[index + 3]) || !IsLowerHex(body[index + 4])) {
			return false;
		}
		const auto control =
		    static_cast<uint8_t>((LowerHexValue(body[index + 3]) << 4) | LowerHexValue(body[index + 4]));
		if (control >= 0x20 || control == '\b' || control == '\f' || control == '\n' || control == '\r' ||
		    control == '\t') {
			return false;
		}
		if (++decoded_bytes > 512) {
			return false;
		}
		index += 4;
	}
	return decoded_bytes > 0;
}

} // namespace

HttpRequest BuildAdmittedGraphqlRequest(const AdmittedGraphqlRequestProfile &profile, const std::string *cursor) {
	try {
		if (cursor && !IsValidBoundedCursorBytes(*cursor)) {
			throw ExecutionError(ErrorStage::POLICY, "pagination.cursor",
			                     "GraphQL continuation cursor is invalid or exceeds its byte budget");
		}
		std::string body;
		body.reserve(profile.Document().size() + (cursor ? cursor->size() : 0) + 80);
		body += "{\"query\":";
		AppendJsonString(profile.Document(), body);
		body += ",\"variables\":{\"pageSize\":" + std::to_string(profile.PageSize()) + ",\"cursor\":";
		if (cursor) {
			AppendJsonString(*cursor, body);
		} else {
			body += "null";
		}
		body += "}}";
		if (body.empty() || static_cast<uint64_t>(body.size()) > profile.MaxRequestBodyBytes()) {
			throw ExecutionError(ErrorStage::RESOURCE, "request_body_bytes",
			                     "GraphQL request exceeded its serialized-body budget");
		}

		HttpRequest request;
		request.method = profile.Method();
		request.scheme = profile.Scheme();
		request.host = profile.Host();
		request.port = profile.Port();
		request.target = profile.Path();
		request.headers = profile.Headers();
		request.body = std::move(body);
		request.content_type = "application/json";
		return request;
	} catch (const ExecutionError &) {
		throw;
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "request_body_bytes",
		                     "GraphQL request could not be allocated within its body budget");
	}
}

bool IsCanonicalAdmittedGraphqlBody(const std::string &body) noexcept {
	try {
		std::string prefix = "{\"query\":";
		AppendJsonString(CanonicalGraphqlDocumentBytes(), prefix);
		prefix += ",\"variables\":{\"pageSize\":100,\"cursor\":";
		if (body.compare(0, prefix.size(), prefix) != 0 || body.size() <= prefix.size() + 2 ||
		    body.compare(body.size() - 2, 2, "}}") != 0) {
			return false;
		}
		const auto cursor_size = body.size() - prefix.size() - 2;
		if (cursor_size == 4 && body.compare(prefix.size(), cursor_size, "null") == 0) {
			return true;
		}
		return IsCanonicalCursorToken(body, prefix.size(), cursor_size);
	} catch (...) {
		return false;
	}
}

} // namespace internal
} // namespace duckdb_api
