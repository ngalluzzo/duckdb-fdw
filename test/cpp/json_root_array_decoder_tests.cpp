#include "duckdb_api/internal/json_decoder.hpp"
#include "support/require.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

namespace {

using duckdb_api_test::Require;

class Control final : public duckdb_api::ExecutionControl {
public:
	Control() : cancelled(false) {
	}
	bool IsCancellationRequested() const noexcept override {
		return cancelled;
	}
	bool cancelled;
};

duckdb_api::internal::JsonDecodePlan Plan(uint64_t max_records = 100) {
	duckdb_api::internal::JsonDecodePlan plan;
	plan.response_source = duckdb_api::internal::JsonResponseSource::ROOT_ARRAY;
	plan.columns = {{"id", "id", duckdb_api::ValueKind::BIGINT},
	                {"full_name", "full_name", duckdb_api::ValueKind::VARCHAR},
	                {"private", "private", duckdb_api::ValueKind::BOOLEAN},
	                {"fork", "fork", duckdb_api::ValueKind::BOOLEAN},
	                {"archived", "archived", duckdb_api::ValueKind::BOOLEAN}};
	plan.max_records = max_records;
	plan.max_string_bytes = 512;
	plan.max_json_nesting = 16;
	plan.max_decoded_memory_bytes = 2 * 1024 * 1024;
	plan.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	return plan;
}

std::string Repository(uint64_t id, const std::string &full_name = "owner/repository") {
	return std::string("{\"id\":") + std::to_string(id) + ",\"full_name\":\"" + full_name +
	       "\",\"private\":false,\"fork\":true,\"archived\":false}";
}

void RequireError(const std::function<void()> &action, duckdb_api::ErrorStage stage, const std::string &field) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == stage && error.Field() == field, "root-array failure used the wrong safe diagnostic");
		Require(error.SafeMessage().size() <= 128, "root-array failure diagnostic was unbounded");
	}
	Require(rejected, "root-array counterexample was accepted");
}

void TestFiveColumnRootArrayAndMemoryEvidence() {
	Control control;
	auto decoded = duckdb_api::internal::DecodeJsonPage("[" + Repository(11) + "]", Plan(), control);
	Require(decoded.rows.size() == 1 && decoded.rows[0].values.size() == 5 &&
	            decoded.rows[0].values[0].bigint_value == 11 &&
	            decoded.rows[0].values[1].varchar_value == "owner/repository" &&
	            !decoded.rows[0].values[2].boolean_value && decoded.rows[0].values[3].boolean_value &&
	            !decoded.rows[0].values[4].boolean_value && decoded.retained_memory_bytes > 0,
	        "repository root array did not produce the exact typed row and retained-memory evidence");
	Require(duckdb_api::internal::DecodeJsonRows("[]", Plan(), control).empty(),
	        "empty root array was not a valid empty page");
}

void TestRecordAndStringBoundaries() {
	Control control;
	std::string exact = "[";
	for (uint64_t index = 0; index < 100; index++) {
		exact += index == 0 ? "" : ",";
		exact += Repository(index + 1, "r");
	}
	exact += "]";
	Require(duckdb_api::internal::DecodeJsonRows(exact, Plan(), control).size() == 100,
	        "exact 100-record page was rejected");
	exact.insert(exact.size() - 1, "," + Repository(101, "r"));
	RequireError([&]() { (void)duckdb_api::internal::DecodeJsonRows(exact, Plan(), control); },
	             duckdb_api::ErrorStage::RESOURCE, "");

	auto string_plan = Plan();
	Require(duckdb_api::internal::DecodeJsonRows("[" + Repository(1, std::string(512, 'a')) + "]", string_plan, control)
	                .size() == 1,
	        "exact 512-byte repository name was rejected");
	RequireError(
	    [&]() {
		    (void)duckdb_api::internal::DecodeJsonRows("[" + Repository(1, std::string(513, 'b')) + "]", string_plan,
		                                               control);
	    },
	    duckdb_api::ErrorStage::RESOURCE, "full_name");
}

void TestSchemaAndLifecycleCounterexamples() {
	Control control;
	const std::string cases[] = {
	    "{}",
	    "[null]",
	    "[{\"full_name\":\"r\",\"private\":false,\"fork\":false,\"archived\":false}]",
	    "[{\"id\":1,\"full_name\":null,\"private\":false,\"fork\":false,\"archived\":false}]",
	    "[{\"id\":1,\"id\":2,\"full_name\":\"r\",\"private\":false,\"fork\":false,\"archived\":false}]",
	    "[{\"id\":1,\"full_name\":\"r\",\"private\":0,\"fork\":false,\"archived\":false}]"};
	const std::string fields[] = {"", "", "id", "full_name", "id", "private"};
	for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		RequireError([&]() { (void)duckdb_api::internal::DecodeJsonRows(cases[index], Plan(), control); },
		             duckdb_api::ErrorStage::SCHEMA, fields[index]);
	}
	RequireError([&]() { (void)duckdb_api::internal::DecodeJsonRows("[", Plan(), control); },
	             duckdb_api::ErrorStage::DECODE, "");
	control.cancelled = true;
	bool cancelled = false;
	try {
		(void)duckdb_api::internal::DecodeJsonRows("[]", Plan(), control);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancelled = true;
	}
	Require(cancelled, "root-array decoder ignored cancellation");
}

} // namespace

int main() {
	try {
		TestFiveColumnRootArrayAndMemoryEvidence();
		TestRecordAndStringBoundaries();
		TestSchemaAndLifecycleCounterexamples();
		std::cout << "JSON root-array decoder tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "JSON root-array decoder tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
