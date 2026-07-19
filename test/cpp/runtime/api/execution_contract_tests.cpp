#include "duckdb_api/execution.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using duckdb_api_test::Require;

static_assert(std::is_nothrow_destructible<duckdb_api::ExecutionControl>::value,
              "execution control teardown must be non-throwing");
static_assert(std::is_nothrow_destructible<duckdb_api::BatchStream>::value,
              "batch stream teardown must be non-throwing");
static_assert(std::is_nothrow_destructible<duckdb_api::ScanExecutor>::value,
              "scan executor teardown must be non-throwing");
static_assert(duckdb_api::ErrorStage::AUTHENTICATION != duckdb_api::ErrorStage::AUTHORIZATION,
              "authentication and authorization must remain distinct stages");

void TestTypedValuesAndSchemaAlignment() {
	duckdb_api::TypedBatch batch;
	batch.column_kinds = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	duckdb_api::TypedRow row;
	row.values.push_back(duckdb_api::TypedValue::BigInt(42));
	row.values.push_back(duckdb_api::TypedValue::Varchar("duckdb"));
	row.values.push_back(duckdb_api::TypedValue::Boolean(true));
	batch.rows.push_back(std::move(row));

	Require(batch.IsSchemaAligned(), "valid typed batch was not schema aligned");
	Require(batch.rows[0].values[0].bigint_value == 42, "BIGINT value was not retained losslessly");
	Require(batch.rows[0].values[1].varchar_value == "duckdb", "VARCHAR value was not retained");
	Require(batch.rows[0].values[2].boolean_value, "BOOLEAN value was not retained");

	batch.rows[0].values[2].kind = duckdb_api::ValueKind::VARCHAR;
	Require(!batch.IsSchemaAligned(), "typed batch accepted a row kind mismatch");
	batch.rows[0].values.pop_back();
	Require(!batch.IsSchemaAligned(), "typed batch accepted a row arity mismatch");
	batch.Clear();
	Require(batch.column_kinds.empty() && batch.rows.empty(), "typed batch clear retained values or schema");
}

void TestStableErrorContract() {
	const duckdb_api::ExecutionError error(duckdb_api::ErrorStage::HTTP_STATUS, "", "HTTP endpoint rejected request");
	Require(error.Stage() == duckdb_api::ErrorStage::HTTP_STATUS, "error stage drifted");
	Require(error.Field().empty(), "status error unexpectedly exposed a field");
	Require(error.SafeMessage() == "HTTP endpoint rejected request", "safe error message drifted");
	Require(std::string(error.what()) == error.SafeMessage(), "what() exposed a different diagnostic");

	const duckdb_api::ExecutionCancelled cancellation;
	Require(std::string(cancellation.what()) == "execution cancelled", "cancellation marker drifted");

	const duckdb_api::ExecutionError authentication(duckdb_api::ErrorStage::AUTHENTICATION, "authorization",
	                                                "authentication failed");
	const duckdb_api::ExecutionError authorization(duckdb_api::ErrorStage::AUTHORIZATION, "authorization",
	                                               "authorization failed");
	Require(authentication.Stage() == duckdb_api::ErrorStage::AUTHENTICATION, "authentication error stage drifted");
	Require(authorization.Stage() == duckdb_api::ErrorStage::AUTHORIZATION, "authorization error stage drifted");
}

} // namespace

int main() {
	try {
		TestTypedValuesAndSchemaAlignment();
		TestStableErrorContract();
		std::cout << "execution contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "execution contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
