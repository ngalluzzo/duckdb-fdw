#include "duckdb_api/contracts.hpp"

#include "duckdb/common/exception.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

ExecutionError::ExecutionError(ErrorStage stage_p, std::string field_p, std::string safe_message_p)
    : stage(stage_p), field(std::move(field_p)), safe_message(std::move(safe_message_p)) {
}

const char *ExecutionError::what() const noexcept {
	return safe_message.c_str();
}

ErrorStage ExecutionError::Stage() const {
	return stage;
}

const std::string &ExecutionError::Field() const {
	return field;
}

const std::string &ExecutionError::SafeMessage() const {
	return safe_message;
}

FixtureReadBuffer::FixtureReadBuffer(duckdb::ClientContext &context_p, FixtureSource &source_p,
                                     duckdb::idx_t max_bytes_p, std::chrono::steady_clock::time_point deadline_p)
    : context(context_p), source(source_p), max_bytes(max_bytes_p), deadline(deadline_p) {
}

void FixtureReadBuffer::Checkpoint() {
	if (context.IsInterrupted()) {
		source.OnInterruption();
		throw duckdb::InterruptException();
	}
	if (std::chrono::steady_clock::now() >= deadline) {
		throw ExecutionError(ErrorStage::POLICY, "", "execution exceeds the wall-time budget");
	}
}

void FixtureReadBuffer::Append(const std::string &chunk) {
	Checkpoint();
	if (chunk.size() > max_bytes - contents.size()) {
		throw ExecutionError(ErrorStage::POLICY, "", "response exceeds the fixture-byte budget");
	}
	contents.append(chunk);
}

const std::string &FixtureReadBuffer::Contents() const {
	return contents;
}

namespace {

void CheckExecution(duckdb::ClientContext &context, FixtureSource &source,
                    std::chrono::steady_clock::time_point deadline) {
	if (context.IsInterrupted()) {
		source.OnInterruption();
		throw duckdb::InterruptException();
	}
	if (std::chrono::steady_clock::now() >= deadline) {
		throw ExecutionError(ErrorStage::POLICY, "", "execution exceeds the wall-time budget");
	}
}

bool TryParseLosslessInt64(const std::string &token, int64_t &result) {
	auto position = duckdb::idx_t(0);
	const auto negative = token[position] == '-';
	if (negative) {
		position++;
	}

	std::string digits;
	while (position < token.size() && token[position] >= '0' && token[position] <= '9') {
		digits.push_back(token[position++]);
	}
	auto fractional_digits = int64_t(0);
	if (position < token.size() && token[position] == '.') {
		position++;
		const auto fractional_begin = digits.size();
		while (position < token.size() && token[position] >= '0' && token[position] <= '9') {
			digits.push_back(token[position++]);
		}
		fractional_digits = static_cast<int64_t>(digits.size() - fractional_begin);
	}

	auto exponent = int64_t(0);
	if (position < token.size() && (token[position] == 'e' || token[position] == 'E')) {
		position++;
		const auto exponent_negative = position < token.size() && token[position] == '-';
		if (position < token.size() && (token[position] == '+' || token[position] == '-')) {
			position++;
		}
		const auto exponent_limit = int64_t(1000000);
		while (position < token.size()) {
			const auto digit = static_cast<int64_t>(token[position++] - '0');
			if (exponent <= (exponent_limit - digit) / 10) {
				exponent = exponent * 10 + digit;
			} else {
				exponent = exponent_limit;
			}
		}
		if (exponent_negative) {
			exponent = -exponent;
		}
	}

	const auto first_nonzero = digits.find_first_not_of('0');
	if (first_nonzero == std::string::npos) {
		result = 0;
		return true;
	}
	digits.erase(0, first_nonzero);
	const auto scale = exponent - fractional_digits;
	if (scale < 0) {
		const auto removed_digits = static_cast<uint64_t>(-scale);
		if (removed_digits > digits.size()) {
			return false;
		}
		const auto retained_digits = digits.size() - static_cast<duckdb::idx_t>(removed_digits);
		if (digits.find_first_not_of('0', retained_digits) != std::string::npos) {
			return false;
		}
		digits.resize(retained_digits);
		const auto retained_nonzero = digits.find_first_not_of('0');
		if (retained_nonzero == std::string::npos) {
			result = 0;
			return true;
		}
		digits.erase(0, retained_nonzero);
	} else {
		if (scale > 19 || digits.size() + static_cast<duckdb::idx_t>(scale) > 19) {
			return false;
		}
		digits.append(static_cast<duckdb::idx_t>(scale), '0');
	}

	const std::string limit = negative ? "9223372036854775808" : "9223372036854775807";
	if (digits.size() > limit.size() || (digits.size() == limit.size() && digits > limit)) {
		return false;
	}
	errno = 0;
	char *end = nullptr;
	const auto magnitude = std::strtoull(digits.c_str(), &end, 10);
	if (errno == ERANGE || end == nullptr || *end != '\0') {
		return false;
	}
	if (negative && magnitude == uint64_t(9223372036854775808ULL)) {
		result = std::numeric_limits<int64_t>::min();
	} else {
		result = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
	}
	return true;
}

class JsonParser {
public:
	JsonParser(const std::string &input_p, duckdb::ClientContext &context_p, FixtureSource &source_p,
	           const ResourceBudgets &budgets_p, std::chrono::steady_clock::time_point deadline_p)
	    : input(input_p), context(context_p), source(source_p), budgets(budgets_p), deadline(deadline_p), position(0) {
	}

