#include "relation_execution.hpp"

#include "typed_value_adapter.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb_api/duckdb_secret.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

const char *ErrorStageName(duckdb_api::ErrorStage stage) {
	switch (stage) {
	case duckdb_api::ErrorStage::TRANSPORT:
		return "transport";
	case duckdb_api::ErrorStage::HTTP_STATUS:
		return "http_status";
	case duckdb_api::ErrorStage::DECODE:
		return "decode";
	case duckdb_api::ErrorStage::SCHEMA:
		return "schema";
	case duckdb_api::ErrorStage::POLICY:
		return "policy";
	case duckdb_api::ErrorStage::RESOURCE:
		return "resource";
	case duckdb_api::ErrorStage::INTERNAL:
		return "internal";
	case duckdb_api::ErrorStage::AUTHENTICATION:
		return "authentication";
	case duckdb_api::ErrorStage::AUTHORIZATION:
		return "authorization";
	case duckdb_api::ErrorStage::REMOTE_PROTOCOL:
		return "remote_protocol";
	}
	return "internal";
}

// RFC 0021: append the additive structured resilience suffix for a classified
// failure. Every token is a closed code or count drawn from the freeze-bound
// vocabulary; never content. Appended after the preserved stage/field/message
// prefix so existing rendered strings stay verbatim. Carries the primary class,
// the cumulative rows exposed to DuckDB, and the terminating budget dimension
// (when a budget terminated execution) — the facts the success signals require.
// The full property set remains available programmatically via Properties().
std::string ResilienceSuffix(const duckdb_api::FailureProperties &properties) {
	std::string suffix = " [class=";
	suffix += duckdb_api::FailureClassName(properties.failure_class);
	suffix += " attempt=" + std::to_string(properties.attempt);
	suffix += " cumulative_delay_ms=" + std::to_string(properties.cumulative_delay_milliseconds);
	suffix += " exposure=";
	suffix += duckdb_api::ExposureStateName(properties.exposure_state);
	suffix += " rows_exposed=" + std::to_string(properties.rows_exposed);
	if (properties.terminating_budget != duckdb_api::BudgetDimension::NONE) {
		suffix += " budget=";
		suffix += duckdb_api::BudgetDimensionName(properties.terminating_budget);
	}
	suffix += "]";
	return suffix;
}

[[noreturn]] void ThrowExecutionError(const duckdb_api::ExecutionError &error, const std::string &connector,
                                      const std::string &relation) {
	if (error.Stage() == duckdb_api::ErrorStage::INTERNAL) {
		throw duckdb::InvalidInputException(
		    "[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure", connector, relation);
	}
	std::string message = error.SafeMessage();
	if (error.Classified()) {
		message += ResilienceSuffix(error.Properties());
	}
	if (error.Field().empty()) {
		throw duckdb::InvalidInputException("[duckdb_api][%s] connector=%s relation=%s: %s",
		                                    ErrorStageName(error.Stage()), connector, relation, message);
	}
	throw duckdb::InvalidInputException("[duckdb_api][%s] connector=%s relation=%s field=%s: %s",
	                                    ErrorStageName(error.Stage()), connector, relation, error.Field(), message);
}

[[noreturn]] void ThrowCancellation(duckdb_api::BatchStream *stream) {
	if (stream) {
		stream->Cancel();
	}
	throw duckdb::InterruptException();
}

std::unique_ptr<duckdb_api::BatchStream>
OpenAuthorizedStream(const duckdb_api::ScanPlan &plan, const std::shared_ptr<const duckdb_api::ScanExecutor> &executor,
                     duckdb::ClientContext &context, DuckdbExecutionControl &control) {
	if (!executor) {
		throw std::logic_error("relation execution is missing its scan executor");
	}
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
	if (plan.Authentication() == duckdb_api::FeatureState::ENABLED) {
		const auto &reference = plan.SecretReference();
		if (!reference.IsPresent()) {
			throw std::logic_error("authenticated scan plan has no logical secret reference");
		}
		auto provider = duckdb::CreateDuckdbApiCredentialProvider(context);
		return executor->OpenWithCredentialProvider(plan, *provider, control);
	}
	if (plan.Authentication() != duckdb_api::FeatureState::DISABLED) {
		throw std::logic_error("scan plan has an unknown authentication state");
	}
	return executor->Open(plan, control);
}

