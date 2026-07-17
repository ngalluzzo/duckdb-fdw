#include "duckdb_api/internal/fixture_runtime.hpp"
#include "support/fixture_scenarios.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::BuildPlanFor;
using duckdb_api_test::FixtureScenario;
using duckdb_api_test::NeverCancelled;
using duckdb_api_test::Require;
using duckdb_api_test::ScenarioFactory;

std::vector<duckdb_api::ItemRow> Execute(const std::shared_ptr<ScenarioFactory> &factory) {
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled control;
	auto stream = executor->Open(BuildPlanFor(*factory), control);
	std::vector<duckdb_api::ItemRow> result;
	std::vector<duckdb_api::ItemRow> batch;
	while (stream->Next(control, batch)) {
		result.insert(result.end(), batch.begin(), batch.end());
	}
	stream->Close();
	return result;
}

void RequireExecutionFailure(FixtureScenario scenario, duckdb_api::ErrorStage expected_stage,
                             const std::string &expected_message, const std::string &forbidden,
                             const std::string &body = "") {
	auto factory = std::make_shared<ScenarioFactory>(scenario, body);
	bool rejected = false;
	try {
		Execute(factory);
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = error.Stage() == expected_stage && error.SafeMessage() == expected_message &&
		           error.SafeMessage().find(forbidden) == std::string::npos;
	}
	Require(rejected, "decoder failure category, safe message, or redaction drifted");
	Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "decoder failure did not close its stream exactly once");
}

void TestUnicodeAndAdditiveFields() {
	auto factory = std::make_shared<ScenarioFactory>(
	    FixtureScenario::SUCCESS,
	    "{\"metadata\":{\"nested\":[1,{\"ignored\":true}]},\"items\":[{\"id\":1,\"name\":\"\\u0061lpha\","
	    "\"active\":true,\"future\":{\"also\":\"ignored\"}},{\"id\":2,\"name\":\"\\uD83D\\uDE00\","
	    "\"active\":false}]}");
	const auto rows = Execute(factory);
	Require(rows.size() == 2 && rows[0].name == "alpha" && rows[1].name == "\xf0\x9f\x98\x80",
	        "Unicode or additive-field decoding drifted");
}

void TestLosslessBigintConversion() {
	auto factory = std::make_shared<ScenarioFactory>(
	    FixtureScenario::SUCCESS,
	    "{\"items\":[{\"id\":1.0,\"name\":\"alpha\",\"active\":true},{\"id\":2e0,\"name\":\"beta\","
	    "\"active\":false},{\"id\":300e-2,\"name\":\"gamma\",\"active\":true}]}");
	const auto rows = Execute(factory);
	Require(rows.size() == 3 && rows[0].id == 1 && rows[1].id == 2 && rows[2].id == 3,
	        "exact decimal or exponent BIGINT conversion drifted");
}

void TestFailureCategoriesAndRedaction() {
	RequireExecutionFailure(FixtureScenario::MALFORMED, duckdb_api::ErrorStage::DECODE, "response is not valid JSON",
	                        "top-secret-malformed");
	RequireExecutionFailure(FixtureScenario::TYPE_MISMATCH, duckdb_api::ErrorStage::SCHEMA,
	                        "field id cannot be converted to BIGINT", "top-secret-type-value");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA, "required field id is missing",
	                        "top-secret-missing", "{\"items\":[{\"name\":\"top-secret-missing\",\"active\":true}]}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA,
	                        "field id cannot be converted to BIGINT", "9223372036854775808",
	                        "{\"items\":[{\"id\":9223372036854775808,\"name\":\"alpha\",\"active\":true}]}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA,
	                        "field id cannot be converted to BIGINT", "1.5",
	                        "{\"items\":[{\"id\":1.5,\"name\":\"alpha\",\"active\":true}]}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA,
	                        "field active cannot be converted to BOOLEAN", "top-secret-active",
	                        "{\"items\":[{\"id\":1,\"name\":\"alpha\",\"active\":\"top-secret-active\"}]}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA,
	                        "response extractor did not match the required field", "top-secret-wrapper",
	                        "{\"other\":{\"nested\":[\"top-secret-wrapper\"]}}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA,
	                        "response extractor did not produce an array", "top-secret-items",
	                        "{\"items\":{\"secret\":\"top-secret-items\"}}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::SCHEMA,
	                        "response item does not match the declared row shape", "top-secret-scalar",
	                        "{\"items\":[null],\"secret\":\"top-secret-scalar\"}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::DECODE, "response is not valid JSON",
	                        "top-secret-trailing",
	                        "{\"items\":[{\"id\":1,\"name\":\"top-secret-trailing\",\"active\":true},]}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::DECODE, "response is not valid JSON",
	                        "top-secret-surrogate",
	                        "{\"items\":[{\"id\":1,\"name\":\"\\uD800top-secret-surrogate\",\"active\":true}]}");
	std::string invalid_utf8 = "{\"items\":[{\"id\":1,\"name\":\"";
	invalid_utf8.push_back(static_cast<char>(0xc3));
	invalid_utf8 += "(top-secret-invalid-utf8\",\"active\":true}]}";
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::DECODE, "response is not valid JSON",
	                        "top-secret-invalid-utf8", invalid_utf8);
}

void TestResourceBudgets() {
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::POLICY,
	                        "field name exceeds the configured resource budget", std::string(129, 'x'),
	                        "{\"items\":[{\"id\":1,\"name\":\"" + std::string(129, 'x') + "\",\"active\":true}]}");
	RequireExecutionFailure(FixtureScenario::SUCCESS, duckdb_api::ErrorStage::POLICY,
	                        "response exceeds the fixture-byte budget", "top-secret-oversize",
	                        std::string(4097, 'x') + "top-secret-oversize");
}

} // namespace

int main() {
	try {
		TestUnicodeAndAdditiveFields();
		TestLosslessBigintConversion();
		TestFailureCategoriesAndRedaction();
		TestResourceBudgets();
		std::cout << "fixture decoder tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "fixture decoder tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
