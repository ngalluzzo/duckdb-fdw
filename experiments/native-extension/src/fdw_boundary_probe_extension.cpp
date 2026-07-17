#define DUCKDB_EXTENSION_MAIN

#include "fdw_boundary_probe_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace duckdb {
namespace {

struct ProbeCounters {
	std::atomic<uint64_t> opened {0};
	std::atomic<uint64_t> closed {0};
	std::atomic<uint64_t> chunks {0};
	std::atomic<uint64_t> interruptions {0};
	std::atomic<uint64_t> active_waiters {0};
};

ProbeCounters probe_counters;

struct ProbeBindData : public TableFunctionData {
	ProbeBindData(idx_t row_count_p, idx_t batch_size_p, idx_t delay_ms_p, int64_t fail_after_p)
	    : row_count(row_count_p), batch_size(batch_size_p), delay_ms(delay_ms_p), fail_after(fail_after_p) {
	}

	idx_t row_count;
	idx_t batch_size;
	idx_t delay_ms;
	int64_t fail_after;
};

struct ProbeGlobalState : public GlobalTableFunctionState {
	ProbeGlobalState() {
		probe_counters.opened.fetch_add(1, std::memory_order_relaxed);
	}

	~ProbeGlobalState() override {
		probe_counters.closed.fetch_add(1, std::memory_order_relaxed);
	}

	idx_t offset = 0;
	idx_t batch_id = 0;
};

unique_ptr<FunctionData> ProbeBind(ClientContext &, TableFunctionBindInput &input, vector<LogicalType> &return_types,
                                   vector<string> &names) {
	const auto row_count = BigIntValue::Get(input.inputs[0]);
	const auto batch_size = BigIntValue::Get(input.inputs[1]);
	const auto delay_ms = BigIntValue::Get(input.inputs[2]);
	const auto fail_after = BigIntValue::Get(input.inputs[3]);

	if (row_count < 0) {
		throw InvalidInputException("boundary probe row_count must be non-negative");
	}
	if (batch_size <= 0 || NumericCast<idx_t>(batch_size) > STANDARD_VECTOR_SIZE) {
		throw InvalidInputException("boundary probe batch_size must be between 1 and %llu",
		                            NumericCast<unsigned long long>(STANDARD_VECTOR_SIZE));
	}
	if (delay_ms < 0 || delay_ms > 1000) {
		throw InvalidInputException("boundary probe delay_ms must be between 0 and 1000");
	}
	if (fail_after < -1 || fail_after > row_count) {
		throw InvalidInputException("boundary probe fail_after must be -1 or between 0 and row_count");
	}

	names.emplace_back("row_id");
	return_types.emplace_back(LogicalType::BIGINT);
	names.emplace_back("batch_id");
	return_types.emplace_back(LogicalType::BIGINT);
	names.emplace_back("payload");
	return_types.emplace_back(LogicalType::VARCHAR);

	return make_uniq<ProbeBindData>(NumericCast<idx_t>(row_count), NumericCast<idx_t>(batch_size),
	                                NumericCast<idx_t>(delay_ms), fail_after);
}

unique_ptr<GlobalTableFunctionState> ProbeInit(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<ProbeGlobalState>();
}

struct ActiveWaitGuard {
	ActiveWaitGuard() {
		probe_counters.active_waiters.fetch_add(1, std::memory_order_relaxed);
	}

