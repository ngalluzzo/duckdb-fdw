#include "live_rest/internal/json_decoder.hpp"

#include <cerrno>
#include <cstdlib>
#include <utility>

namespace live_rest {
namespace internal {
namespace {

const uint64_t MAX_JSON_NESTING = 32;

void CheckCancelled(const CancellationView &cancellation) {
	if (cancellation.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
}

struct SchemaMapping {
	std::string response_array_field;
	std::string id_field;
	std::string login_field;
	std::string site_admin_field;
};

SchemaMapping BuildSchemaMapping(const LiveScanPlan &plan) {
	SchemaMapping result;
	result.response_array_field = plan.response_array_field;
	for (std::size_t index = 0; index < plan.columns.size(); index++) {
		const auto &column = plan.columns[index];
		if (column.name == "id" && column.type == ColumnType::BIGINT) {
			result.id_field = column.json_field;
		} else if (column.name == "login" && column.type == ColumnType::VARCHAR) {
			result.login_field = column.json_field;
		} else if (column.name == "site_admin" && column.type == ColumnType::BOOLEAN) {
			result.site_admin_field = column.json_field;
		}
	}
	return result;
}

// Strict single-document decoder for the three-column proof relation. It
// validates the complete JSON document before extraction, so malformed ignored
// fields are still rejected. Unrelated fields are skipped, while the plan's
// three required JSON fields are non-null, unique, and exactly typed. Parsed
// rows and strings are bounded by the immutable plan.
class JsonParser {
public:
	JsonParser(const std::string &input_p, const SchemaMapping &mapping_p, uint64_t max_records_p,
	           uint64_t max_string_bytes_p, const CancellationView &cancellation_p)
	    : input(input_p), mapping(mapping_p), max_records(max_records_p), max_string_bytes(max_string_bytes_p),
	      cancellation(cancellation_p), position(0) {
	}

	std::vector<LiveRow> ParseResponse() {
		ValidateDocument();
		SkipWhitespace();
		if (Peek() != '{') {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.response_array_field,
			                   "live REST response root does not match the declared schema");
		}

		Expect('{');
		SkipWhitespace();
		bool found_items = false;
		std::vector<LiveRow> result;
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseString();
			SkipWhitespace();
			Expect(':');
			SkipWhitespace();
			if (key == mapping.response_array_field) {
				if (found_items) {
					throw RuntimeError(RuntimeStage::SCHEMA, mapping.response_array_field,
					                   "required response field is duplicated");
				}
				found_items = true;
				if (Peek() != '[') {
					SkipValue();
					throw RuntimeError(RuntimeStage::SCHEMA, mapping.response_array_field,
					                   "required response field has an incompatible type");
				}
				result = ParseItems();
			} else {
				SkipValue();
			}
			ConsumeObjectSeparator();
		}
		Expect('}');
		SkipWhitespace();
		if (position != input.size()) {
			Malformed();
		}
		if (!found_items) {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.response_array_field,
			                   "required response field is missing");
		}
		return result;
	}

private:
	struct RowBuilder {
		RowBuilder() : id(0), site_admin(false), has_id(false), has_login(false), has_site_admin(false) {
		}

		int64_t id;
		std::string login;
		bool site_admin;
		bool has_id;
		bool has_login;
		bool has_site_admin;
	};

	void ValidateDocument() {
		SkipWhitespace();
		SkipValue();
		SkipWhitespace();
		if (position != input.size()) {
			Malformed();
		}
		position = 0;
	}

	void Checkpoint() const {
		CheckCancelled(cancellation);
	}

	void SkipWhitespace() {
		while (position < input.size()) {
			Checkpoint();
			const auto character = input[position];
			if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
				break;
			}
			position++;
		}
	}

	char Peek() const {
		return position < input.size() ? input[position] : '\0';
	}

	[[noreturn]] void Malformed() const {
		throw RuntimeError(RuntimeStage::DECODE, "", "live REST response is not valid JSON");
	}

	void Expect(char expected) {
		Checkpoint();
		if (position >= input.size() || input[position] != expected) {
			Malformed();
		}
		position++;
	}

	void RequireObjectKey() const {
		if (Peek() != '"') {
			Malformed();
		}
	}

	void ConsumeObjectSeparator() {
		SkipWhitespace();
		if (Peek() == ',') {
			position++;
			SkipWhitespace();
			if (Peek() == '}') {
				Malformed();
			}
			return;
		}
		if (Peek() != '}') {
			Malformed();
		}
	}

	void ConsumeArraySeparator() {
		SkipWhitespace();
		if (Peek() == ',') {
			position++;
			SkipWhitespace();
			if (Peek() == ']') {
				Malformed();
			}
			return;
		}
		if (Peek() != ']') {
			Malformed();
		}
	}

