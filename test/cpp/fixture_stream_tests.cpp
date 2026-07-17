#include "duckdb_api/internal/fixture_runtime.hpp"
#include "support/fixture_scenarios.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using duckdb_api_test::AtomicExecutionControl;
using duckdb_api_test::BuildPlanFor;
using duckdb_api_test::FixtureScenario;
using duckdb_api_test::NeverCancelled;
using duckdb_api_test::Require;
using duckdb_api_test::ScenarioFactory;

class CountdownExecutionControl : public duckdb_api::ExecutionControl {
public:
	explicit CountdownExecutionControl(uint64_t allowed_checks_p) : allowed_checks(allowed_checks_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		const auto remaining = allowed_checks.load(std::memory_order_relaxed);
		if (remaining == 0) {
			return true;
		}
		allowed_checks.fetch_sub(1, std::memory_order_relaxed);
		return false;
	}

private:
	mutable std::atomic<uint64_t> allowed_checks;
};

void RequirePlanRejected(duckdb_api::ScanPlan plan, const std::string &field) {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	factory->digest = plan.fixture_digest;
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled control;
	bool rejected = false;
	try {
		auto stream = executor->Open(plan, control);
		stream->Close();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = error.Stage() == duckdb_api::ErrorStage::POLICY &&
		           error.SafeMessage() == "scan plan is not authorized for fixture execution";
	}
	Require(rejected, "fixture executor accepted a mutated " + field);
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "fixture executor opened a source before rejecting " + field);
}

void TestCapabilityEnvelopeValidation() {
	auto source_factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	auto plan = BuildPlanFor(*source_factory);
	plan.executor_name = "other";
	RequirePlanRejected(plan, "executor identity");
	plan = BuildPlanFor(*source_factory);
	plan.method = "POST";
	RequirePlanRejected(plan, "method");
	plan = BuildPlanFor(*source_factory);
	plan.path = "/other";
	RequirePlanRejected(plan, "path");
	plan = BuildPlanFor(*source_factory);
	plan.extractor = "$.other[*]";
	RequirePlanRejected(plan, "extractor");
	plan = BuildPlanFor(*source_factory);
	plan.output_columns.pop_back();
	RequirePlanRejected(plan, "projection");
	plan = BuildPlanFor(*source_factory);
	plan.remote_predicate = "id > 1";
	RequirePlanRejected(plan, "remote predicate work");
	plan = BuildPlanFor(*source_factory);
	plan.runtime_residual_predicate = "id > 1";
	RequirePlanRejected(plan, "runtime residual work");
	plan = BuildPlanFor(*source_factory);
	plan.remote_ordering.push_back("id");
	RequirePlanRejected(plan, "remote ordering work");
	plan = BuildPlanFor(*source_factory);
	plan.runtime_ordering.push_back("id");
	RequirePlanRejected(plan, "runtime ordering work");
	plan = BuildPlanFor(*source_factory);
	plan.has_remote_limit = true;
	RequirePlanRejected(plan, "remote limit work");
	plan = BuildPlanFor(*source_factory);
	plan.has_remote_offset = true;
	RequirePlanRejected(plan, "remote offset work");
	plan = BuildPlanFor(*source_factory);
	plan.has_runtime_limit = true;
	RequirePlanRejected(plan, "runtime limit work");
	plan = BuildPlanFor(*source_factory);
	plan.has_runtime_offset = true;
	RequirePlanRejected(plan, "runtime offset work");
	plan = BuildPlanFor(*source_factory);
	plan.pagination_enabled = true;
	RequirePlanRejected(plan, "pagination capability");
	plan = BuildPlanFor(*source_factory);
	plan.providers_enabled = true;
	RequirePlanRejected(plan, "provider capability");
	plan = BuildPlanFor(*source_factory);
	plan.retry_enabled = true;
	RequirePlanRejected(plan, "retry capability");
	plan = BuildPlanFor(*source_factory);
	plan.cache_enabled = true;
	RequirePlanRejected(plan, "cache capability");
	plan = BuildPlanFor(*source_factory);
	plan.network_enabled = true;
	RequirePlanRejected(plan, "network capability");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.fixture_bytes++;
	RequirePlanRejected(plan, "fixture-byte budget");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.decoded_records++;
	RequirePlanRejected(plan, "record budget");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.name_bytes++;
	RequirePlanRejected(plan, "name budget");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.json_nesting++;
	RequirePlanRejected(plan, "nesting budget");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.batch_rows++;
	RequirePlanRejected(plan, "batch budget");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.wall_milliseconds++;
	RequirePlanRejected(plan, "wall-time budget");
	plan = BuildPlanFor(*source_factory);
	plan.budgets.concurrency++;
	RequirePlanRejected(plan, "concurrency budget");
}

void TestPlannerAnnotationsDoNotAuthorizeExecution() {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	auto plan = BuildPlanFor(*factory);
	plan.duckdb_owned_operations = {"planner-only-diagnostic-drift"};
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled control;
	auto stream = executor->Open(plan, control);
	stream->Close();
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 1,
	        "runtime reconstructed planner-owned relational classifications");
}