	std::vector<ItemRow> ParseResponse() {
		ValidateDocument();
		SkipWhitespace();
		if (Peek() != '{') {
			throw ExecutionError(ErrorStage::SCHEMA, "items", "response extractor requires an object");
		}
		Expect('{');
		SkipWhitespace();
		bool found_items = false;
		std::vector<ItemRow> result;
		while (Peek() != '}') {
			if (Peek() != '"') {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			const auto key = ParseString();
			SkipWhitespace();
			Expect(':');
			SkipWhitespace();
			if (key == "items") {
				if (found_items) {
					throw ExecutionError(ErrorStage::SCHEMA, "items", "response extractor matched a duplicate field");
				}
				found_items = true;
				if (Peek() != '[') {
					SkipValue();
					throw ExecutionError(ErrorStage::SCHEMA, "items", "response extractor did not produce an array");
				}
				result = ParseItems();
			} else {
				SkipValue();
			}
			SkipWhitespace();
			if (Peek() == ',') {
				position++;
				SkipWhitespace();
				if (Peek() == '}') {
					throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
				}
				continue;
			}
			break;
		}
		Expect('}');
		SkipWhitespace();
		if (position != input.size()) {
			throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
		}
		if (!found_items) {
			throw ExecutionError(ErrorStage::SCHEMA, "items", "response extractor did not match the required field");
		}
		return result;
	}

private:
	struct RecordBuilder {
		RecordBuilder() : id(0), active(false), has_id(false), has_name(false), has_active(false) {
		}

		int64_t id;
		std::string name;
		bool active;
		bool has_id;
		bool has_name;
		bool has_active;
	};

	void ValidateDocument() {
		SkipWhitespace();
		SkipValue();
		SkipWhitespace();
		if (position != input.size()) {
			throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
		}
		position = 0;
	}

	void SkipWhitespace() {
		while (position < input.size()) {
			CheckExecution(context, source, deadline);
			const auto character = input[position];
			if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
				break;
			}
			position++;
		}
	}

	char Peek() const {
		if (position >= input.size()) {
			return '\0';
		}
		return input[position];
	}

	void Expect(char expected) {
		CheckExecution(context, source, deadline);
		if (position >= input.size() || input[position] != expected) {
			throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
		}
		position++;
	}

