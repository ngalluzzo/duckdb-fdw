#define DUCKDB_EXTENSION_MAIN

#include "live_rest_product_proof_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb {
namespace {

const char *const DEFAULT_AUTHORITY = "https://api.github.com";
const char *const AUTHORITY_ENVIRONMENT = "DUCKDB_API_LIVE_PROOF_AUTHORITY";

// Owned by DuckDB bind state for the complete query. The plan is fully built
// during bind and is never modified or enriched from network state.
struct LiveRestBindData final : public TableFunctionData {
	explicit LiveRestBindData(live_rest::LiveScanPlan plan_p) : plan(std::move(plan_p)) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<LiveRestBindData>(plan);
	}

	const live_rest::LiveScanPlan plan;
};

// A call-scoped view over DuckDB's cancellation flag. Runtime and transport
// interfaces are forbidden from retaining this reference after the call.
class DuckdbCancellationView final : public live_rest::CancellationView {
public:
	explicit DuckdbCancellationView(const ClientContext &context_p) : context(context_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		try {
			return context.IsInterrupted();
		} catch (...) {
			// A cancellation adapter must never unwind through a C callback.
			return true;
		}
	}

private:
	const ClientContext &context;
};

// DuckDB global scan state owns the runtime executor and its one pull stream.
// Destruction is the final exception-safe cleanup boundary for success,
// runtime failure, cancellation, and connection teardown.
struct LiveRestGlobalState final : public GlobalTableFunctionState {
	LiveRestGlobalState(std::shared_ptr<const live_rest::ScanExecutor> executor_p,
	                    std::unique_ptr<live_rest::BatchStream> stream_p)
	    : executor(std::move(executor_p)), stream(std::move(stream_p)) {
	}

	~LiveRestGlobalState() override {
		if (stream) {
			stream->Cancel();
			stream->Close();
		}
	}

	std::shared_ptr<const live_rest::ScanExecutor> executor;
	std::unique_ptr<live_rest::BatchStream> stream;
};

[[noreturn]] void ThrowRuntimeError(const live_rest::RuntimeError &error) {
	throw IOException("live REST proof execution failed: %s", error.what());
}

void ValidateFixedSchema(const live_rest::LiveScanPlan &plan) {
	const auto valid = plan.columns.size() == 3 && plan.columns[0].name == "id" &&
	                   plan.columns[0].type == live_rest::ColumnType::BIGINT && plan.columns[1].name == "login" &&
	                   plan.columns[1].type == live_rest::ColumnType::VARCHAR && plan.columns[2].name == "site_admin" &&
	                   plan.columns[2].type == live_rest::ColumnType::BOOLEAN;
	if (!valid) {
		throw InternalException("live REST proof plan returned an unexpected schema");
	}
}

unique_ptr<FunctionData> LiveRestBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &return_types,
	                                  vector<string> &names) {
	const char *configured_authority = std::getenv(AUTHORITY_ENVIRONMENT);
	const std::string authority = configured_authority ? configured_authority : DEFAULT_AUTHORITY;

	try {
		auto plan = live_rest::BuildLiveScanPlan(authority);
		ValidateFixedSchema(plan);
		names.emplace_back("id");
		return_types.emplace_back(LogicalType::BIGINT);
		names.emplace_back("login");
		return_types.emplace_back(LogicalType::VARCHAR);
		names.emplace_back("site_admin");
		return_types.emplace_back(LogicalType::BOOLEAN);
		return make_uniq<LiveRestBindData>(std::move(plan));
	} catch (const std::invalid_argument &) {
		// Do not echo an environment-controlled authority into the diagnostic.
		throw InvalidInputException("live REST proof rejected the configured authority");
	} catch (...) {
		throw IOException("live REST proof planning failed unexpectedly");
	}
}

unique_ptr<GlobalTableFunctionState> LiveRestInit(ClientContext &context, TableFunctionInitInput &input) {
	const auto &bind_data = input.bind_data->Cast<LiveRestBindData>();
	DuckdbCancellationView cancellation(context);
	try {
		auto executor = live_rest::BuildScanExecutor(BuildCurlHttpTransport());
		auto stream = executor->Open(bind_data.plan, cancellation);
		return make_uniq<LiveRestGlobalState>(std::move(executor), std::move(stream));
	} catch (const live_rest::ExecutionCancelled &) {
		throw InterruptException();
	} catch (const live_rest::RuntimeError &error) {
		ThrowRuntimeError(error);
	} catch (...) {
		throw IOException("live REST proof initialization failed unexpectedly");
	}
}

void LiveRestScan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<LiveRestGlobalState>();
	DuckdbCancellationView cancellation(context);
	std::vector<live_rest::LiveRow> rows;

	try {
		if (!state.stream->Next(cancellation, rows)) {
			return;
		}
		if (rows.size() > STANDARD_VECTOR_SIZE) {
			throw InternalException("live REST proof runtime exceeded the DuckDB vector bound");
		}
		for (idx_t index = 0; index < rows.size(); index++) {
			output.SetValue(0, index, Value::BIGINT(rows[index].id));
			output.SetValue(1, index, Value(rows[index].login));
			output.SetValue(2, index, Value::BOOLEAN(rows[index].site_admin));
		}
		output.SetCardinality(rows.size());
	} catch (const live_rest::ExecutionCancelled &) {
		state.stream->Cancel();
		throw InterruptException();
	} catch (const live_rest::RuntimeError &error) {
		ThrowRuntimeError(error);
	} catch (...) {
		state.stream->Cancel();
		state.stream->Close();
		throw IOException("live REST proof execution failed unexpectedly");
	}
}

void LoadInternal(ExtensionLoader &loader) {
	TableFunction proof("duckdb_api_live_rest_proof", {}, LiveRestScan, LiveRestBind, LiveRestInit);
	loader.RegisterFunction(proof);
}

} // namespace

void LiveRestProductProofExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string LiveRestProductProofExtension::Name() {
	return "live_rest_product_proof";
}

std::string LiveRestProductProofExtension::Version() const {
#ifdef EXT_VERSION_LIVE_REST_PRODUCT_PROOF
	return EXT_VERSION_LIVE_REST_PRODUCT_PROOF;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(live_rest_product_proof, loader) {
	duckdb::LoadInternal(loader);
}
}