void TestPreOpenRejectionHasNoSourceSideEffects() {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	AtomicExecutionControl cancelled(true);
	bool cancellation_reported = false;
	try {
		executor->Open(BuildPlanFor(*factory), cancelled);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancellation_reported = true;
	}
	Require(cancellation_reported, "pre-open cancellation did not use the runtime marker");
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "pre-open cancellation acquired a fixture source");
	Require(factory->probe->factory_digest_reads.load(std::memory_order_relaxed) == 0,
	        "pre-open cancellation consulted provider identity");

	NeverCancelled control;
	auto plan = BuildPlanFor(*factory);
	plan.fixture_digest = "other-digest";
	bool identity_rejected = false;
	try {
		executor->Open(plan, control);
	} catch (const duckdb_api::ExecutionError &error) {
		identity_rejected = error.Stage() == duckdb_api::ErrorStage::POLICY;
	}
	Require(identity_rejected, "executor accepted fixture identity drift");
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "identity rejection acquired a fixture source");
}

void TestBoundedBatchesAndIdempotentClose() {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled control;
	auto stream = executor->Open(BuildPlanFor(*factory), control);
	std::vector<duckdb_api::ItemRow> batch;
	Require(stream->Next(control, batch) && batch.size() == 2, "first runtime batch was not bounded");
	Require(stream->Next(control, batch) && batch.size() == 1, "second runtime batch was not bounded");
	Require(!stream->Next(control, batch) && batch.empty(), "runtime did not end with an empty batch");
	stream->Close();
	stream->Close();
	Require(factory->probe->batches.load(std::memory_order_relaxed) == 2 &&
	            factory->probe->rows.load(std::memory_order_relaxed) == 3,
	        "runtime batch accounting drifted");
	Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "idempotent close notified the provider more than once");
}

void TestSynchronizedCancellation() {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::BLOCKING);
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled open_control;
	auto stream = executor->Open(BuildPlanFor(*factory), open_control);
	AtomicExecutionControl control;
	std::atomic<bool> cancelled {false};
	std::thread worker([&]() {
		std::vector<duckdb_api::ItemRow> batch;
		try {
			stream->Next(control, batch);
		} catch (const duckdb_api::ExecutionCancelled &) {
			cancelled.store(true, std::memory_order_relaxed);
		}
	});
	{
		std::unique_lock<std::mutex> guard(factory->probe->mutex);
		const auto ready = factory->probe->condition.wait_for(guard, std::chrono::seconds(5), [&]() {
			return factory->probe->active_waiters.load(std::memory_order_relaxed) == 1;
		});
		if (!ready) {
			control.RequestCancellation();
			worker.join();
			throw std::runtime_error("runtime fixture did not reach its cancellation checkpoint");
		}
	}
	control.RequestCancellation();
	worker.join();
	Require(cancelled.load(std::memory_order_relaxed), "runtime did not propagate ExecutionCancelled");
	Require(factory->probe->active_waiters.load(std::memory_order_relaxed) == 0,
	        "runtime cancellation left an active fixture waiter");
	Require(factory->probe->interruptions.load(std::memory_order_relaxed) == 1,
	        "runtime did not report interruption exactly once");
	std::vector<duckdb_api::ItemRow> batch;
	try {
		stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionCancelled &) {
	}
	Require(factory->probe->interruptions.load(std::memory_order_relaxed) == 1,
	        "repeated cancellation repeated the provider interruption hook");
	stream->Close();
}

void TestCancellationDuringDecoding() {
	auto factory = std::make_shared<ScenarioFactory>(
	    FixtureScenario::SUCCESS,
	    "{\"metadata\":{\"nested\":[1,2,3]},\"items\":[{\"id\":1,\"name\":\"alpha\",\"active\":true},"
	    "{\"id\":2,\"name\":\"beta\",\"active\":false}]}");
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	CountdownExecutionControl control(20);
	auto stream = executor->Open(BuildPlanFor(*factory), control);
	bool cancelled = false;
	try {
		std::vector<duckdb_api::ItemRow> batch;
		stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancelled = true;
	}
	stream->Close();
	Require(cancelled, "decoder checkpoints did not propagate runtime cancellation");
	Require(factory->probe->sources_read.load(std::memory_order_relaxed) == 1 &&
	            factory->probe->batches.load(std::memory_order_relaxed) == 0,
	        "decode cancellation occurred before reading or after emitting a batch");
	Require(factory->probe->interruptions.load(std::memory_order_relaxed) == 1,
	        "decode cancellation did not report interruption exactly once");
}

