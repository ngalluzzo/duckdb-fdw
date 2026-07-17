#define DUCKDB_EXTENSION_MAIN

#include "duckdb_api_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/embedded_example.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace duckdb {
namespace {

class EmbeddedFixtureSource : public duckdb_api::FixtureSource {
public:
	const std::string &ContentDigest() const override {
		static const std::string digest(duckdb_api::EXAMPLE_FIXTURE_SHA256);
		return digest;
	}

	void Read(duckdb_api::FixtureReadBuffer &buffer) override {
		buffer.Append(duckdb_api::EXAMPLE_FIXTURE);
	}
};

class EmbeddedFixtureFactory : public duckdb_api::FixtureFactory {
public:
	const std::string &ContentDigest() const override {
		static const std::string digest(duckdb_api::EXAMPLE_FIXTURE_SHA256);
		return digest;
	}

	std::unique_ptr<duckdb_api::FixtureSource> Open() const override {
		return std::unique_ptr<duckdb_api::FixtureSource>(new EmbeddedFixtureSource());
	}
};

struct DuckdbApiFunctionInfo : public TableFunctionInfo {
	DuckdbApiFunctionInfo(duckdb_api::CompiledConnector connector_p,
	                      shared_ptr<duckdb_api::FixtureFactory> fixture_factory_p)
	    : connector(std::move(connector_p)), fixture_factory(std::move(fixture_factory_p)) {
	}

	const duckdb_api::CompiledConnector connector;
	shared_ptr<duckdb_api::FixtureFactory> fixture_factory;
};

struct DuckdbApiBindData : public TableFunctionData {
	DuckdbApiBindData(duckdb_api::CompiledConnector connector_p, duckdb_api::ScanRequest request_p,
	                  duckdb_api::ScanPlan plan_p, shared_ptr<duckdb_api::FixtureFactory> fixture_factory_p)
	    : connector(std::move(connector_p)), request(std::move(request_p)), plan(std::move(plan_p)),
	      fixture_factory(std::move(fixture_factory_p)) {
	}

	duckdb_api::CompiledConnector connector;
	duckdb_api::ScanRequest request;
	duckdb_api::ScanPlan plan;
	shared_ptr<duckdb_api::FixtureFactory> fixture_factory;
};

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
	if (!function_info.fixture_factory) {
		throw InternalException("duckdb_api table function is missing its fixture factory");
	}

	auto connector = function_info.connector;
	auto request = duckdb_api::BuildConservativeScanRequest();
	auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);

	names = {"id", "name", "active"};
	return_types = {LogicalType::BIGINT, LogicalType::VARCHAR, LogicalType::BOOLEAN};
	return make_uniq<DuckdbApiBindData>(std::move(connector), std::move(request), std::move(plan),
	                                    function_info.fixture_factory);
}

unique_ptr<GlobalTableFunctionState> DuckdbApiInit(ClientContext &, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	try {
		return make_uniq<DuckdbApiGlobalState>(duckdb_api::OpenBatchStream(bind_data.plan, *bind_data.fixture_factory));
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
	std::vector<duckdb_api::ItemRow> rows;
	try {
		if (!state.stream->Next(context, rows)) {
			return;
		}
		if (rows.size() > STANDARD_VECTOR_SIZE) {
			throw std::logic_error("batch stream exceeded DuckDB's vector size");
		}
		for (idx_t index = 0; index < rows.size(); index++) {
			if (context.IsInterrupted()) {
				state.stream->Cancel();
				throw InterruptException();
			}
			output.SetValue(0, index, Value::BIGINT(rows[index].id));
			output.SetValue(1, index, Value(rows[index].name));
			output.SetValue(2, index, Value::BOOLEAN(rows[index].active));
		}
		output.SetCardinality(rows.size());
	} catch (const InterruptException &) {
		state.stream->Cancel();
		throw;
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

void RegisterDuckdbApi(ExtensionLoader &loader, shared_ptr<duckdb_api::FixtureFactory> fixture_factory) {
	if (!fixture_factory) {
		throw InternalException("duckdb_api registration requires a fixture factory");
	}
	auto connector = duckdb_api::BuildCompiledConnector(fixture_factory->ContentDigest());
	TableFunction scan("duckdb_api_scan", {}, DuckdbApiScan, DuckdbApiBind, DuckdbApiInit);
	scan.named_parameters["connector"] = LogicalType::VARCHAR;
	scan.named_parameters["relation"] = LogicalType::VARCHAR;
	scan.projection_pushdown = false;
	scan.filter_pushdown = false;
	scan.filter_prune = false;
	scan.function_info = make_shared_ptr<DuckdbApiFunctionInfo>(std::move(connector), std::move(fixture_factory));
	loader.RegisterFunction(std::move(scan));
}

void DuckdbApiExtension::Load(ExtensionLoader &loader) {
	RegisterDuckdbApi(loader, make_shared_ptr<EmbeddedFixtureFactory>());
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
	duckdb::RegisterDuckdbApi(loader, duckdb::make_shared_ptr<duckdb::EmbeddedFixtureFactory>());
}
}