// One DuckDB source task exclusively owns one mutable stream. Destruction is a
// non-throwing finalizer for success, failure, early close, and connection
// teardown; unfinished streams receive cancellation before close.
struct RelationExecutionState final : public duckdb::GlobalTableFunctionState {
	RelationExecutionState(std::unique_ptr<duckdb_api::BatchStream> stream_p,
	                       std::vector<PlannedValueColumn> expected_columns_p, std::uint64_t max_batch_rows_p,
	                       std::string connector_p, std::string relation_p)
	    : stream(std::move(stream_p)), expected_columns(std::move(expected_columns_p)),
	      max_batch_rows(max_batch_rows_p), connector(std::move(connector_p)), relation(std::move(relation_p)),
	      finished(false) {
	}

	~RelationExecutionState() override {
		if (!stream) {
			return;
		}
		if (!finished) {
			stream->Cancel();
		}
		stream->Close();
	}

	duckdb::idx_t MaxThreads() const override {
		return 1;
	}

	std::unique_ptr<duckdb_api::BatchStream> stream;
	const std::vector<PlannedValueColumn> expected_columns;
	const std::uint64_t max_batch_rows;
	const std::string connector;
	const std::string relation;
	bool finished;
};

} // namespace

DuckdbExecutionControl::DuckdbExecutionControl(ClientContext &context_p) : context(context_p) {
}

bool DuckdbExecutionControl::IsCancellationRequested() const noexcept {
	try {
		return context.IsInterrupted();
	} catch (...) {
		return true;
	}
}

unique_ptr<GlobalTableFunctionState>
InitializeRelationExecution(ClientContext &context, const duckdb_api::ScanPlan &plan,
                            const std::shared_ptr<const duckdb_api::ScanExecutor> &executor) {
	try {
		DuckdbExecutionControl control(context);
		auto stream = OpenAuthorizedStream(plan, executor, context, control);
		if (!stream) {
			throw std::logic_error("scan executor returned no stream");
		}
		return make_uniq<RelationExecutionState>(std::move(stream), PlannedValueColumns(plan),
		                                         plan.Budgets().batch_rows, plan.ConnectorName(), plan.RelationName());
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(nullptr);
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error, plan.ConnectorName(), plan.RelationName());
	} catch (const std::exception &) {
		throw duckdb::InvalidInputException(
		    "[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure", plan.ConnectorName(),
		    plan.RelationName());
	} catch (...) {
		throw duckdb::InvalidInputException(
		    "[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure", plan.ConnectorName(),
		    plan.RelationName());
	}
}

void ScanRelationExecution(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<RelationExecutionState>();
	duckdb_api::TypedBatch batch;
	try {
		DuckdbExecutionControl control(context);
		const auto produced = state.stream->Next(control, batch);
		if (!produced) {
			if (!batch.rows.empty()) {
				throw std::logic_error("batch stream returned rows with clean exhaustion");
			}
			state.finished = true;
			return;
		}
		for (duckdb::idx_t row_index = 0; row_index < batch.rows.size(); row_index++) {
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
		}
		WriteTypedBatch(output, batch, state.expected_columns, state.max_batch_rows, control);
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(state.stream.get());
	} catch (const duckdb::InterruptException &) {
		ThrowCancellation(state.stream.get());
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error, state.connector, state.relation);
	} catch (const std::exception &) {
		state.stream->Cancel();
		throw duckdb::InvalidInputException(
		    "[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure", state.connector,
		    state.relation);
	} catch (...) {
		state.stream->Cancel();
		throw duckdb::InvalidInputException(
		    "[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure", state.connector,
		    state.relation);
	}
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