	std::string ParseString() {
		Expect('"');
		std::string result;
		while (position < input.size()) {
			Checkpoint();
			const auto character = input[position++];
			if (character == '"') {
				return result;
			}
			if (static_cast<unsigned char>(character) < 0x20) {
				Malformed();
			}
			if (character != '\\') {
				AppendValidatedUtf8(character, result);
				continue;
			}
			if (position >= input.size()) {
				Malformed();
			}
			const auto escaped = input[position++];
			switch (escaped) {
			case '"':
			case '\\':
			case '/':
				result.push_back(escaped);
				break;
			case 'b':
				result.push_back('\b');
				break;
			case 'f':
				result.push_back('\f');
				break;
			case 'n':
				result.push_back('\n');
				break;
			case 'r':
				result.push_back('\r');
				break;
			case 't':
				result.push_back('\t');
				break;
			case 'u':
				AppendEscapedUnicode(result);
				break;
			default:
				Malformed();
			}
		}
		Malformed();
	}

	uint32_t ParseHexCodeUnit() {
		uint32_t result = 0;
		for (std::size_t index = 0; index < 4; index++) {
			Checkpoint();
			if (position >= input.size()) {
				Malformed();
			}
			const auto character = input[position++];
			result <<= 4;
			if (character >= '0' && character <= '9') {
				result += static_cast<uint32_t>(character - '0');
			} else if (character >= 'a' && character <= 'f') {
				result += static_cast<uint32_t>(character - 'a' + 10);
			} else if (character >= 'A' && character <= 'F') {
				result += static_cast<uint32_t>(character - 'A' + 10);
			} else {
				Malformed();
			}
		}
		return result;
	}

	void AppendCodePoint(uint32_t code_point, std::string &result) {
		if (code_point <= 0x7f) {
			result.push_back(static_cast<char>(code_point));
		} else if (code_point <= 0x7ff) {
			result.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
			result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
		} else if (code_point <= 0xffff) {
			result.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
			result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
			result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
		} else {
			result.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
			result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
			result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
			result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
		}
	}

