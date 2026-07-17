#include "fixture_scenarios.hpp"

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace duckdb_api_test {

const char *const ACCEPTED_SQL =
    "SELECT id, name, active FROM duckdb_api_scan(connector := 'example', relation := 'items') ORDER BY id";

const char *const SUCCESS_RESPONSE = "{\"items\":[{\"id\":1,\"name\":\"alpha\",\"active\":true},"
                                     "{\"id\":2,\"name\":\"beta\",\"active\":false},"
                                     "{\"id\":3,\"name\":\"gamma\",\"active\":true}]}\n";

HookFailures::HookFailures()
    : factory_open(false), stream_open(false), read(false), batch_block(false), interruption(false), close(false) {
}

namespace {

class ScenarioSource : public duckdb_api::FixtureSource {
public:
	ScenarioSource(FixtureScenario scenario_p, std::string digest_p, std::shared_ptr<LifecycleProbe> probe_p,
	               std::string custom_body_p, HookFailures failures_p)
	    : scenario(scenario_p), digest(std::move(digest_p)), probe(std::move(probe_p)),
	      custom_body(std::move(custom_body_p)), failures(failures_p) {
		probe->sources_opened.fetch_add(1, std::memory_order_relaxed);
	}

	const std::string &ContentDigest() const override {
		return digest;
	}

	void Read(duckdb_api::FixtureReadBuffer &buffer) override {
		probe->sources_read.fetch_add(1, std::memory_order_relaxed);
		if (failures.read) {
			throw std::runtime_error("top-secret-read-hook-failure");
		}
		if (scenario == FixtureScenario::UNKNOWN_FAILURE) {
			throw std::runtime_error("top-secret-unknown-fixture-payload");
		}
		if (scenario == FixtureScenario::BLOCKING) {
			probe->active_waiters.fetch_add(1, std::memory_order_relaxed);
			probe->condition.notify_all();
			try {
				while (true) {
					buffer.Checkpoint();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			} catch (...) {
				probe->active_waiters.fetch_sub(1, std::memory_order_relaxed);
				throw;
			}
		}
		if (!custom_body.empty()) {
			buffer.Append(custom_body);
			return;
		}
		if (scenario == FixtureScenario::MALFORMED) {
			buffer.Append("{\"items\":[{\"id\":1,\"name\":\"top-secret-malformed\"");
			return;
		}
		if (scenario == FixtureScenario::TYPE_MISMATCH) {
			buffer.Append("{\"items\":[{\"id\":\"top-secret-type-value\",\"name\":\"alpha\",\"active\":true}]}");
			return;
		}
		buffer.Append(SUCCESS_RESPONSE);
	}

	void OnStreamOpen() override {
		probe->streams_opened.fetch_add(1, std::memory_order_relaxed);
		if (failures.stream_open) {
			throw std::runtime_error("top-secret-open-hook-failure");
		}
	}

	void OnBatch(uint64_t row_count) override {
		probe->batches.fetch_add(1, std::memory_order_relaxed);
		probe->rows.fetch_add(row_count, std::memory_order_relaxed);
		if (failures.batch_block) {
			probe->active_waiters.fetch_add(1, std::memory_order_relaxed);
			probe->condition.notify_all();
			std::unique_lock<std::mutex> guard(probe->mutex);
			probe->condition.wait(guard, [&]() { return probe->release_batches.load(std::memory_order_relaxed); });
			probe->active_waiters.fetch_sub(1, std::memory_order_relaxed);
		}
	}

	void OnInterruption() override {
		probe->interruptions.fetch_add(1, std::memory_order_relaxed);
		if (failures.interruption) {
			throw std::runtime_error("top-secret-interruption-hook-failure");
		}
	}

	void OnStreamClose() override {
		probe->streams_closed.fetch_add(1, std::memory_order_relaxed);
		if (failures.close) {
			throw std::runtime_error("top-secret-close-hook-failure");
		}
	}

private:
	FixtureScenario scenario;
	std::string digest;
	std::shared_ptr<LifecycleProbe> probe;
	std::string custom_body;
	HookFailures failures;
};

} // namespace

ScenarioFactory::ScenarioFactory(FixtureScenario scenario_p, std::string custom_body_p)
    : scenario(scenario_p), digest("test-only-fixture-digest"), source_digest(digest),
      probe(std::make_shared<LifecycleProbe>()), custom_body(std::move(custom_body_p)) {
}

const std::string &ScenarioFactory::ContentDigest() const {
	probe->factory_digest_reads.fetch_add(1, std::memory_order_relaxed);
	return digest;
}

std::unique_ptr<duckdb_api::FixtureSource> ScenarioFactory::Open() const {
	if (failures.factory_open) {
		throw std::runtime_error("top-secret-factory-open-failure");
	}
	return std::unique_ptr<duckdb_api::FixtureSource>(
	    new ScenarioSource(scenario, source_digest, probe, custom_body, failures));
}

bool NeverCancelled::IsCancellationRequested() const noexcept {
	return false;
}

AtomicExecutionControl::AtomicExecutionControl(bool cancelled_p) : cancelled(cancelled_p) {
}

bool AtomicExecutionControl::IsCancellationRequested() const noexcept {
	return cancelled.load(std::memory_order_relaxed);
}

void AtomicExecutionControl::RequestCancellation() noexcept {
	cancelled.store(true, std::memory_order_relaxed);
}

duckdb_api::ScanPlan BuildPlanFor(const ScenarioFactory &factory) {
	return duckdb_api::BuildConservativeScanPlan(duckdb_api::BuildCompiledConnector(factory.digest),
	                                             duckdb_api::BuildConservativeScanRequest());
}

ScenarioRuntime BuildScenarioRuntime(std::unique_ptr<ScenarioFactory> factory) {
	ScenarioRuntime result;
	result.plan = BuildPlanFor(*factory);
	result.probe = factory->probe;
	result.executor = duckdb_api::BuildFixtureScanExecutor(std::move(factory));
	return result;
}

} // namespace duckdb_api_test