	~ActiveWaitGuard() {
		probe_counters.active_waiters.fetch_sub(1, std::memory_order_relaxed);
	}
};

void WaitInterruptibly(ClientContext &context, idx_t delay_ms) {
	ActiveWaitGuard active_wait_guard;
	for (idx_t waited = 0; waited < delay_ms; waited++) {
		if (context.IsInterrupted()) {
			probe_counters.interruptions.fetch_add(1, std::memory_order_relaxed);
			throw InterruptException();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (context.IsInterrupted()) {
		probe_counters.interruptions.fetch_add(1, std::memory_order_relaxed);
		throw InterruptException();
	}
}

void ProbeFunction(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &bind_data = input.bind_data->Cast<ProbeBindData>();
	auto &state = input.global_state->Cast<ProbeGlobalState>();

	if (bind_data.fail_after >= 0 && state.offset >= NumericCast<idx_t>(bind_data.fail_after)) {
		throw InvalidInputException("boundary probe injected execution failure after %llu rows",
		                            NumericCast<unsigned long long>(state.offset));
	}
	if (state.offset >= bind_data.row_count) {
		return;
	}

	WaitInterruptibly(context, bind_data.delay_ms);

	auto remaining = bind_data.row_count - state.offset;
	auto count = MinValue<idx_t>(bind_data.batch_size, remaining);
	if (bind_data.fail_after >= 0) {
		const auto until_failure = NumericCast<idx_t>(bind_data.fail_after) - state.offset;
		count = MinValue<idx_t>(count, until_failure);
	}
	if (count == 0) {
		throw InvalidInputException("boundary probe injected execution failure after %llu rows",
		                            NumericCast<unsigned long long>(state.offset));
	}

	for (idx_t output_index = 0; output_index < count; output_index++) {
		const auto row_id = state.offset + output_index;
		output.SetValue(0, output_index, Value::BIGINT(NumericCast<int64_t>(row_id)));
		output.SetValue(1, output_index, Value::BIGINT(NumericCast<int64_t>(state.batch_id)));
		output.SetValue(2, output_index, Value("row-" + std::to_string(row_id)));
	}

	state.offset += count;
	state.batch_id++;
	probe_counters.chunks.fetch_add(1, std::memory_order_relaxed);
	output.SetCardinality(count);
}

struct StatsGlobalState : public GlobalTableFunctionState {
	bool finished = false;
};

unique_ptr<FunctionData> StatsBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &return_types,
                                   vector<string> &names) {
	for (const auto &name : {"opened", "closed", "chunks", "interruptions", "active_waiters"}) {
		names.emplace_back(name);
		return_types.emplace_back(LogicalType::UBIGINT);
	}
	return nullptr;
}

unique_ptr<GlobalTableFunctionState> StatsInit(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<StatsGlobalState>();
}

void StatsFunction(ClientContext &, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<StatsGlobalState>();
	if (state.finished) {
		return;
	}

	output.SetValue(0, 0, Value::UBIGINT(probe_counters.opened.load(std::memory_order_relaxed)));
	output.SetValue(1, 0, Value::UBIGINT(probe_counters.closed.load(std::memory_order_relaxed)));
	output.SetValue(2, 0, Value::UBIGINT(probe_counters.chunks.load(std::memory_order_relaxed)));
	output.SetValue(3, 0, Value::UBIGINT(probe_counters.interruptions.load(std::memory_order_relaxed)));
	output.SetValue(4, 0, Value::UBIGINT(probe_counters.active_waiters.load(std::memory_order_relaxed)));
	output.SetCardinality(1);
	state.finished = true;
}

void LoadInternal(ExtensionLoader &loader) {
	TableFunction probe("fdw_boundary_probe",
	                    {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT},
	                    ProbeFunction, ProbeBind, ProbeInit);
	loader.RegisterFunction(probe);

	TableFunction stats("fdw_boundary_probe_stats", {}, StatsFunction, StatsBind, StatsInit);
	loader.RegisterFunction(stats);
}

} // namespace

void FdwBoundaryProbeExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string FdwBoundaryProbeExtension::Name() {
	return "fdw_boundary_probe";
}

std::string FdwBoundaryProbeExtension::Version() const {
#ifdef EXT_VERSION_FDW_BOUNDARY_PROBE
	return EXT_VERSION_FDW_BOUNDARY_PROBE;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(fdw_boundary_probe, loader) {
	duckdb::LoadInternal(loader);
}
}