void TestWallTimeBudget() {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::BLOCKING);
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled control;
	auto stream = executor->Open(BuildPlanFor(*factory), control);
	const auto started = std::chrono::steady_clock::now();
	bool timed_out = false;
	try {
		std::vector<duckdb_api::ItemRow> batch;
		stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		timed_out = error.Stage() == duckdb_api::ErrorStage::POLICY &&
		            error.SafeMessage() == "execution exceeds the wall-time budget";
	}
	const auto elapsed =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	Require(timed_out, "blocking runtime did not report its wall-time budget");
	Require(elapsed >= 4500 && elapsed < 8000, "wall-time budget did not terminate the provider promptly");
	stream->Close();
}

void TestConcurrentStreamsOwnIndependentState() {
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	NeverCancelled first_control;
	NeverCancelled second_control;
	auto first = executor->Open(BuildPlanFor(*factory), first_control);
	auto second = executor->Open(BuildPlanFor(*factory), second_control);
	std::string first_error;
	std::string second_error;
	auto consume = [](duckdb_api::BatchStream &stream, NeverCancelled &control, std::string &error) {
		std::vector<duckdb_api::ItemRow> batch;
		uint64_t rows = 0;
		while (stream.Next(control, batch)) {
			rows += batch.size();
		}
		if (rows != 3) {
			error = "independent stream returned the wrong row count";
		}
	};
	std::thread first_worker([&]() { consume(*first, first_control, first_error); });
	std::thread second_worker([&]() { consume(*second, second_control, second_error); });
	first_worker.join();
	second_worker.join();
	first->Close();
	second->Close();
	Require(first_error.empty() && second_error.empty(), "concurrent runtime stream state was shared");
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 2 &&
	            factory->probe->streams_closed.load(std::memory_order_relaxed) == 2,
	        "concurrent runtime streams did not own distinct source lifecycles");
}

void TestHookFailuresPreservePrimaryOutcomes() {
	NeverCancelled control;
	auto factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	factory->failures.factory_open = true;
	auto executor = duckdb_api::BuildFixtureScanExecutor(factory);
	bool factory_failure = false;
	try {
		executor->Open(BuildPlanFor(*factory), control);
	} catch (const std::runtime_error &error) {
		factory_failure = std::string(error.what()) == "top-secret-factory-open-failure";
	}
	Require(factory_failure && factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "factory-open failure was replaced or acquired a source");

	factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	factory->failures.stream_open = true;
	factory->failures.close = true;
	executor = duckdb_api::BuildFixtureScanExecutor(factory);
	bool open_failure = false;
	try {
		executor->Open(BuildPlanFor(*factory), control);
	} catch (const std::runtime_error &error) {
		open_failure = std::string(error.what()) == "top-secret-open-hook-failure";
	}
	Require(open_failure && factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "open-hook cleanup masked the primary failure or did not run");

	factory = std::make_shared<ScenarioFactory>(FixtureScenario::SUCCESS);
	factory->failures.read = true;
	factory->failures.close = true;
	executor = duckdb_api::BuildFixtureScanExecutor(factory);
	auto stream = executor->Open(BuildPlanFor(*factory), control);
	bool read_failure = false;
	try {
		std::vector<duckdb_api::ItemRow> batch;
		stream->Next(control, batch);
	} catch (const std::runtime_error &error) {
		read_failure = std::string(error.what()) == "top-secret-read-hook-failure";
	}
	stream->Close();
	Require(read_failure && factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "close-hook cleanup masked a read failure or escaped Close");

	factory = std::make_shared<ScenarioFactory>(FixtureScenario::MALFORMED);
	factory->failures.close = true;
	executor = duckdb_api::BuildFixtureScanExecutor(factory);
	stream = executor->Open(BuildPlanFor(*factory), control);
	bool execution_error_preserved = false;
	try {
		std::vector<duckdb_api::ItemRow> batch;
		stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		execution_error_preserved = error.Stage() == duckdb_api::ErrorStage::DECODE;
	}
	stream->Close();
	Require(execution_error_preserved && factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "close-hook cleanup masked a structured execution failure");

	factory = std::make_shared<ScenarioFactory>(FixtureScenario::BLOCKING);
	factory->failures.interruption = true;
	factory->failures.close = true;
	executor = duckdb_api::BuildFixtureScanExecutor(factory);
	stream = executor->Open(BuildPlanFor(*factory), control);
	stream->Cancel();
	bool cancellation_preserved = false;
	try {
		std::vector<duckdb_api::ItemRow> batch;
		stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancellation_preserved = true;
	}
	stream->Close();
	Require(cancellation_preserved && factory->probe->interruptions.load(std::memory_order_relaxed) == 1 &&
	            factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "interruption or close hook replaced runtime cancellation");
}

} // namespace

int main() {
	try {
		TestCapabilityEnvelopeValidation();
		TestPlannerAnnotationsDoNotAuthorizeExecution();
		TestPreOpenRejectionHasNoSourceSideEffects();
		TestBoundedBatchesAndIdempotentClose();
		TestSynchronizedCancellation();
		TestCancellationDuringDecoding();
		TestWallTimeBudget();
		TestConcurrentStreamsOwnIndependentState();
		TestHookFailuresPreservePrimaryOutcomes();
		std::cout << "fixture stream tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "fixture stream tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
