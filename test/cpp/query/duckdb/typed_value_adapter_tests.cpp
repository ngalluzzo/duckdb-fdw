#include "duckdb/common/allocator.hpp"
#include "query/duckdb/typed_value_adapter.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb::duckdb_api_query_internal::PlannedValueColumn;
using duckdb_api_test::Require;

std::vector<PlannedValueColumn> ExpectedColumns() {
	return {{duckdb_api::ValueKind::BIGINT, false}, {duckdb_api::ValueKind::VARCHAR, true},
	        {duckdb_api::ValueKind::BOOLEAN, false}};
}

void InitializeOutput(duckdb::DataChunk &output) {
	output.Initialize(duckdb::Allocator::DefaultAllocator(),
	                  {duckdb::LogicalType::BIGINT, duckdb::LogicalType::VARCHAR, duckdb::LogicalType::BOOLEAN});
}

duckdb_api::TypedBatch OneRowBatch(duckdb_api::TypedValue first, duckdb_api::TypedValue second,
                                   duckdb_api::TypedValue third) {
	duckdb_api::TypedBatch batch;
	batch.column_kinds = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	batch.rows.push_back({{std::move(first), std::move(second), std::move(third)}});
	return batch;
}

void RequireLogicError(const std::function<void()> &action, const std::string &message) {
	try {
		action();
	} catch (const std::logic_error &) {
		return;
	}
	throw std::runtime_error(message);
}

void TestNullAndSentinelCounterexamples() {
	duckdb::DataChunk output;
	InitializeOutput(output);
	duckdb_api::TypedBatch batch;
	batch.column_kinds = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	batch.rows.push_back({{duckdb_api::TypedValue::BigInt(0), duckdb_api::TypedValue::Null(duckdb_api::ValueKind::VARCHAR),
	                       duckdb_api::TypedValue::Boolean(false)}});
	batch.rows.push_back({{duckdb_api::TypedValue::BigInt(-1), duckdb_api::TypedValue::Varchar(""),
	                       duckdb_api::TypedValue::Boolean(true)}});

	duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 2);
	Require(output.size() == 2, "typed writer changed successful cardinality");
	Require(!output.GetValue(0, 0).IsNull() && output.GetValue(0, 0).GetValue<int64_t>() == 0,
	        "zero became NULL or changed value");
	Require(output.GetValue(1, 0).IsNull() && output.GetValue(1, 0).type() == duckdb::LogicalType::VARCHAR,
	        "invalid VARCHAR did not become a typed DuckDB NULL");
	Require(!output.GetValue(2, 0).IsNull() && !output.GetValue(2, 0).GetValue<bool>(),
	        "false became NULL or changed value");
	Require(!output.GetValue(1, 1).IsNull() && output.GetValue(1, 1).ToString().empty(),
	        "empty string became NULL or changed value");
}

void TestRequiredNullsAndKindDriftFailBeforeWrites() {
	duckdb::DataChunk output;
	InitializeOutput(output);
	auto batch = OneRowBatch(duckdb_api::TypedValue::BigInt(7), duckdb_api::TypedValue::Varchar("kept"),
	                         duckdb_api::TypedValue::Boolean(true));
	batch.rows.push_back({{duckdb_api::TypedValue::Null(duckdb_api::ValueKind::BIGINT),
	                       duckdb_api::TypedValue::Varchar("rejected"), duckdb_api::TypedValue::Boolean(false)}});
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 2); },
	    "required NULL was accepted");
	Require(output.size() == 0, "failed batch partially changed output cardinality");

	batch = OneRowBatch(duckdb_api::TypedValue::BigInt(1), duckdb_api::TypedValue::Varchar("value"),
	                    duckdb_api::TypedValue::Boolean(false));
	batch.rows[0].values[1] = duckdb_api::TypedValue::Boolean(false);
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 1); },
	    "value-kind mismatch was accepted");
	batch = OneRowBatch(duckdb_api::TypedValue::BigInt(1), duckdb_api::TypedValue::Varchar("value"),
	                    duckdb_api::TypedValue::Boolean(false));
	batch.column_kinds[0] = duckdb_api::ValueKind::VARCHAR;
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 1); },
	    "batch-kind mismatch was accepted");
}

void TestArityAndBatchBoundsFailClosed() {
	duckdb::DataChunk output;
	InitializeOutput(output);
	duckdb_api::TypedBatch empty;
	empty.column_kinds = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, empty, ExpectedColumns(), 1); },
	    "empty successful batch was accepted");

	auto batch = OneRowBatch(duckdb_api::TypedValue::BigInt(1), duckdb_api::TypedValue::Varchar("value"),
	                         duckdb_api::TypedValue::Boolean(false));
	batch.rows[0].values.pop_back();
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 1); },
	    "short row was accepted");

	batch = OneRowBatch(duckdb_api::TypedValue::BigInt(1), duckdb_api::TypedValue::Varchar("value"),
	                    duckdb_api::TypedValue::Boolean(false));
	batch.rows.push_back(batch.rows[0]);
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 1); },
	    "widened successful batch was accepted");
}

void TestPlannedLogicalTypeMappingIsClosed() {
	Require(duckdb::duckdb_api_query_internal::PlannedLogicalType({"id", "BIGINT", false, "id"}) ==
	            duckdb::LogicalType::BIGINT &&
	            duckdb::duckdb_api_query_internal::PlannedLogicalType({"name", "VARCHAR", true, "name"}) ==
	                duckdb::LogicalType::VARCHAR &&
	            duckdb::duckdb_api_query_internal::PlannedLogicalType({"flag", "BOOLEAN", false, "flag"}) ==
	                duckdb::LogicalType::BOOLEAN,
	        "planned scalar mapping changed");
	RequireLogicError(
	    []() {
		    (void)duckdb::duckdb_api_query_internal::PlannedLogicalType({"value", "INTEGER", false, "value"});
	    },
	    "unsupported planned logical type was accepted");
}

} // namespace

int main() {
	try {
		TestNullAndSentinelCounterexamples();
		TestRequiredNullsAndKindDriftFailBeforeWrites();
		TestArityAndBatchBoundsFailClosed();
		TestPlannedLogicalTypeMappingIsClosed();
		std::cout << "typed value adapter tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "typed value adapter tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
