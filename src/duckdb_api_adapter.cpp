#include "duckdb_api_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/scan_request.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb {
namespace {

// Call-scoped DuckDB coupling. Runtime receives only this non-owning control
// interface and cannot retain ClientContext or throw a DuckDB exception.
class DuckdbExecutionControl : public duckdb_api::ExecutionControl {
public:
	explicit DuckdbExecutionControl(ClientContext &context_p) : context(context_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		try {
			return context.IsInterrupted();
		} catch (...) {
			return true;
		}
	}

private:
	ClientContext &context;
};

// Registration state retains only immutable provider APIs. Product and private
// controlled composition both enter through this boundary.
struct DuckdbApiFunctionInfo : public TableFunctionInfo {
	DuckdbApiFunctionInfo(duckdb_api::CompiledConnector connector_p,
	                      std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : connector(std::move(connector_p)), executor(std::move(executor_p)) {
	}

	const duckdb_api::CompiledConnector connector;
	const std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

// Bind state freezes Query's conservative request and the complete provider
// plan. Copy preserves prepared-statement meaning without consulting metadata,
// environment, runtime state, or the network again.
struct DuckdbApiBindData : public TableFunctionData {
	DuckdbApiBindData(duckdb_api::ScanRequest request_p, duckdb_api::ScanPlan plan_p,
	                  std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : request(std::move(request_p)), plan(std::move(plan_p)), executor(std::move(executor_p)) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<DuckdbApiBindData>(request, plan, executor);
	}

	const duckdb_api::ScanRequest request;
	const duckdb_api::ScanPlan plan;
	const std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

// One DuckDB source task exclusively owns one mutable stream. Destruction is a
// non-throwing finalizer for success, failure, early close, and connection
// teardown; unfinished streams receive cancellation before close.
struct DuckdbApiGlobalState : public GlobalTableFunctionState {
	DuckdbApiGlobalState(std::unique_ptr<duckdb_api::BatchStream> stream_p,
	                     std::vector<duckdb_api::ValueKind> expected_kinds_p, uint64_t max_batch_rows_p,
	                     std::string connector_name_p, std::string relation_name_p)
	    : stream(std::move(stream_p)), expected_kinds(std::move(expected_kinds_p)), max_batch_rows(max_batch_rows_p),
	      connector_name(std::move(connector_name_p)), relation_name(std::move(relation_name_p)), finished(false) {
	}

	~DuckdbApiGlobalState() override {
		if (!stream) {
			return;
		}
		if (!finished) {
			stream->Cancel();
		}
		stream->Close();
	}

	idx_t MaxThreads() const override {
		return 1;
	}

	std::unique_ptr<duckdb_api::BatchStream> stream;
	const std::vector<duckdb_api::ValueKind> expected_kinds;
	const uint64_t max_batch_rows;
	const std::string connector_name;
	const std::string relation_name;
	bool finished;
};

std::string RequiredNamedString(TableFunctionBindInput &input, const std::string &name) {
	const auto entry = input.named_parameters.find(name);
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		throw BinderException("[duckdb_api][bind] required named argument %s is missing", name);
	}
	return StringValue::Get(entry->second);
}

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
	}
	return "internal";
}

[[noreturn]] void ThrowExecutionError(const duckdb_api::ExecutionError &error, const std::string &connector_name,
                                      const std::string &relation_name) {
	if (error.Stage() == duckdb_api::ErrorStage::INTERNAL) {
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            connector_name, relation_name);
	}
	if (error.Field().empty()) {
		throw InvalidInputException("[duckdb_api][%s] connector=%s relation=%s: %s", ErrorStageName(error.Stage()),
		                            connector_name, relation_name, error.SafeMessage());
	}
	throw InvalidInputException("[duckdb_api][%s] connector=%s relation=%s field=%s: %s", ErrorStageName(error.Stage()),
	                            connector_name, relation_name, error.Field(), error.SafeMessage());
}

LogicalType PlannedLogicalType(const duckdb_api::PlannedColumn &column) {
	if (column.logical_type == "BIGINT") {
		return LogicalType::BIGINT;
	}
	if (column.logical_type == "VARCHAR") {
		return LogicalType::VARCHAR;
	}
	if (column.logical_type == "BOOLEAN") {
		return LogicalType::BOOLEAN;
	}
	throw InternalException("duckdb_api scan plan contains an unsupported logical type");
}

duckdb_api::ValueKind PlannedValueKind(const duckdb_api::PlannedColumn &column) {
	if (column.logical_type == "BIGINT") {
		return duckdb_api::ValueKind::BIGINT;
	}
	if (column.logical_type == "VARCHAR") {
		return duckdb_api::ValueKind::VARCHAR;
	}
	if (column.logical_type == "BOOLEAN") {
		return duckdb_api::ValueKind::BOOLEAN;
	}
	throw InternalException("duckdb_api scan plan contains an unsupported logical type");
}

std::vector<duckdb_api::ValueKind> PlannedValueKinds(const duckdb_api::ScanPlan &plan) {
	std::vector<duckdb_api::ValueKind> result;
	result.reserve(plan.OutputColumns().size());
	for (const auto &column : plan.OutputColumns()) {
		result.push_back(PlannedValueKind(column));
	}
	return result;
}

