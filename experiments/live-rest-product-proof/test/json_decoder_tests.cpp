#include "live_rest/internal/json_decoder.hpp"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

class ManualCancellation final : public live_rest::CancellationView {
public:
	ManualCancellation() : cancelled(false) {
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

void RequireRuntimeError(const std::function<void()> &action, live_rest::RuntimeStage stage,
	                     const std::string &field) {
	bool rejected = false;
	try {
		action();
	} catch (const live_rest::RuntimeError &error) {
		rejected = true;
		Require(error.Stage() == stage, "decoder error stage did not match the expected boundary");
		Require(error.Field() == field, "decoder error field did not match the expected field");
		Require(std::string(error.what()).size() <= 128, "decoder diagnostic was not bounded");
	}
	Require(rejected, "expected a structured decoder error");
}

std::string OneRow(const std::string &fields) {
	return std::string("{\"items\":[{") + fields + "}]}";
}

std::vector<live_rest::LiveRow> Decode(const std::string &body, const live_rest::CancellationView &cancellation) {
	const auto plan = live_rest::BuildLiveScanPlan("https://api.github.com");
	return live_rest::internal::DecodeResponseRows(body, plan, cancellation);
}

void TestTypedRowsAndIgnoredFields() {
	ManualCancellation cancellation;
	const auto rows = Decode(
	    "{\"ignored\":{\"nested\":[true,null,3.5]},\"items\":["
	    "{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false,\"ignored\":1},"
	    "{\"site_admin\":true,\"login\":\"duck\\u0064b-fdw\",\"id\":-22},"
	    "{\"id\":33,\"login\":\"three\",\"site_admin\":false}]}",
	    cancellation);
	Require(rows.size() == 3, "decoder did not return the declared bounded rows");
	Require(rows[0].id == 11 && rows[0].login == "duckdb" && !rows[0].site_admin,
	        "first typed row was decoded incorrectly");
	Require(rows[1].id == -22 && rows[1].login == "duckdb-fdw" && rows[1].site_admin,
	        "second typed row or Unicode escape was decoded incorrectly");
	Require(rows[2].id == 33 && rows[2].login == "three" && !rows[2].site_admin,
	        "third typed row was decoded incorrectly");
}

void TestMalformedDocuments() {
	ManualCancellation cancellation;
	std::vector<std::string> malformed = {
	    "", "{", "{\"items\":[],\"ignored\":[1,]}", "{\"items\":[]}}", "{\"items\":[] true}"};
	malformed.push_back(std::string("{\"items\":[]}") + std::string(1, '\0'));
	for (std::size_t index = 0; index < malformed.size(); index++) {
		RequireRuntimeError([&]() { Decode(malformed[index], cancellation); }, live_rest::RuntimeStage::DECODE, "");
	}
}

void TestRequiredShapeAndTypes() {
	ManualCancellation cancellation;
	struct SchemaCase {
		std::string body;
		std::string field;
	};
	const std::vector<SchemaCase> cases = {
	    {"[]", "items"},
	    {"{}", "items"},
	    {"{\"items\":[],\"items\":[]}", "items"},
	    {"{\"items\":null}", "items"},
	    {"{\"items\":[1]}", ""},
	    {OneRow("\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1,\"site_admin\":false"), "login"},
	    {OneRow("\"id\":1,\"login\":\"duckdb\""), "site_admin"},
	    {OneRow("\"id\":1,\"id\":2,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1,\"login\":\"duckdb\",\"login\":\"again\",\"site_admin\":false"), "login"},
	    {OneRow("\"id\":1,\"login\":\"duckdb\",\"site_admin\":false,\"site_admin\":true"), "site_admin"},
	    {OneRow("\"id\":\"1\",\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1.0,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1e0,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":9223372036854775808,\"login\":\"duckdb\",\"site_admin\":false"), "id"},
	    {OneRow("\"id\":1,\"login\":null,\"site_admin\":false"), "login"},
	    {OneRow("\"id\":1,\"login\":\"duckdb\",\"site_admin\":0"), "site_admin"}};
	for (std::size_t index = 0; index < cases.size(); index++) {
		RequireRuntimeError([&]() { Decode(cases[index].body, cancellation); }, live_rest::RuntimeStage::SCHEMA,
		                    cases[index].field);
	}
}

void TestDecoderResourceLimits() {
	ManualCancellation cancellation;
	const auto plan = live_rest::BuildLiveScanPlan("https://api.github.com");

	const std::string four_rows =
	    "{\"items\":[{\"id\":1,\"login\":\"a\",\"site_admin\":false},"
	    "{\"id\":2,\"login\":\"b\",\"site_admin\":false},"
	    "{\"id\":3,\"login\":\"c\",\"site_admin\":false},"
	    "{\"id\":4,\"login\":\"d\",\"site_admin\":false}]}";
	RequireRuntimeError([&]() { live_rest::internal::DecodeResponseRows(four_rows, plan, cancellation); },
	                    live_rest::RuntimeStage::RESOURCE, "items");

	const std::string long_login(static_cast<std::size_t>(plan.max_string_bytes + 1), 'x');
	const auto long_string = OneRow(std::string("\"id\":1,\"login\":\"") + long_login +
	                                "\",\"site_admin\":false");
	RequireRuntimeError([&]() { live_rest::internal::DecodeResponseRows(long_string, plan, cancellation); },
	                    live_rest::RuntimeStage::RESOURCE, "login");

	std::string nested = "0";
	for (std::size_t index = 0; index < 34; index++) {
		nested = std::string("[") + nested + "]";
	}
	const auto deep_document = std::string("{\"ignored\":") + nested + ",\"items\":[]}";
	RequireRuntimeError([&]() { live_rest::internal::DecodeResponseRows(deep_document, plan, cancellation); },
	                    live_rest::RuntimeStage::RESOURCE, "");
}

void TestCancellationCheckpoint() {
	ManualCancellation cancellation;
	cancellation.Cancel();
	bool observed = false;
	try {
		Decode("{\"items\":[]}", cancellation);
	} catch (const live_rest::ExecutionCancelled &) {
		observed = true;
	}
	Require(observed, "decoder did not observe cancellation before parsing");
}

} // namespace

int main() {
	try {
		TestTypedRowsAndIgnoredFields();
		TestMalformedDocuments();
		TestRequiredShapeAndTypes();
		TestDecoderResourceLimits();
		TestCancellationCheckpoint();
		std::cout << "live REST JSON decoder tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "live REST JSON decoder tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
