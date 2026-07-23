#include "duckdb/common/allocator.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/types/vector.hpp"
#include "query/duckdb/typed_value_adapter.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb::duckdb_api_query_internal::PlannedValueColumn;
using duckdb_api_test::Require;

std::vector<PlannedValueColumn> ExpectedColumns() {
	return {{duckdb_api::ValueKind::BIGINT, false},
	        {duckdb_api::ValueKind::VARCHAR, true},
	        {duckdb_api::ValueKind::BOOLEAN, false}};
}

class StepControl final : public duckdb_api::ExecutionControl {
public:
	explicit StepControl(std::size_t cancel_at_p) : cancel_at(cancel_at_p), calls(0) {
	}

	bool IsCancellationRequested() const noexcept override {
		return ++calls >= cancel_at;
	}

private:
	const std::size_t cancel_at;
	mutable std::size_t calls;
};

std::vector<PlannedValueColumn> ExpectedArrayColumns() {
	return {{duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::BOOLEAN, true), false},
	        {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::BIGINT, false), false},
	        {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::VARCHAR, false), true},
	        {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::DOUBLE, false), false}};
}

void InitializeArrayOutput(duckdb::DataChunk &output) {
	output.Initialize(duckdb::Allocator::DefaultAllocator(), {duckdb::LogicalType::LIST(duckdb::LogicalType::BOOLEAN),
	                                                          duckdb::LogicalType::LIST(duckdb::LogicalType::BIGINT),
	                                                          duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR),
	                                                          duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE)});
}

void InitializeOutput(duckdb::DataChunk &output) {
	output.Initialize(duckdb::Allocator::DefaultAllocator(),
	                  {duckdb::LogicalType::BIGINT, duckdb::LogicalType::VARCHAR, duckdb::LogicalType::BOOLEAN});
}

duckdb_api::TypedBatch OneRowBatch(duckdb_api::TypedValue first, duckdb_api::TypedValue second,
                                   duckdb_api::TypedValue third) {
	duckdb_api::TypedBatch batch;
	batch.column_types = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
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
	batch.column_types = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	batch.rows.push_back(
	    {{duckdb_api::TypedValue::BigInt(0), duckdb_api::TypedValue::Null(duckdb_api::ValueKind::VARCHAR),
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
	batch.column_types[0] = duckdb_api::ValueKind::VARCHAR;
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedColumns(), 1); },
	    "batch-kind mismatch was accepted");
}

void TestArityAndBatchBoundsFailClosed() {
	duckdb::DataChunk output;
	InitializeOutput(output);
	duckdb_api::TypedBatch empty;
	empty.column_types = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
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

	duckdb::DataChunk short_output;
	short_output.Initialize(duckdb::Allocator::DefaultAllocator(), {duckdb::LogicalType::BIGINT});
	batch.rows.pop_back();
	RequireLogicError(
	    [&]() { duckdb::duckdb_api_query_internal::WriteTypedBatch(short_output, batch, ExpectedColumns(), 1); },
	    "DuckDB output-column mismatch was accepted");
	Require(short_output.size() == 0, "output-column mismatch partially changed output cardinality");
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
	    []() { (void)duckdb::duckdb_api_query_internal::PlannedLogicalType({"value", "INTEGER", false, "value"}); },
	    "unsupported planned logical type was accepted");
}