	std::string ParseString() {
		Expect('"');
		std::string result;
		while (position < input.size()) {
			CheckExecution(context, source, deadline);
			const auto character = input[position++];
			if (character == '"') {
				return result;
			}
			if (static_cast<unsigned char>(character) < 0x20) {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			if (character != '\\') {
				AppendValidatedUtf8(character, result);
				continue;
			}
			if (position >= input.size()) {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
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
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
		}
		throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
	}

	uint32_t ParseHexCodeUnit() {
		uint32_t result = 0;
		for (duckdb::idx_t index = 0; index < 4; index++) {
			CheckExecution(context, source, deadline);
			if (position >= input.size()) {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
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
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
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
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			position += 2;
			const auto low = ParseHexCodeUnit();
			if (low < 0xdc00 || low > 0xdfff) {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
		} else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
			throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
		}
		AppendCodePoint(code_point, result);
	}

	void AppendValidatedUtf8(char first_character, std::string &result) {
		const auto first = static_cast<unsigned char>(first_character);
		if (first < 0x80) {
			result.push_back(first_character);
			return;
		}
		duckdb::idx_t continuation_count = 0;
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
			throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
		}
		const auto begin = position - 1;
		for (duckdb::idx_t index = 0; index < continuation_count; index++) {
			CheckExecution(context, source, deadline);
			if (position >= input.size()) {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			const auto continuation = static_cast<unsigned char>(input[position++]);
			if ((continuation & 0xc0) != 0x80) {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			code_point = (code_point << 6) | (continuation & 0x3f);
		}
		if (code_point < minimum || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
			throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
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
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
		} else {
			if (Peek() < '1' || Peek() > '9') {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			while (Peek() >= '0' && Peek() <= '9') {
				CheckExecution(context, source, deadline);
				position++;
			}
		}
		if (Peek() == '.') {
			position++;
			if (Peek() < '0' || Peek() > '9') {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			while (Peek() >= '0' && Peek() <= '9') {
				CheckExecution(context, source, deadline);
				position++;
			}
		}
		if (Peek() == 'e' || Peek() == 'E') {
			position++;
			if (Peek() == '+' || Peek() == '-') {
				position++;
			}
			if (Peek() < '0' || Peek() > '9') {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			while (Peek() >= '0' && Peek() <= '9') {
				CheckExecution(context, source, deadline);
				position++;
			}
		}
		return input.substr(begin, position - begin);
	}

	void ParseLiteral(const char *literal) {
		for (duckdb::idx_t index = 0; literal[index] != '\0'; index++) {
			Expect(literal[index]);
		}
	}

	void SkipValue(duckdb::idx_t depth = 0) {
		SkipWhitespace();
		if (depth > budgets.json_nesting) {
			throw ExecutionError(ErrorStage::POLICY, "", "response exceeds the JSON nesting budget");
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
				SkipWhitespace();
				if (Peek() == ',') {
					position++;
					SkipWhitespace();
					if (Peek() == ']') {
						throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
					}
					continue;
				}
				break;
			}
			Expect(']');
			return;
		}
		if (Peek() == '{') {
			Expect('{');
			SkipWhitespace();
			while (Peek() != '}') {
				if (Peek() != '"') {
					throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
				}
				ParseString();
				SkipWhitespace();
				Expect(':');
				SkipValue(depth + 1);
				SkipWhitespace();
				if (Peek() == ',') {
					position++;
					SkipWhitespace();
					if (Peek() == '}') {
						throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
					}
					continue;
				}
				break;
			}
			Expect('}');
			return;
		}
		throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
	}

	int64_t ParseId() {
		SkipWhitespace();
		if (Peek() != '-' && (Peek() < '0' || Peek() > '9')) {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, "id", "field id cannot be converted to BIGINT");
		}
		const auto token = ParseNumberToken();
		int64_t value = 0;
		if (!TryParseLosslessInt64(token, value)) {
			throw ExecutionError(ErrorStage::SCHEMA, "id", "field id cannot be converted to BIGINT");
		}
		return value;
	}

	std::string ParseName() {
		SkipWhitespace();
		if (Peek() != '"') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, "name", "field name cannot be converted to VARCHAR");
		}
		auto result = ParseString();
		if (result.size() > budgets.name_bytes) {
			throw ExecutionError(ErrorStage::POLICY, "name", "field name exceeds the configured resource budget");
		}
		return result;
	}

	bool ParseActive() {
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
		throw ExecutionError(ErrorStage::SCHEMA, "active", "field active cannot be converted to BOOLEAN");
	}

	ItemRow ParseItem() {
		RecordBuilder builder;
		if (Peek() != '{') {
			SkipValue();
			throw ExecutionError(ErrorStage::SCHEMA, "", "response item does not match the declared row shape");
		}
		Expect('{');
		SkipWhitespace();
		while (Peek() != '}') {
			if (Peek() != '"') {
				throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
			}
			const auto key = ParseString();
			SkipWhitespace();
			Expect(':');
			if (key == "id") {
				if (builder.has_id) {
					throw ExecutionError(ErrorStage::SCHEMA, "id", "field id is duplicated");
				}
				builder.id = ParseId();
				builder.has_id = true;
			} else if (key == "name") {
				if (builder.has_name) {
					throw ExecutionError(ErrorStage::SCHEMA, "name", "field name is duplicated");
				}
				builder.name = ParseName();
				builder.has_name = true;
			} else if (key == "active") {
				if (builder.has_active) {
					throw ExecutionError(ErrorStage::SCHEMA, "active", "field active is duplicated");
				}
				builder.active = ParseActive();
				builder.has_active = true;
			} else {
				SkipValue();
			}
			SkipWhitespace();
			if (Peek() == ',') {
				position++;
				SkipWhitespace();
				if (Peek() == '}') {
					throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
				}
				continue;
			}
			break;
		}
		Expect('}');
		if (!builder.has_id) {
			throw ExecutionError(ErrorStage::SCHEMA, "id", "required field id is missing");
		}
		if (!builder.has_name) {
			throw ExecutionError(ErrorStage::SCHEMA, "name", "required field name is missing");
		}
		if (!builder.has_active) {
			throw ExecutionError(ErrorStage::SCHEMA, "active", "required field active is missing");
		}
		ItemRow result;
		result.id = builder.id;
		result.name = std::move(builder.name);
		result.active = builder.active;
		return result;
	}

	std::vector<ItemRow> ParseItems() {
		std::vector<ItemRow> result;
		Expect('[');
		SkipWhitespace();
		while (Peek() != ']') {
			CheckExecution(context, source, deadline);
			if (result.size() >= budgets.decoded_records) {
				throw ExecutionError(ErrorStage::POLICY, "", "response exceeds the decoded-record budget");
			}
			result.push_back(ParseItem());
			SkipWhitespace();
			if (Peek() == ',') {
				position++;
				SkipWhitespace();
				if (Peek() == ']') {
					throw ExecutionError(ErrorStage::DECODE, "", "response is not valid JSON");
				}
				continue;
			}
			break;
		}
		Expect(']');
		return result;
	}

	const std::string &input;
	duckdb::ClientContext &context;
	FixtureSource &source;
	const ResourceBudgets &budgets;
	std::chrono::steady_clock::time_point deadline;
	duckdb::idx_t position;
};

void ValidateFixturePlan(const ScanPlan &plan) {
	const std::vector<std::string> expected_columns = {"id", "name", "active"};
	const std::vector<std::string> expected_ownership = {"filter", "ordering", "limit", "offset"};
	if (plan.operation_name != "items_list" || plan.executor_name != "fixture_rest" || plan.method != "GET" ||
	    plan.path != "/items" || plan.extractor != "$.items[*]" || plan.fixture_digest.empty() ||
	    plan.output_columns != expected_columns || plan.remote_predicate != "TRUE" ||
	    plan.runtime_residual_predicate != "TRUE" || !plan.remote_ordering.empty() || !plan.runtime_ordering.empty() ||
	    plan.has_remote_limit || plan.has_remote_offset || plan.has_runtime_limit || plan.has_runtime_offset ||
	    plan.duckdb_owned_operations != expected_ownership || plan.pagination_enabled || plan.providers_enabled ||
	    plan.retry_enabled || plan.cache_enabled || plan.network_enabled || !plan.budgets.IsPreviewBudget()) {
		throw ExecutionError(ErrorStage::POLICY, "", "scan plan is not authorized for fixture execution");
	}
}

class FixtureBatchStream : public BatchStream {
public:
	FixtureBatchStream(ScanPlan plan_p, std::unique_ptr<FixtureSource> source_p)
	    : plan(std::move(plan_p)), source(std::move(source_p)), offset(0), loaded(false), cancelled(false),
	      closed(false), started(std::chrono::steady_clock::now()),
	      deadline(started + std::chrono::milliseconds(plan.budgets.wall_milliseconds)) {
		if (!source || source->ContentDigest() != plan.fixture_digest) {
			throw ExecutionError(ErrorStage::POLICY, "", "fixture identity does not match the immutable scan plan");
		}
		source->OnStreamOpen();
	}

	~FixtureBatchStream() override {
		Close();
	}

	bool Next(duckdb::ClientContext &context, std::vector<ItemRow> &output) override {
		output.clear();
		CheckCancellation(context);
		CheckWallTime();
		if (!loaded) {
			FixtureReadBuffer buffer(context, *source, plan.budgets.fixture_bytes, deadline);
			source->Read(buffer);
			buffer.Checkpoint();
			JsonParser parser(buffer.Contents(), context, *source, plan.budgets, deadline);
			rows = parser.ParseResponse();
			CheckWallTime();
			loaded = true;
		}
		CheckCancellation(context);
		if (offset >= rows.size()) {
			return false;
		}
		const auto remaining = rows.size() - offset;
		const auto count = std::min<duckdb::idx_t>(plan.budgets.batch_rows, remaining);
		output.reserve(count);
		for (duckdb::idx_t index = 0; index < count; index++) {
			CheckCancellation(context);
			output.push_back(rows[offset + index]);
		}
		offset += count;
		source->OnBatch(count);
		return true;
	}

	void Cancel() override {
		cancelled = true;
	}

	void Close() override {
		if (closed) {
			return;
		}
		closed = true;
		if (source) {
			source->OnStreamClose();
		}
		rows.clear();
	}

private:
	void CheckCancellation(duckdb::ClientContext &context) {
		if (cancelled) {
			source->OnInterruption();
			throw duckdb::InterruptException();
		}
		CheckExecution(context, *source, deadline);
	}

	void CheckWallTime() const {
		if (std::chrono::steady_clock::now() >= deadline) {
			throw ExecutionError(ErrorStage::POLICY, "", "execution exceeds the wall-time budget");
		}
	}

	ScanPlan plan;
	std::unique_ptr<FixtureSource> source;
	std::vector<ItemRow> rows;
	duckdb::idx_t offset;
	bool loaded;
	bool cancelled;
	bool closed;
	std::chrono::steady_clock::time_point started;
	std::chrono::steady_clock::time_point deadline;
};

} // namespace

std::string CompiledConnector::Snapshot() const {
	return "connector=" + connector_name + ";version=" + version + ";relation=" + relation_name +
	       ";"
	       "schema=id:BIGINT!:$.id,name:VARCHAR!:$.name,active:BOOLEAN!:$.active;"
	       "operation=" +
	       operation_name + ":fallback:many:REST:" + method + ":" + path + ":" + extractor +
	       ";fixture=" + fixture_digest;
}

bool AdapterCapabilities::IsConservativePreview() const {
	return !projection && !filter && !ordering && !limit && !offset && !progress && cancellation && !secrets;
}

bool ResourceBudgets::IsPreviewBudget() const {
	return fixture_bytes == MAX_FIXTURE_BYTES && decoded_records == MAX_DECODED_RECORDS &&
	       name_bytes == MAX_NAME_BYTES && json_nesting == MAX_JSON_NESTING && batch_rows == OUTPUT_BATCH_ROWS &&
	       wall_milliseconds == MAX_EXECUTION_MILLISECONDS && concurrency == 1;
}

std::string ScanRequest::Snapshot() const {
	std::ostringstream result;
	result << "connector=" << connector_name << ";relation=" << relation_name
	       << ";inputs=" << (explicit_inputs.empty() ? "[]" : "unexpected") << ";projection=";
	for (duckdb::idx_t index = 0; index < projected_columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << projected_columns[index];
	}
	result << ";predicate=" << predicate << ";ordering=" << (orderings.empty() ? "[]" : "unexpected")
	       << ";limit=" << (has_limit ? "set" : "unset") << ";offset=" << (has_offset ? "set" : "unset")
	       << ";capabilities=projection:" << (capabilities.projection ? "available" : "unavailable")
	       << ",filter:" << (capabilities.filter ? "available" : "unavailable")
	       << ",ordering:" << (capabilities.ordering ? "available" : "unavailable")
	       << ",limit:" << (capabilities.limit ? "available" : "unavailable")
	       << ",offset:" << (capabilities.offset ? "available" : "unavailable")
	       << ",progress:" << (capabilities.progress ? "available" : "unavailable")
	       << ",cancellation:" << (capabilities.cancellation ? "verified" : "unavailable")
	       << ",secrets:" << (capabilities.secrets ? "available" : "unavailable");
	return result.str();
}

std::string ScanPlan::Snapshot() const {
	std::ostringstream result;
	result << "operation=" << operation_name << ";executor=" << executor_name << ";method=" << method
	       << ";path=" << path << ";extractor=" << extractor << ";fixture=" << fixture_digest << ";projection=";
	for (duckdb::idx_t index = 0; index < output_columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << output_columns[index];
	}
	result << ";remote_predicate=" << remote_predicate << ";runtime_residual=" << runtime_residual_predicate
	       << ";duckdb_owns=";
	for (duckdb::idx_t index = 0; index < duckdb_owned_operations.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << duckdb_owned_operations[index];
	}
	result << ";remote_ordering=" << (remote_ordering.empty() ? "[]" : "unexpected")
	       << ";runtime_ordering=" << (runtime_ordering.empty() ? "[]" : "unexpected")
	       << ";remote_limit=" << (has_remote_limit ? "set" : "unset")
	       << ";remote_offset=" << (has_remote_offset ? "set" : "unset")
	       << ";runtime_limit=" << (has_runtime_limit ? "set" : "unset")
	       << ";runtime_offset=" << (has_runtime_offset ? "set" : "unset")
	       << ";pagination=" << (pagination_enabled ? "enabled" : "disabled")
	       << ";providers=" << (providers_enabled ? "enabled" : "disabled")
	       << ";retry=" << (retry_enabled ? "enabled" : "disabled")
	       << ";cache=" << (cache_enabled ? "enabled" : "disabled")
	       << ";network=" << (network_enabled ? "enabled" : "disabled")
	       << ";budgets=fixture_bytes:" << budgets.fixture_bytes << ",records:" << budgets.decoded_records
	       << ",name_bytes:" << budgets.name_bytes << ",json_nesting:" << budgets.json_nesting
	       << ",batch_rows:" << budgets.batch_rows << ",wall_ms:" << budgets.wall_milliseconds
	       << ",concurrency:" << budgets.concurrency;
	return result.str();
}

CompiledConnector BuildCompiledConnector(const std::string &fixture_digest) {
	CompiledConnector result;
	result.connector_name = "example";
	result.version = "0.1.0";
	result.relation_name = "items";
	result.operation_name = "items_list";
	result.method = "GET";
	result.path = "/items";
	result.extractor = "$.items[*]";
	result.fixture_digest = fixture_digest;
	return result;
}

ScanRequest BuildConservativeScanRequest() {
	ScanRequest result;
	result.connector_name = "example";
	result.relation_name = "items";
	result.explicit_inputs.clear();
	result.projected_columns = {"id", "name", "active"};
	result.predicate = "TRUE";
	result.has_limit = false;
	result.has_offset = false;
	result.capabilities = {false, false, false, false, false, false, true, false};
	return result;
}

ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request) {
	const std::vector<std::string> expected_projection = {"id", "name", "active"};
	if (connector.connector_name != "example" || connector.version != "0.1.0" || connector.relation_name != "items" ||
	    connector.operation_name != "items_list" || connector.method != "GET" || connector.path != "/items" ||
	    connector.extractor != "$.items[*]") {
		throw std::logic_error("native preview received unsupported compiled connector metadata");
	}
	if (request.connector_name != connector.connector_name || request.relation_name != connector.relation_name ||
	    !request.explicit_inputs.empty() || request.projected_columns != expected_projection ||
	    request.predicate != "TRUE" || !request.orderings.empty() || request.has_limit || request.has_offset ||
	    !request.capabilities.IsConservativePreview()) {
		throw std::logic_error("native preview received a non-conservative scan request");
	}
	ScanPlan result;
	result.operation_name = connector.operation_name;
	result.executor_name = "fixture_rest";
	result.method = connector.method;
	result.path = connector.path;
	result.extractor = connector.extractor;
	result.fixture_digest = connector.fixture_digest;
	result.output_columns = request.projected_columns;
	result.remote_predicate = "TRUE";
	result.runtime_residual_predicate = "TRUE";
	result.has_remote_limit = false;
	result.has_remote_offset = false;
	result.has_runtime_limit = false;
	result.has_runtime_offset = false;
	result.duckdb_owned_operations = {"filter", "ordering", "limit", "offset"};
	result.pagination_enabled = false;
	result.providers_enabled = false;
	result.retry_enabled = false;
	result.cache_enabled = false;
	result.network_enabled = false;
	result.budgets = {MAX_FIXTURE_BYTES,
	                  MAX_DECODED_RECORDS,
	                  MAX_NAME_BYTES,
	                  MAX_JSON_NESTING,
	                  OUTPUT_BATCH_ROWS,
	                  MAX_EXECUTION_MILLISECONDS,
	                  1};
	return result;
}

FixtureSource::~FixtureSource() {
}

void FixtureSource::OnStreamOpen() {
}

void FixtureSource::OnBatch(duckdb::idx_t) {
}

void FixtureSource::OnInterruption() {
}

void FixtureSource::OnStreamClose() {
}

FixtureFactory::~FixtureFactory() {
}

BatchStream::~BatchStream() {
}

std::unique_ptr<BatchStream> OpenBatchStream(const ScanPlan &plan, const FixtureFactory &factory) {
	ValidateFixturePlan(plan);
	if (factory.ContentDigest() != plan.fixture_digest) {
		throw ExecutionError(ErrorStage::POLICY, "", "fixture identity does not match the immutable scan plan");
	}
	return std::unique_ptr<BatchStream>(new FixtureBatchStream(plan, factory.Open()));
}

} // namespace duckdb_api
