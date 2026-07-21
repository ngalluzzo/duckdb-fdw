#include "runtime/support/package_fixture_json_variant_internal.hpp"

#include "duckdb_api/internal/runtime/decoding/strict_json_reader.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace duckdb_api_test {
namespace internal {
namespace {

using Reader = duckdb_api::internal::StrictJsonReader;

struct JsonSpan {
	std::size_t begin;
	std::size_t end;
	std::size_t erase_begin;
	std::size_t erase_end;
};

duckdb_api::internal::HttpExecutionProfile PublicFixtureProfile() {
	return {duckdb_api::PlannedUrlScheme::HTTPS,
	        "",
	        0,
	        false,
	        false,
	        false,
	        duckdb_api::PAGINATION_MAX_EXECUTION_MILLISECONDS,
	        duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE};
}

bool FindObjectPath(Reader &reader, const std::vector<std::string> &path, std::size_t path_index, JsonSpan &span) {
	reader.SkipWhitespace();
	if (reader.Peek() != '{') {
		reader.SkipValue();
		return false;
	}
	reader.Expect('{');
	reader.SkipWhitespace();
	std::size_t previous_separator = std::numeric_limits<std::size_t>::max();
	bool found = false;
	bool selected_member_seen = false;
	while (reader.Peek() != '}') {
		const auto member_begin = reader.Position();
		reader.RequireObjectKey();
		const auto key = reader.ParseObjectKey();
		reader.SkipWhitespace();
		reader.Expect(':');
		reader.SkipWhitespace();
		const auto value_begin = reader.Position();
		if (key.Equals(path[path_index])) {
			if (selected_member_seen) {
				throw std::invalid_argument("closed Runtime fixture base response has an ambiguous selected path");
			}
			selected_member_seen = true;
			if (path_index + 1 < path.size()) {
				JsonSpan nested;
				if (FindObjectPath(reader, path, path_index + 1, nested)) {
					span = nested;
					found = true;
				}
			} else {
				reader.SkipValue();
				const auto value_end = reader.Position();
				reader.SkipWhitespace();
				const auto after_value = reader.Position();
				JsonSpan candidate {value_begin, value_end, member_begin, after_value};
				if (reader.Peek() == ',') {
					candidate.erase_end = after_value + 1;
				} else if (previous_separator != std::numeric_limits<std::size_t>::max()) {
					candidate.erase_begin = previous_separator;
				}
				span = candidate;
				found = true;
			}
		} else {
			reader.SkipValue();
		}
		reader.SkipWhitespace();
		if (reader.Peek() == ',') {
			previous_separator = reader.Position();
			reader.ObjectSeparator();
		} else if (reader.Peek() != '}') {
			throw std::invalid_argument("controlled fixture JSON object has an invalid separator");
		}
	}
	reader.Expect('}');
	return found;
}

bool FindPath(const std::vector<std::string> &path, Reader &reader, JsonSpan &span) {
	reader.Reset();
	reader.SkipWhitespace();
	if (path.empty()) {
		span.begin = reader.Position();
		reader.SkipValue();
		span.end = reader.Position();
		span.erase_begin = span.begin;
		span.erase_end = span.end;
		return true;
	}
	return FindObjectPath(reader, path, 0, span);
}

bool FindFirstArrayElement(Reader &reader, const JsonSpan &array, JsonSpan &element) {
	reader.SetPosition(array.begin);
	reader.SkipWhitespace();
	if (reader.Peek() != '[') {
		return false;
	}
	reader.Expect('[');
	reader.SkipWhitespace();
	if (reader.Peek() == ']') {
		return false;
	}
	element.begin = reader.Position();
	reader.SkipValue();
	element.end = reader.Position();
	element.erase_begin = element.begin;
	element.erase_end = element.end;
	return true;
}

std::string ReplaceSpan(const std::string &body, const JsonSpan &span, const std::string &replacement,
                        bool remove_member) {
	const auto begin = remove_member ? span.erase_begin : span.begin;
	const auto end = remove_member ? span.erase_end : span.end;
	if (begin > end || end > body.size()) {
		throw std::logic_error("controlled fixture JSON mutation produced an invalid span");
	}
	std::string result;
	result.reserve(body.size() - (end - begin) + replacement.size());
	result.append(body, 0, begin);
	result += replacement;
	result.append(body, end, body.size() - end);
	return result;
}

Reader MakeReader(const std::string &body, duckdb_api::ExecutionControl &control) {
	return Reader(body, duckdb_api::PAGINATION_MAX_JSON_NESTING,
	              std::chrono::steady_clock::now() + std::chrono::seconds(5), control);
}

} // namespace

RuntimeFixtureJsonShape AdmitRuntimeFixtureJsonShape(const duckdb_api::ScanPlan &plan) {
	RuntimeFixtureJsonShape result;
	const auto profile = PublicFixtureProfile();
	if (plan.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL) {
		auto admitted = duckdb_api::internal::TryAdmitGraphqlPlan(plan, profile);
		if (!admitted) {
			throw std::invalid_argument("closed Runtime fixture GraphQL plan was not admitted");
		}
		result.records_are_array = true;
		result.records_path = admitted->NodesPath();
		for (const auto &column : admitted->Columns()) {
			result.columns.push_back({column.name, column.kind, column.nullable, column.response_path});
		}
		return result;
	}

	if (plan.Operation().Protocol() != duckdb_api::PlannedProtocol::REST) {
		throw std::invalid_argument("closed Runtime fixture plan has an unknown protocol");
	}
	if (plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::DISABLED) {
		auto admitted = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(plan, profile);
		if (!admitted) {
			throw std::invalid_argument("closed Runtime fixture REST plan was not admitted");
		}
		result.records_are_array = admitted->ResponseSource() != duckdb_api::PlannedResponseSource::ROOT_OBJECT;
		result.records_path = admitted->RecordsPath();
		for (const auto &column : admitted->Columns()) {
			result.columns.push_back({column.name, column.kind, column.nullable, column.source_path});
		}
		return result;
	}
	auto admitted = duckdb_api::internal::TryAdmitPaginatedRestPlan(plan, profile);
	if (!admitted) {
		throw std::invalid_argument("closed Runtime fixture paginated REST plan was not admitted");
	}
	result.records_are_array = true;
	result.records_path = admitted->RecordsPath();
	for (const auto &column : admitted->Columns()) {
		result.columns.push_back({column.name, column.kind, column.nullable, column.source_path});
	}
	return result;
}

std::string ReplaceFirstRuntimeFixtureColumn(const std::string &body, const RuntimeFixtureJsonShape &shape,
                                             std::size_t column_ordinal, const std::string &replacement,
                                             bool remove_member, duckdb_api::ExecutionControl &control) {
	if (column_ordinal >= shape.columns.size()) {
		throw std::invalid_argument("closed Runtime fixture column ordinal is outside the admitted schema");
	}
	auto reader = MakeReader(body, control);
	reader.ValidateDocument();
	JsonSpan record;
	if (shape.records_are_array) {
		JsonSpan records;
		if (!FindPath(shape.records_path, reader, records) || !FindFirstArrayElement(reader, records, record)) {
			throw std::invalid_argument("closed Runtime fixture base response has no first planned record");
		}
	} else {
		if (!FindPath(shape.records_path, reader, record)) {
			throw std::invalid_argument("closed Runtime fixture base response is missing its planned root record");
		}
	}
	reader.SetPosition(record.begin);
	JsonSpan column;
	if (!FindObjectPath(reader, shape.columns[column_ordinal].path, 0, column)) {
		throw std::invalid_argument("closed Runtime fixture first record is missing the planned column path");
	}
	return ReplaceSpan(body, column, replacement, remove_member);
}

std::string RepeatFirstRuntimeFixtureRecord(const std::string &body, const RuntimeFixtureJsonShape &shape,
                                            uint64_t record_count, duckdb_api::ExecutionControl &control) {
	if (!shape.records_are_array) {
		throw std::invalid_argument("closed Runtime fixture record repetition requires a collection response");
	}
	auto reader = MakeReader(body, control);
	reader.ValidateDocument();
	JsonSpan records;
	JsonSpan record;
	if (!FindPath(shape.records_path, reader, records) || !FindFirstArrayElement(reader, records, record)) {
		throw std::invalid_argument("closed Runtime fixture base response has no first planned record");
	}
	const auto value = body.substr(record.begin, record.end - record.begin);
	std::string replacement = "[";
	for (uint64_t index = 0; index < record_count; index++) {
		if (index != 0) {
			replacement.push_back(',');
		}
		replacement += value;
	}
	replacement.push_back(']');
	return ReplaceSpan(body, records, replacement, false);
}

std::string ReplaceRuntimeFixturePath(const std::string &body, const std::vector<std::string> &path,
                                      const std::string &replacement, bool remove_member,
                                      duckdb_api::ExecutionControl &control) {
	if (path.empty()) {
		throw std::invalid_argument("closed Runtime fixture path mutation requires a nonempty object path");
	}
	auto reader = MakeReader(body, control);
	reader.ValidateDocument();
	JsonSpan span;
	if (!FindPath(path, reader, span)) {
		throw std::invalid_argument("closed Runtime fixture base response is missing the selected path");
	}
	return ReplaceSpan(body, span, replacement, remove_member);
}

} // namespace internal
} // namespace duckdb_api_test