void TestArrayVectorWritingAndCancellation() {
	duckdb::DataChunk output;
	InitializeArrayOutput(output);
	duckdb_api::TypedBatch batch;
	batch.column_types = {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::BOOLEAN, true),
	                      duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::BIGINT, false),
	                      duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::VARCHAR, false),
	                      duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::DOUBLE, false)};
	std::vector<duckdb_api::TypedScalarValue> flags;
	flags.push_back(duckdb_api::TypedScalarValue::Boolean(true));
	flags.push_back(duckdb_api::TypedScalarValue::Null(duckdb_api::ValueKind::BOOLEAN));
	flags.push_back(duckdb_api::TypedScalarValue::Boolean(false));
	std::vector<duckdb_api::TypedScalarValue> ids;
	ids.push_back(duckdb_api::TypedScalarValue::BigInt(7));
	ids.push_back(duckdb_api::TypedScalarValue::BigInt(7));
	std::vector<duckdb_api::TypedScalarValue> names;
	names.push_back(duckdb_api::TypedScalarValue::Varchar("alpha"));
	names.push_back(duckdb_api::TypedScalarValue::Varchar(""));
	std::vector<duckdb_api::TypedScalarValue> scores;
	scores.push_back(duckdb_api::TypedScalarValue::Double(0.0));
	scores.push_back(duckdb_api::TypedScalarValue::Double(1.5));
	batch.rows.push_back({{duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BOOLEAN, true, std::move(flags)),
	                       duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BIGINT, false, std::move(ids)),
	                       duckdb_api::TypedValue::Array(duckdb_api::ValueKind::VARCHAR, false, std::move(names)),
	                       duckdb_api::TypedValue::Array(duckdb_api::ValueKind::DOUBLE, false, std::move(scores))}});
	batch.rows.push_back(
	    {{duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BOOLEAN, true, {}),
	      duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BIGINT, false, {}),
	      duckdb_api::TypedValue::Null(duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::VARCHAR, false)),
	      duckdb_api::TypedValue::Array(duckdb_api::ValueKind::DOUBLE, false, {})}});
	batch.rows.push_back({{duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BOOLEAN, true,
	                                                     {duckdb_api::TypedScalarValue::Boolean(false)}),
	                       duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BIGINT, false,
	                                                     {duckdb_api::TypedScalarValue::BigInt(9)}),
	                       duckdb_api::TypedValue::Array(duckdb_api::ValueKind::VARCHAR, false,
	                                                     {duckdb_api::TypedScalarValue::Varchar("omega")}),
	                       duckdb_api::TypedValue::Array(duckdb_api::ValueKind::DOUBLE, false,
	                                                     {duckdb_api::TypedScalarValue::Double(2.5)})}});

	duckdb::duckdb_api_query_internal::WriteTypedBatch(output, batch, ExpectedArrayColumns(), 3);
	const auto flags_value = output.GetValue(0, 0);
	const auto ids_value = output.GetValue(1, 0);
	const auto names_value = output.GetValue(2, 0);
	const auto scores_value = output.GetValue(3, 0);
	const auto &written_flags = duckdb::ListValue::GetChildren(flags_value);
	const auto &written_ids = duckdb::ListValue::GetChildren(ids_value);
	const auto &written_names = duckdb::ListValue::GetChildren(names_value);
	const auto &written_scores = duckdb::ListValue::GetChildren(scores_value);
	const auto *flag_entries = duckdb::ListVector::GetData(output.data[0]);
	Require(output.size() == 3 && written_flags.size() == 3 && written_flags[0].GetValue<bool>() &&
	            written_flags[1].IsNull() && !written_flags[2].GetValue<bool>() && written_ids.size() == 2 &&
	            written_ids[0].GetValue<int64_t>() == 7 && written_ids[1].GetValue<int64_t>() == 7 &&
	            written_names.size() == 2 && written_names[0].GetValue<std::string>() == "alpha" &&
	            written_names[1].GetValue<std::string>().empty() && written_scores.size() == 2 &&
	            written_scores[0].GetValue<double>() == 0.0 && written_scores[1].GetValue<double>() == 1.5 &&
	            duckdb::ListValue::GetChildren(output.GetValue(0, 1)).empty() && output.GetValue(2, 1).IsNull() &&
	            duckdb::ListValue::GetChildren(output.GetValue(0, 2))[0].GetValue<bool>() == false &&
	            duckdb::ListValue::GetChildren(output.GetValue(1, 2))[0].GetValue<int64_t>() == 9 &&
	            duckdb::ListValue::GetChildren(output.GetValue(2, 2))[0].ToString() == "omega" &&
	            duckdb::ListValue::GetChildren(output.GetValue(3, 2))[0].GetValue<double>() == 2.5 &&
	            flag_entries[0].offset == 0 && flag_entries[0].length == 3 && flag_entries[1].offset == 3 &&
	            flag_entries[1].length == 0 && flag_entries[2].offset == 3 && flag_entries[2].length == 1,
	        "direct DuckDB list-vector writing changed array values, child NULLs, duplicates, empties, or outer NULL");

	duckdb::DataChunk cancelled_output;
	cancelled_output.Initialize(duckdb::Allocator::DefaultAllocator(),
	                            {duckdb::LogicalType::LIST(duckdb::LogicalType::BOOLEAN)});
	duckdb_api::TypedBatch cancellation_batch;
	cancellation_batch.column_types = {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::BOOLEAN, true)};
	cancellation_batch.rows.push_back({{duckdb_api::TypedValue::Array(duckdb_api::ValueKind::BOOLEAN, true,
	                                                                  {duckdb_api::TypedScalarValue::Boolean(true),
	                                                                   duckdb_api::TypedScalarValue::Boolean(false),
	                                                                   duckdb_api::TypedScalarValue::Boolean(true)})}});
	const std::vector<PlannedValueColumn> cancellation_columns = {
	    {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::BOOLEAN, true), false}};
	// Alignment accounts for checks 1-6, reservation/row traversal for 7-9,
	// and child zero for 10. Check 11 therefore cancels after one child has
	// already mutated DuckDB's private child vector but before publication.
	StepControl cancelled_after_one_child(11);
	bool cancelled = false;
	try {
		duckdb::duckdb_api_query_internal::WriteTypedBatch(cancelled_output, cancellation_batch, cancellation_columns,
		                                                   1, cancelled_after_one_child);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancelled = true;
	}
	Require(cancelled && cancelled_output.size() == 0 &&
	            duckdb::ListVector::GetEntry(cancelled_output.data[0]).GetValue(0).GetValue<bool>(),
	        "Query ARRAY child transfer published a partial batch after cancellation");

	auto invalid = batch;
	invalid.rows[0].values[1].elements[0] = duckdb_api::TypedScalarValue::Null(duckdb_api::ValueKind::BIGINT);
	duckdb::DataChunk rejected_output;
	InitializeArrayOutput(rejected_output);
	RequireLogicError(
	    [&]() {
		    duckdb::duckdb_api_query_internal::WriteTypedBatch(rejected_output, invalid, ExpectedArrayColumns(), 3);
	    },
	    "non-nullable ARRAY child NULL was accepted");
	Require(rejected_output.size() == 0, "invalid ARRAY batch changed output cardinality");
}

} // namespace

int main() {
	try {
		TestNullAndSentinelCounterexamples();
		TestRequiredNullsAndKindDriftFailBeforeWrites();
		TestArityAndBatchBoundsFailClosed();
		TestPlannedLogicalTypeMappingIsClosed();
		TestArrayVectorWritingAndCancellation();
		std::cout << "typed value adapter tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "typed value adapter tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
