#include "duckdb_api/connector.hpp"
#include "duckdb_api/embedded_example.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

void RequireColumn(const duckdb_api::CompiledColumn &column, const std::string &name, const std::string &logical_type,
                   const std::string &extractor) {
	Require(column.name == name, "CompiledConnector column name drifted: " + name);
	Require(column.logical_type == logical_type, "CompiledConnector column type drifted: " + name);
	Require(!column.nullable, "CompiledConnector column became nullable: " + name);
	Require(column.extractor == extractor, "CompiledConnector column extractor drifted: " + name);
}

void TestInternalExampleMetadata() {
	const auto connector = duckdb_api::BuildCompiledConnector("fixture-digest");
	Require(connector.connector_name == "example", "CompiledConnector identifier drifted");
	Require(connector.version == "0.1.0", "CompiledConnector version drifted");
	Require(connector.relation_name == "items", "CompiledConnector relation drifted");
	Require(connector.columns.size() == 3, "CompiledConnector schema width drifted");
	RequireColumn(connector.columns[0], "id", "BIGINT", "$.id");
	RequireColumn(connector.columns[1], "name", "VARCHAR", "$.name");
	RequireColumn(connector.columns[2], "active", "BOOLEAN", "$.active");
	Require(connector.operation_name == "items_list" && connector.method == "GET" && connector.path == "/items" &&
	            connector.extractor == "$.items[*]",
	        "CompiledConnector operation drifted");
	Require(connector.fixture_digest == "fixture-digest", "CompiledConnector fixture provenance drifted");

	Require(connector.Snapshot() ==
	            "connector=example;version=0.1.0;relation=items;"
	            "schema=id:BIGINT!:$.id,name:VARCHAR!:$.name,active:BOOLEAN!:$.active;"
	            "operation=items_list:fallback:many:REST:GET:/items:$.items[*];fixture=fixture-digest",
	        "CompiledConnector snapshot drifted");
}

void TestTrackedSnapshot() {
	std::ifstream input(DUCKDB_API_SOURCE_ROOT "/fixtures/example/compiled_connector.snapshot");
	std::string tracked_snapshot;
	std::getline(input, tracked_snapshot);
	Require(input.good() || input.eof(), "tracked CompiledConnector snapshot could not be read");
	Require(duckdb_api::BuildCompiledConnector(duckdb_api::EXAMPLE_FIXTURE_SHA256).Snapshot() == tracked_snapshot,
	        "executable CompiledConnector drifted from the tracked snapshot");
}

} // namespace

int main() {
	try {
		TestInternalExampleMetadata();
		TestTrackedSnapshot();
		std::cout << "connector contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
