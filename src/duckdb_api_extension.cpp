#define DUCKDB_EXTENSION_MAIN

#include "duckdb_api_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/example_composition.hpp"
#include "duckdb_api/scan_request.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace duckdb {
namespace {

// Call-scoped DuckDB coupling. Runtime receives only the non-owning control
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

// Registration state is immutable and contains only connector metadata plus
// the abstract runtime service supplied by example composition.
struct DuckdbApiFunctionInfo : public TableFunctionInfo {
	DuckdbApiFunctionInfo(duckdb_api::CompiledConnector connector_p,
	                      std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : connector(std::move(connector_p)), executor(std::move(executor_p)) {
	}

	const duckdb_api::CompiledConnector connector;
	const std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

// Bind state freezes the side-effect-free request and plan for the later
// global-init callback. No concrete source or provider crosses this boundary.
struct DuckdbApiBindData : public TableFunctionData {
	DuckdbApiBindData(duckdb_api::ScanRequest request_p, duckdb_api::ScanPlan plan_p,
	                  std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : request(std::move(request_p)), plan(std::move(plan_p)), executor(std::move(executor_p)) {
	}

	const duckdb_api::ScanRequest request;
	const duckdb_api::ScanPlan plan;
	const std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

// One DuckDB source task exclusively owns one mutable stream. Close is a
// non-throwing finalizer for success, failure, early close, and destruction.
struct DuckdbApiGlobalState : public GlobalTableFunctionState {
	explicit DuckdbApiGlobalState(std::unique_ptr<duckdb_api::BatchStream> stream_p) : stream(std::move(stream_p)) {
	}

	~DuckdbApiGlobalState() override {
		if (stream) {
			stream->Close();
		}
	}

	idx_t MaxThreads() const override {
		return 1;
	}

	std::unique_ptr<duckdb_api::BatchStream> stream;
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
	case duckdb_api::ErrorStage::DECODE:
		return "decode";
	case duckdb_api::ErrorStage::SCHEMA:
		return "schema";
	case duckdb_api::ErrorStage::POLICY:
		return "policy";
	case duckdb_api::ErrorStage::INTERNAL:
		return "internal";
	}
	return "internal";
}

[[noreturn]] void ThrowExecutionError(const duckdb_api::ExecutionError &error) {
	if (error.Stage() == duckdb_api::ErrorStage::INTERNAL) {
		throw InvalidInputException(
		    "[duckdb_api][internal] connector=example relation=items: unexpected execution failure");
	}
	throw InvalidInputException("[duckdb_api][%s] connector=example relation=items: %s", ErrorStageName(error.Stage()),
	                            error.SafeMessage());
}

LogicalType ConnectorLogicalType(const duckdb_api::CompiledColumn &column) {
	if (column.logical_type == "BIGINT") {
		return LogicalType::BIGINT;
	}
	if (column.logical_type == "VARCHAR") {
		return LogicalType::VARCHAR;
	}
	if (column.logical_type == "BOOLEAN") {
		return LogicalType::BOOLEAN;
	}
	throw InternalException("duckdb_api compiled connector contains an unsupported logical type");
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
	if (connector_name != "example") {
		throw BinderException("[duckdb_api][bind] unknown connector identifier");
	}
	if (relation_name != "items") {
		throw BinderException("[duckdb_api][bind] connector=example: unknown relation identifier");
	}
	if (!input.info) {
		throw InternalException("duckdb_api table function is missing immutable function information");
	}
	auto &function_info = input.info->Cast<DuckdbApiFunctionInfo>();
	if (!function_info.executor) {
		throw InternalException("duckdb_api table function is missing its scan executor");
	}

	// Bind performs deterministic metadata planning only; source acquisition is
	// deferred to DuckdbApiInit.
	auto request = duckdb_api::BuildConservativeScanRequest();
	auto plan = duckdb_api::BuildConservativeScanPlan(function_info.connector, request);

	for (const auto &column : function_info.connector.columns) {
		names.push_back(column.name);
		return_types.push_back(ConnectorLogicalType(column));
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
		return make_uniq<DuckdbApiGlobalState>(std::move(stream));
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(nullptr);
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error);
	} catch (const std::exception &) {
		throw InvalidInputException(
		    "[duckdb_api][internal] connector=example relation=items: unexpected execution failure");
	} catch (...) {
		throw InvalidInputException(
		    "[duckdb_api][internal] connector=example relation=items: unexpected execution failure");
	}
}

void DuckdbApiScan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<DuckdbApiGlobalState>();
	// Rows are callback-local and copied into DuckDB-owned vectors before Next
	// returns control to the engine.
	std::vector<duckdb_api::ItemRow> rows;
	try {
		DuckdbExecutionControl control(context);
		if (!state.stream->Next(control, rows)) {
			return;
		}
		if (rows.size() > STANDARD_VECTOR_SIZE) {
			throw std::logic_error("batch stream exceeded DuckDB's vector size");
		}
		for (idx_t index = 0; index < rows.size(); index++) {
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
			output.SetValue(0, index, Value::BIGINT(rows[index].id));
			output.SetValue(1, index, Value(rows[index].name));
			output.SetValue(2, index, Value::BOOLEAN(rows[index].active));
		}
		output.SetCardinality(rows.size());
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(state.stream.get());
	} catch (const InterruptException &) {
		ThrowCancellation(state.stream.get());
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error);
	} catch (const std::exception &) {
		throw InvalidInputException(
		    "[duckdb_api][internal] connector=example relation=items: unexpected execution failure");
	} catch (...) {
		throw InvalidInputException(
		    "[duckdb_api][internal] connector=example relation=items: unexpected execution failure");
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

void DuckdbApiExtension::Load(ExtensionLoader &loader) {
	auto example = duckdb_api::BuildEmbeddedExampleComposition();
	RegisterDuckdbApi(loader, std::move(example.connector), std::move(example.executor));
}

std::string DuckdbApiExtension::Name() {
	return "duckdb_api";
}

std::string DuckdbApiExtension::Version() const {
#ifdef EXT_VERSION_DUCKDB_API
	return EXT_VERSION_DUCKDB_API;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(duckdb_api, loader) {
	auto example = duckdb_api::BuildEmbeddedExampleComposition();
	duckdb::RegisterDuckdbApi(loader, std::move(example.connector), std::move(example.executor));
}
}
