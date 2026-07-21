#include "package_fixture_execution.hpp"

#include "runtime/support/package_fixture_json_variant_internal.hpp"
#include "runtime/support/package_fixture_observation_internal.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace duckdb_api_test {
namespace {

struct ColumnMutation {
	std::string replacement;
	bool remove_member;
	bool should_succeed;
	duckdb_api::ErrorStage failure_stage;
	RuntimeFixtureVariantOutcome outcome;
	uint64_t observed_units;
	uint64_t admitted_limit;
};

ColumnMutation BuildColumnMutation(const duckdb_api::ScanPlan &plan, const internal::RuntimeFixtureJsonColumn &column,
                                   RuntimeFixtureColumnVariant variant) {
	const auto string_limit = plan.Budgets().extracted_string_bytes;
	switch (variant) {
	case RuntimeFixtureColumnVariant::TYPE_MISMATCH_REJECTED:
		return {column.kind == duckdb_api::ValueKind::VARCHAR ? "false" : "\"type-mismatch\"",
		        false,
		        false,
		        duckdb_api::ErrorStage::SCHEMA,
		        RuntimeFixtureVariantOutcome::EXPECTED_REJECTION,
		        0,
		        0};
	case RuntimeFixtureColumnVariant::MISSING_REJECTED:
		if (column.nullable) {
			throw std::invalid_argument("missing-rejected column variant requires a non-nullable planned column");
		}
		return {"", true, false, duckdb_api::ErrorStage::SCHEMA, RuntimeFixtureVariantOutcome::EXPECTED_REJECTION,
		        0,  0};
	case RuntimeFixtureColumnVariant::NULL_REJECTED:
		if (column.nullable) {
			throw std::invalid_argument("null-rejected column variant requires a non-nullable planned column");
		}
		return {"null", false, false, duckdb_api::ErrorStage::SCHEMA, RuntimeFixtureVariantOutcome::EXPECTED_REJECTION,
		        0,      0};
	case RuntimeFixtureColumnVariant::BIGINT_MINIMUM:
		if (column.kind != duckdb_api::ValueKind::BIGINT) {
			throw std::invalid_argument("BIGINT minimum variant requires a BIGINT planned column");
		}
		return {"-9223372036854775808",
		        false,
		        true,
		        duckdb_api::ErrorStage::INTERNAL,
		        RuntimeFixtureVariantOutcome::VALUE_SUCCEEDED,
		        0,
		        0};
	case RuntimeFixtureColumnVariant::BIGINT_MAXIMUM:
		if (column.kind != duckdb_api::ValueKind::BIGINT) {
			throw std::invalid_argument("BIGINT maximum variant requires a BIGINT planned column");
		}
		return {"9223372036854775807",
		        false,
		        true,
		        duckdb_api::ErrorStage::INTERNAL,
		        RuntimeFixtureVariantOutcome::VALUE_SUCCEEDED,
		        0,
		        0};
	case RuntimeFixtureColumnVariant::BIGINT_UNDERFLOW_REJECTED:
	case RuntimeFixtureColumnVariant::BIGINT_OVERFLOW_REJECTED:
	case RuntimeFixtureColumnVariant::BIGINT_FRACTION_REJECTED:
		if (column.kind != duckdb_api::ValueKind::BIGINT) {
			throw std::invalid_argument("BIGINT rejection variant requires a BIGINT planned column");
		}
		return {variant == RuntimeFixtureColumnVariant::BIGINT_UNDERFLOW_REJECTED  ? "-9223372036854775809"
		        : variant == RuntimeFixtureColumnVariant::BIGINT_OVERFLOW_REJECTED ? "9223372036854775808"
		                                                                           : "1.5",
		        false,
		        false,
		        duckdb_api::ErrorStage::SCHEMA,
		        RuntimeFixtureVariantOutcome::EXPECTED_REJECTION,
		        0,
		        0};
	case RuntimeFixtureColumnVariant::VARCHAR_STRING_BUDGET_BOUNDARY:
	case RuntimeFixtureColumnVariant::VARCHAR_STRING_BUDGET_ONE_OVER_REJECTED: {
		if (column.kind != duckdb_api::ValueKind::VARCHAR || string_limit == 0 ||
		    string_limit >= static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
			throw std::invalid_argument("VARCHAR budget variant requires a bounded VARCHAR planned column");
		}
		const bool one_over = variant == RuntimeFixtureColumnVariant::VARCHAR_STRING_BUDGET_ONE_OVER_REJECTED;
		const auto units = string_limit + static_cast<uint64_t>(one_over);
		return {"\"" + std::string(static_cast<std::size_t>(units), 'x') + "\"",
		        false,
		        !one_over,
		        one_over ? duckdb_api::ErrorStage::RESOURCE : duckdb_api::ErrorStage::INTERNAL,
		        one_over ? RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED
		                 : RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED,
		        units,
		        string_limit};
	}
	}
	throw std::invalid_argument("unknown closed Runtime column variant");
}

void ValidateColumnObservation(const RuntimeFixtureExecutionObservation &execution,
                               const internal::RuntimeFixtureJsonColumn &column,
                               const RuntimeFixtureColumnScenario &scenario, const ColumnMutation &mutation) {
	if (mutation.should_succeed) {
		if (!execution.succeeded || execution.has_runtime_error || execution.cancellation_observed ||
		    execution.rows.empty() || scenario.column_ordinal >= execution.rows[0].values.size() ||
		    !execution.rows[0].values[scenario.column_ordinal].valid || !execution.stream_close_invoked) {
			throw std::logic_error("closed Runtime column variant did not produce its exact successful value");
		}
		const auto &value = execution.rows[0].values[scenario.column_ordinal];
		if (scenario.variant == RuntimeFixtureColumnVariant::BIGINT_MINIMUM &&
		    value.bigint_value != std::numeric_limits<int64_t>::min()) {
			throw std::logic_error("closed Runtime BIGINT minimum variant decoded the wrong value");
		}
		if (scenario.variant == RuntimeFixtureColumnVariant::BIGINT_MAXIMUM &&
		    value.bigint_value != std::numeric_limits<int64_t>::max()) {
			throw std::logic_error("closed Runtime BIGINT maximum variant decoded the wrong value");
		}
		if (scenario.variant == RuntimeFixtureColumnVariant::VARCHAR_STRING_BUDGET_BOUNDARY &&
		    value.varchar_value.size() != mutation.admitted_limit) {
			throw std::logic_error("closed Runtime VARCHAR boundary variant decoded the wrong byte count");
		}
		return;
	}
	if (execution.succeeded || execution.cancellation_observed || !execution.has_runtime_error ||
	    execution.runtime_error_stage != mutation.failure_stage || execution.runtime_error_field != column.name ||
	    !execution.rows.empty() || !execution.transport_observed || execution.request_count != 1 ||
	    !execution.stream_close_invoked) {
		throw std::logic_error("closed Runtime column rejection lost its exact stage, field, or all-or-nothing result");
	}
}

} // namespace

