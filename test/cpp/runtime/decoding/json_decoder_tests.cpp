#include "duckdb_api/internal/runtime/decoding/json_decoder.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::Require;

class ManualControl final : public duckdb_api::ExecutionControl {
public:
	ManualControl() : cancelled(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire);
	}

	void Cancel() noexcept {
		cancelled.store(true, std::memory_order_release);
	}

private:
	std::atomic<bool> cancelled;
};

duckdb_api::internal::JsonDecodePlan Plan() {
	duckdb_api::internal::JsonDecodePlan plan;
	plan.response_source = duckdb_api::internal::JsonResponseSource::JSON_PATH_MANY;
	plan.records_field = "items";
	plan.columns = {{"id", "id", duckdb_api::ValueKind::BIGINT},
	                {"login", "login", duckdb_api::ValueKind::VARCHAR},
	                {"site_admin", "site_admin", duckdb_api::ValueKind::BOOLEAN}};
	plan.max_records = 3;
	plan.max_string_bytes = 256;
	plan.max_json_nesting = 16;
	plan.max_decoded_memory_bytes = 4096;
	plan.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	return plan;
}

duckdb_api::internal::JsonDecodePlan RootPlan() {
	auto plan = Plan();
	plan.response_source = duckdb_api::internal::JsonResponseSource::ROOT_OBJECT;
	plan.records_field.clear();
	plan.max_records = 1;
	return plan;
}

std::vector<duckdb_api::TypedRow> Decode(const std::string &body, ManualControl &control) {
	return duckdb_api::internal::DecodeJsonRows(body, Plan(), control);
}

std::vector<duckdb_api::TypedRow> DecodeRoot(const std::string &body, ManualControl &control) {
	return duckdb_api::internal::DecodeJsonRows(body, RootPlan(), control);
}

void RequireError(const std::function<void()> &action, duckdb_api::ErrorStage stage, const std::string &field,
                  const std::string &forbidden = "") {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == stage, "decoder error stage drifted");
		Require(error.Field() == field, "decoder error field drifted");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        "decoder error was empty or unbounded");
		if (!forbidden.empty()) {
			Require(error.SafeMessage().find(forbidden) == std::string::npos, "decoder error exposed response content");
		}
	}
	Require(rejected, "expected a structured decoder error");
}

std::string OneRow(const std::string &fields) {
	return std::string("{\"items\":[{") + fields + "}]}";
}

void TestTypedRowsAndCompleteValidation() {
	ManualControl control;
	const auto rows = Decode("{\"ignored\":{\"nested\":[true,null,3.5]},\"items\":["
	                         "{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false,\"ignored\":1},"
	                         "{\"site_admin\":true,\"login\":\"duck\\u0064b-fdw\",\"id\":-22},"
	                         "{\"id\":33,\"login\":\"three\",\"site_admin\":false}]}",
	                         control);
	Require(rows.size() == 3, "decoder did not return all bounded records");
	Require(rows[0].values[0].kind == duckdb_api::ValueKind::BIGINT && rows[0].values[0].bigint_value == 11 &&
	            rows[0].values[1].varchar_value == "duckdb" && !rows[0].values[2].boolean_value,
	        "first schema-aligned row was decoded incorrectly");
	Require(rows[1].values[0].bigint_value == -22 && rows[1].values[1].varchar_value == "duckdb-fdw" &&
	            rows[1].values[2].boolean_value,
	        "Unicode escape or typed value was decoded incorrectly");
}

void TestStrictSuccessfulRootObject() {
	ManualControl control;
	const auto rows = DecodeRoot(
	    "{\"ignored\":{\"nested\":[true,null]},\"site_admin\":false,\"login\":\"duck\\u0064b\",\"id\":11}", control);
	Require(rows.size() == 1 && rows[0].values.size() == 3 && rows[0].values[0].bigint_value == 11 &&
	            rows[0].values[1].varchar_value == "duckdb" && !rows[0].values[2].boolean_value,
	        "successful root object did not produce exactly one aligned row");

	struct Case {
		std::string body;
		std::string field;
	};
	const std::vector<Case> cases = {
	    {"[]", ""},
	    {"null", ""},
	    {"{}", "id"},
	    {"{\"id\":1,\"login\":\"duckdb\",\"site_admin\":null}", "site_admin"},
	    {"{\"id\":1,\"login\":\"duckdb\",\"login\":\"duplicate\",\"site_admin\":false}", "login"},
	    {"{\"id\":1.0,\"login\":\"duckdb\",\"site_admin\":false}", "id"}};
	for (std::size_t index = 0; index < cases.size(); index++) {
		RequireError([&]() { DecodeRoot(cases[index].body, control); }, duckdb_api::ErrorStage::SCHEMA,
		             cases[index].field);
	}
	RequireError([&]() { DecodeRoot("{\"id\":1", control); }, duckdb_api::ErrorStage::DECODE, "");

	auto bounded = RootPlan();
	bounded.max_string_bytes = 3;
	RequireError(
	    [&]() {
		    duckdb_api::internal::DecodeJsonRows("{\"id\":1,\"login\":\"four\",\"site_admin\":false}", bounded,
		                                         control);
	    },
	    duckdb_api::ErrorStage::RESOURCE, "login");
	bounded = RootPlan();
	bounded.max_json_nesting = 2;
	RequireError(
	    [&]() {
		    duckdb_api::internal::DecodeJsonRows("{\"id\":1,\"login\":\"ok\",\"site_admin\":false,\"ignored\":[[0]]}",
		                                         bounded, control);
	    },
	    duckdb_api::ErrorStage::RESOURCE, "json_nesting");

	auto invalid = RootPlan();
	invalid.max_records = 2;
	RequireError([&]() { duckdb_api::internal::DecodeJsonRows("{}", invalid, control); },
	             duckdb_api::ErrorStage::INTERNAL, "");
}