[[noreturn]] void ThrowCancellation(duckdb_api::BatchStream *stream) {
	if (stream) {
		stream->Cancel();
	}
	throw InterruptException();
}

unique_ptr<FunctionData> DuckdbApiBind(ClientContext &, TableFunctionBindInput &input,
                                       vector<LogicalType> &return_types, vector<string> &names) {
	const auto connector_name = RequiredNamedString(input, "connector");
	const auto relation_name = RequiredNamedString(input, "relation");
	if (!input.info) {
		throw InternalException("duckdb_api table function is missing immutable function information");
	}
	auto &function_info = input.info->Cast<DuckdbApiFunctionInfo>();
	if (connector_name != function_info.connector.connector_name) {
		throw BinderException("[duckdb_api][bind] unknown connector identifier");
	}
	if (relation_name != function_info.connector.relation_name) {
		throw BinderException("[duckdb_api][bind] connector=%s: unknown relation identifier", connector_name);
	}
	if (!function_info.executor) {
		throw InternalException("duckdb_api table function is missing its scan executor");
	}

	// Bind performs deterministic metadata planning only. Executor open and all
	// network authority remain deferred until DuckdbApiInit.
	auto request = duckdb_api::BuildConservativeScanRequest(function_info.connector);
	auto plan = duckdb_api::BuildConservativeScanPlan(function_info.connector, request);

	for (const auto &column : plan.OutputColumns()) {
		names.push_back(column.name);
		return_types.push_back(PlannedLogicalType(column));
	}
	return make_uniq<DuckdbApiBindData>(std::move(request), std::move(plan), function_info.executor);
}

unique_ptr<GlobalTableFunctionState> DuckdbApiInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	try {
		DuckdbExecutionControl control(context);
		auto stream = bind_data.executor->Open(bind_data.plan, control);
		if (!stream) {
			throw std::logic_error("scan executor returned no stream");
		}
		return make_uniq<DuckdbApiGlobalState>(std::move(stream), PlannedValueKinds(bind_data.plan),
		                                       bind_data.plan.Budgets().batch_rows, bind_data.plan.ConnectorName(),
		                                       bind_data.plan.RelationName());
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(nullptr);
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error, bind_data.plan.ConnectorName(), bind_data.plan.RelationName());
	} catch (const std::exception &) {
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            bind_data.plan.ConnectorName(), bind_data.plan.RelationName());
	} catch (...) {
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            bind_data.plan.ConnectorName(), bind_data.plan.RelationName());
	}
}

Value DuckdbValue(const duckdb_api::TypedValue &value) {
	switch (value.kind) {
	case duckdb_api::ValueKind::BIGINT:
		return Value::BIGINT(value.bigint_value);
	case duckdb_api::ValueKind::VARCHAR:
		return Value(value.varchar_value);
	case duckdb_api::ValueKind::BOOLEAN:
		return Value::BOOLEAN(value.boolean_value);
	}
	throw InternalException("duckdb_api runtime returned an unknown typed value");
}

void DuckdbApiScan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<DuckdbApiGlobalState>();
	duckdb_api::TypedBatch batch;
	try {
		DuckdbExecutionControl control(context);
		if (!state.stream->Next(control, batch)) {
			state.finished = true;
			return;
		}
		if (!batch.IsSchemaAligned() || batch.column_kinds != state.expected_kinds ||
		    batch.rows.size() > state.max_batch_rows || batch.rows.size() > STANDARD_VECTOR_SIZE) {
			throw std::logic_error("batch stream violated its bound schema or row ceiling");
		}
		for (idx_t row_index = 0; row_index < batch.rows.size(); row_index++) {
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
			for (idx_t column_index = 0; column_index < batch.rows[row_index].values.size(); column_index++) {
				output.SetValue(column_index, row_index, DuckdbValue(batch.rows[row_index].values[column_index]));
			}
		}
		output.SetCardinality(batch.rows.size());
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(state.stream.get());
	} catch (const InterruptException &) {
		ThrowCancellation(state.stream.get());
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error, state.connector_name, state.relation_name);
	} catch (const std::exception &) {
		state.stream->Cancel();
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            state.connector_name, state.relation_name);
	} catch (...) {
		state.stream->Cancel();
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            state.connector_name, state.relation_name);
	}
}

} // namespace

void RegisterDuckdbApi(ExtensionLoader &loader, duckdb_api::CompiledConnector connector,
                       std::shared_ptr<const duckdb_api::ScanExecutor> executor) {
	if (!executor) {
		throw InternalException("duckdb_api registration requires a scan executor");
	}
	TableFunction scan("duckdb_api_scan", {}, DuckdbApiScan, DuckdbApiBind, DuckdbApiInit);
	scan.named_parameters["connector"] = LogicalType::VARCHAR;
	scan.named_parameters["relation"] = LogicalType::VARCHAR;
	scan.projection_pushdown = false;
	scan.filter_pushdown = false;
	scan.filter_prune = false;
	scan.function_info = make_shared_ptr<DuckdbApiFunctionInfo>(std::move(connector), std::move(executor));
	loader.RegisterFunction(std::move(scan));
}

} // namespace duckdb