	void AppendEscapedUnicode(std::string &result) {
		auto code_point = ParseHexCodeUnit();
		if (code_point >= 0xd800 && code_point <= 0xdbff) {
			if (position + 2 > input.size() || input[position] != '\\' || input[position + 1] != 'u') {
				Malformed();
			}
			position += 2;
			const auto low = ParseHexCodeUnit();
			if (low < 0xdc00 || low > 0xdfff) {
				Malformed();
			}
			code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
		} else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
			Malformed();
		}
		AppendCodePoint(code_point, result);
	}

	void AppendValidatedUtf8(char first_character, std::string &result) {
		const auto first = static_cast<unsigned char>(first_character);
		if (first < 0x80) {
			result.push_back(first_character);
			return;
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
			Malformed();
		}

		const auto begin = position - 1;
		for (std::size_t index = 0; index < continuation_count; index++) {
			Checkpoint();
			if (position >= input.size()) {
				Malformed();
			}
			const auto continuation = static_cast<unsigned char>(input[position++]);
			if ((continuation & 0xc0) != 0x80) {
				Malformed();
			}
			code_point = (code_point << 6) | (continuation & 0x3f);
		}
		if (code_point < minimum || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
			Malformed();
		}
		result.append(input, begin, continuation_count + 1);
	}

	std::string ParseNumberToken() {
		const auto begin = position;
		if (Peek() == '-') {
			position++;
		}
		if (Peek() == '0') {
			position++;
			if (Peek() >= '0' && Peek() <= '9') {
				Malformed();
			}
		} else {
			if (Peek() < '1' || Peek() > '9') {
				Malformed();
			}
			while (Peek() >= '0' && Peek() <= '9') {
				Checkpoint();
				position++;
			}
		}
		if (Peek() == '.') {
			position++;
			if (Peek() < '0' || Peek() > '9') {
				Malformed();
			}
			while (Peek() >= '0' && Peek() <= '9') {
				Checkpoint();
				position++;
			}
		}
		if (Peek() == 'e' || Peek() == 'E') {
			position++;
			if (Peek() == '+' || Peek() == '-') {
				position++;
			}
			if (Peek() < '0' || Peek() > '9') {
				Malformed();
			}
			while (Peek() >= '0' && Peek() <= '9') {
				Checkpoint();
				position++;
			}
		}
		return input.substr(begin, position - begin);
	}

	void ParseLiteral(const char *literal) {
		for (std::size_t index = 0; literal[index] != '\0'; index++) {
			Expect(literal[index]);
		}
	}

	void SkipValue(uint64_t depth = 0) {
		SkipWhitespace();
		if (depth > MAX_JSON_NESTING) {
			throw RuntimeError(RuntimeStage::RESOURCE, "", "live REST response exceeds the JSON nesting limit");
		}
		if (Peek() == '"') {
			ParseString();
			return;
		}
		if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9')) {
			ParseNumberToken();
			return;
		}
		if (Peek() == 't') {
			ParseLiteral("true");
			return;
		}
		if (Peek() == 'f') {
			ParseLiteral("false");
			return;
		}
		if (Peek() == 'n') {
			ParseLiteral("null");
			return;
		}
		if (Peek() == '[') {
			Expect('[');
			SkipWhitespace();
			while (Peek() != ']') {
				SkipValue(depth + 1);
				ConsumeArraySeparator();
			}
			Expect(']');
			return;
		}
		if (Peek() == '{') {
			Expect('{');
			SkipWhitespace();
			while (Peek() != '}') {
				RequireObjectKey();
				ParseString();
				SkipWhitespace();
				Expect(':');
				SkipValue(depth + 1);
				ConsumeObjectSeparator();
			}
			Expect('}');
			return;
		}
		Malformed();
	}

	int64_t ParseId() {
		SkipWhitespace();
		if (Peek() != '-' && (Peek() < '0' || Peek() > '9')) {
			SkipValue();
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.id_field,
			                   "required response field has an incompatible type");
		}
		const auto token = ParseNumberToken();
		if (token.find_first_of(".eE") != std::string::npos) {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.id_field,
			                   "required response field has an incompatible type");
		}

		errno = 0;
		char *end = nullptr;
		const auto value = std::strtoll(token.c_str(), &end, 10);
		if (errno == ERANGE || end == nullptr || *end != '\0') {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.id_field,
			                   "required response field is outside the BIGINT range");
		}
		return static_cast<int64_t>(value);
	}

	std::string ParseLogin() {
		SkipWhitespace();
		if (Peek() != '"') {
			SkipValue();
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.login_field,
			                   "required response field has an incompatible type");
		}
		auto result = ParseString();
		if (static_cast<uint64_t>(result.size()) > max_string_bytes) {
			throw RuntimeError(RuntimeStage::RESOURCE, mapping.login_field,
			                   "required response string exceeds the configured limit");
		}
		return result;
	}

	bool ParseSiteAdmin() {
		SkipWhitespace();
		if (Peek() == 't') {
			ParseLiteral("true");
			return true;
		}
		if (Peek() == 'f') {
			ParseLiteral("false");
			return false;
		}
		SkipValue();
		throw RuntimeError(RuntimeStage::SCHEMA, mapping.site_admin_field,
		                   "required response field has an incompatible type");
	}

	LiveRow ParseItem() {
		if (Peek() != '{') {
			SkipValue();
			throw RuntimeError(RuntimeStage::SCHEMA, "", "response item does not match the declared schema");
		}

		RowBuilder builder;
		Expect('{');
		SkipWhitespace();
		while (Peek() != '}') {
			RequireObjectKey();
			const auto key = ParseString();
			SkipWhitespace();
			Expect(':');
			if (key == mapping.id_field) {
				if (builder.has_id) {
					throw RuntimeError(RuntimeStage::SCHEMA, mapping.id_field,
					                   "required response field is duplicated");
				}
				builder.id = ParseId();
				builder.has_id = true;
			} else if (key == mapping.login_field) {
				if (builder.has_login) {
					throw RuntimeError(RuntimeStage::SCHEMA, mapping.login_field,
					                   "required response field is duplicated");
				}
				builder.login = ParseLogin();
				builder.has_login = true;
			} else if (key == mapping.site_admin_field) {
				if (builder.has_site_admin) {
					throw RuntimeError(RuntimeStage::SCHEMA, mapping.site_admin_field,
					                   "required response field is duplicated");
				}
				builder.site_admin = ParseSiteAdmin();
				builder.has_site_admin = true;
			} else {
				SkipValue();
			}
			ConsumeObjectSeparator();
		}
		Expect('}');

		if (!builder.has_id) {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.id_field, "required response field is missing");
		}
		if (!builder.has_login) {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.login_field,
			                   "required response field is missing");
		}
		if (!builder.has_site_admin) {
			throw RuntimeError(RuntimeStage::SCHEMA, mapping.site_admin_field,
			                   "required response field is missing");
		}

		LiveRow result;
		result.id = builder.id;
		result.login = std::move(builder.login);
		result.site_admin = builder.site_admin;
		return result;
	}

	std::vector<LiveRow> ParseItems() {
		std::vector<LiveRow> result;
		Expect('[');
		SkipWhitespace();
		while (Peek() != ']') {
			Checkpoint();
			if (static_cast<uint64_t>(result.size()) >= max_records) {
				throw RuntimeError(RuntimeStage::RESOURCE, mapping.response_array_field,
				                   "live REST response exceeds the configured row limit");
			}
			result.push_back(ParseItem());
			ConsumeArraySeparator();
		}
		Expect(']');
		return result;
	}

	const std::string &input;
	const SchemaMapping &mapping;
	uint64_t max_records;
	uint64_t max_string_bytes;
	const CancellationView &cancellation;
	std::size_t position;
};

} // namespace

std::vector<LiveRow> DecodeResponseRows(const std::string &body, const LiveScanPlan &plan,
                                        const CancellationView &cancellation) {
	const auto mapping = BuildSchemaMapping(plan);
	JsonParser parser(body, mapping, plan.max_records, plan.max_string_bytes, cancellation);
	return parser.ParseResponse();
}

} // namespace internal
} // namespace live_rest