void TestMalformedAndInvalidUtf8() {
	ManualControl control;
	std::vector<std::string> malformed = {"", "{", "{\"items\":[],\"ignored\":[1,]}", "{\"items\":[]}}",
	                                      "{\"items\":[] true}"};
	malformed.push_back(std::string("{\"items\":[]}") + std::string(1, '\0'));
	malformed.push_back(std::string("{\"items\":[],\"bad\":\"") + std::string(1, static_cast<char>(0xff)) + "\"}");
	for (std::size_t index = 0; index < malformed.size(); index++) {
		RequireError([&]() { Decode(malformed[index], control); }, duckdb_api::ErrorStage::DECODE, "");
	}
}

void TestRequiredShapeAndStrictTypes() {
	ManualControl control;
	struct Case {
		std::string body;
		std::string field;
	};
	const std::vector<Case> cases = {
	    {"[]", "items"},
	    {"{}", "items"},
	    {"{\"items\":[],\"items\":[]}", "items"},
	    {"{\"items\":null}", "items"},
	    {"{\"items\":[1]}", ""},
	    {OneRow("\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1,\"site_admin\":false"), "login"},
	    {OneRow("\"id\":1,\"login\":\"duckdb\""), "site_admin"},
	    {OneRow("\"id\":1,\"id\":2,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":\"1\",\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1.0,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1e0,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":9223372036854775808,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1,\"login\":null,\"site_admin\":false"), "login"},
	    {OneRow("\"id\":1,\"login\":\"duckdb\",\"site_admin\":0"), "site_admin"}};
	for (std::size_t index = 0; index < cases.size(); index++) {
		RequireError([&]() { Decode(cases[index].body, control); }, duckdb_api::ErrorStage::SCHEMA, cases[index].field,
		             "9223372036854775808");
	}
}

void TestExactResourceBoundaries() {
	ManualControl control;
	auto plan = Plan();
	plan.max_string_bytes = 3;
	const auto exact = OneRow("\"id\":1,\"login\":\"abc\",\"site_admin\":false");
	Require(duckdb_api::internal::DecodeJsonRows(exact, plan, control).size() == 1,
	        "exact string boundary was rejected");
	const auto long_string = OneRow("\"id\":1,\"login\":\"abcd\",\"site_admin\":false");
	RequireError([&]() { duckdb_api::internal::DecodeJsonRows(long_string, plan, control); },
	             duckdb_api::ErrorStage::RESOURCE, "login");

	const std::string four_rows = "{\"items\":[{\"id\":1,\"login\":\"a\",\"site_admin\":false},"
	                              "{\"id\":2,\"login\":\"b\",\"site_admin\":false},"
	                              "{\"id\":3,\"login\":\"c\",\"site_admin\":false},"
	                              "{\"id\":4,\"login\":\"d\",\"site_admin\":false}]}";
	RequireError([&]() { duckdb_api::internal::DecodeJsonRows(four_rows, Plan(), control); },
	             duckdb_api::ErrorStage::RESOURCE, "items");

	plan = Plan();
	plan.max_json_nesting = 2;
	const std::string exact_nesting = "{\"ignored\":[0],\"items\":[]}";
	Require(duckdb_api::internal::DecodeJsonRows(exact_nesting, plan, control).empty(),
	        "exact nesting boundary was rejected");
	const std::string deep = "{\"ignored\":[[0]],\"items\":[]}";
	RequireError([&]() { duckdb_api::internal::DecodeJsonRows(deep, plan, control); }, duckdb_api::ErrorStage::RESOURCE,
	             "json_nesting");

	plan = Plan();
	plan.max_decoded_memory_bytes = 1;
	RequireError([&]() { duckdb_api::internal::DecodeJsonRows("{\"items\":[]}", plan, control); },
	             duckdb_api::ErrorStage::RESOURCE, "decoded_memory_bytes");
}

void TestCancellationAndDeadline() {
	ManualControl cancelled;
	cancelled.Cancel();
	bool observed = false;
	try {
		Decode("{\"items\":[]}", cancelled);
	} catch (const duckdb_api::ExecutionCancelled &) {
		observed = true;
	}
	Require(observed, "decoder did not observe pre-parse cancellation");

	ManualControl control;
	auto expired = Plan();
	expired.deadline = std::chrono::steady_clock::now();
	RequireError([&]() { duckdb_api::internal::DecodeJsonRows("{\"items\":[]}", expired, control); },
	             duckdb_api::ErrorStage::RESOURCE, "wall_milliseconds");
}

} // namespace

int main() {
	try {
		TestTypedRowsAndCompleteValidation();
		TestStrictSuccessfulRootObject();
		TestMalformedAndInvalidUtf8();
		TestRequiredShapeAndStrictTypes();
		TestExactResourceBoundaries();
		TestCancellationAndDeadline();
		std::cout << "JSON decoder tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "JSON decoder tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
