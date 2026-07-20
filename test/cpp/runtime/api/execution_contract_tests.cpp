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
static_assert(duckdb_api::ErrorStage::REMOTE_PROTOCOL != duckdb_api::ErrorStage::DECODE,
              "remote protocol and local decode failures must remain distinct stages");

void TestTypedValuesAndSchemaAlignment() {
	const duckdb_api::TypedValue compatible {duckdb_api::ValueKind::BIGINT, 7, std::string(), false};
	Require(compatible.valid && compatible.kind == duckdb_api::ValueKind::BIGINT && compatible.bigint_value == 7,
	        "former four-field TypedValue construction lost source compatibility");
	const duckdb_api::TypedValue default_value;
	Require(!default_value.valid && default_value.kind == duckdb_api::ValueKind::VARCHAR,
	        "default TypedValue did not fail closed as a typed NULL");

	duckdb_api::TypedBatch batch;
	batch.column_kinds = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	duckdb_api::TypedRow row;
	row.values.push_back(duckdb_api::TypedValue::BigInt(42));
	row.values.push_back(duckdb_api::TypedValue::Varchar("duckdb"));
	row.values.push_back(duckdb_api::TypedValue::Boolean(true));
	batch.rows.push_back(std::move(row));

	Require(batch.IsSchemaAligned(), "valid typed batch was not schema aligned");
	Require(batch.rows[0].values[0].valid && batch.rows[0].values[1].valid && batch.rows[0].values[2].valid,
	        "non-null factories did not retain valid scalar state");
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

void TestNullableTypedValuesRetainKind() {
	const auto null_bigint = duckdb_api::TypedValue::Null(duckdb_api::ValueKind::BIGINT);
	const auto null_varchar = duckdb_api::TypedValue::Null(duckdb_api::ValueKind::VARCHAR);
	const auto null_boolean = duckdb_api::TypedValue::Null(duckdb_api::ValueKind::BOOLEAN);
	Require(!null_bigint.valid && null_bigint.kind == duckdb_api::ValueKind::BIGINT && null_bigint.bigint_value == 0,
	        "NULL BIGINT did not retain kind with an inert payload");
	Require(!null_varchar.valid && null_varchar.kind == duckdb_api::ValueKind::VARCHAR &&
	            null_varchar.varchar_value.empty(),
	        "NULL VARCHAR did not retain kind with an inert payload");
	Require(!null_boolean.valid && null_boolean.kind == duckdb_api::ValueKind::BOOLEAN && !null_boolean.boolean_value,
	        "NULL BOOLEAN did not retain kind with an inert payload");

	duckdb_api::TypedBatch batch;
	batch.column_kinds = {duckdb_api::ValueKind::VARCHAR};
	duckdb_api::TypedRow row;
	row.values.push_back(null_varchar);
	batch.rows.push_back(std::move(row));
	Require(batch.IsSchemaAligned(), "typed NULL was mistaken for a schema mismatch");
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
	const duckdb_api::ExecutionError remote_protocol(duckdb_api::ErrorStage::REMOTE_PROTOCOL, "errors",
	                                                 "remote GraphQL response reported an error");
	Require(remote_protocol.Stage() == duckdb_api::ErrorStage::REMOTE_PROTOCOL, "remote protocol error stage drifted");
	Require(remote_protocol.Field() == "errors" &&
	            remote_protocol.SafeMessage() == "remote GraphQL response reported an error",
	        "remote protocol diagnostic lost its safe structural contract");
}

} // namespace

int main() {
	try {
		TestTypedValuesAndSchemaAlignment();
		TestNullableTypedValuesRetainKind();
		TestStableErrorContract();
		std::cout << "execution contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "execution contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