RuntimeFixtureVariantObservation RuntimePackageFixtureExecutionService::ExecuteColumnVariant(
    const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &transcript, RuntimeFixtureColumnScenario scenario,
    duckdb_api::ExecutionControl &control) const {
	internal::ValidateRuntimeFixtureTranscript(transcript);
	const auto shape = internal::AdmitRuntimeFixtureJsonShape(plan);
	if (scenario.column_ordinal >= shape.columns.size() || transcript.pages.empty()) {
		throw std::invalid_argument("closed Runtime fixture column scenario is outside the admitted response");
	}
	const auto &column = shape.columns[scenario.column_ordinal];
	const auto mutation = BuildColumnMutation(plan, column, scenario.variant);
	auto derived = transcript;
	derived.pages[0].body = internal::ReplaceFirstRuntimeFixtureColumn(
	    derived.pages[0].body, shape, scenario.column_ordinal, mutation.replacement, mutation.remove_member, control);
	auto execution =
	    internal::RunRuntimeFixtureScenario(plan, derived, RuntimeFixtureScenario::Standard(), control, true);
	ValidateColumnObservation(execution, column, scenario, mutation);
	return {std::move(execution),
	        mutation.outcome,
	        RuntimeFixtureVariantEvidencePath::EXECUTOR,
	        mutation.observed_units,
	        0,
	        mutation.admitted_limit};
}

} // namespace duckdb_api_test
