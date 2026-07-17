#pragma once

#include "duckdb_api/internal/fixture_runtime.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace duckdb_api_test {

extern const char *const ACCEPTED_SQL;
extern const char *const SUCCESS_RESPONSE;

enum class FixtureScenario : uint8_t { SUCCESS, MALFORMED, TYPE_MISMATCH, BLOCKING, UNKNOWN_FAILURE };

struct HookFailures {
	HookFailures();

	bool factory_open;
	bool stream_open;
	bool read;
	bool interruption;
	bool close;
};

struct LifecycleProbe {
	std::atomic<uint64_t> sources_opened {0};
	std::atomic<uint64_t> sources_read {0};
	std::atomic<uint64_t> streams_opened {0};
	std::atomic<uint64_t> streams_closed {0};
	std::atomic<uint64_t> batches {0};
	std::atomic<uint64_t> rows {0};
	std::atomic<uint64_t> interruptions {0};
	std::atomic<uint64_t> active_waiters {0};
	std::atomic<uint64_t> factory_digest_reads {0};
	std::mutex mutex;
	std::condition_variable condition;
};

class ScenarioFactory : public duckdb_api::FixtureFactory {
public:
	explicit ScenarioFactory(FixtureScenario scenario, std::string custom_body = "");

	const std::string &ContentDigest() const override;
	std::unique_ptr<duckdb_api::FixtureSource> Open() const override;

	FixtureScenario scenario;
	std::string digest;
	std::shared_ptr<LifecycleProbe> probe;
	std::string custom_body;
	HookFailures failures;
};

class NeverCancelled : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override;
};

class AtomicExecutionControl : public duckdb_api::ExecutionControl {
public:
	explicit AtomicExecutionControl(bool cancelled = false);

	bool IsCancellationRequested() const noexcept override;
	void RequestCancellation() noexcept;

private:
	std::atomic<bool> cancelled;
};

duckdb_api::ScanPlan BuildPlanFor(const ScenarioFactory &factory);

} // namespace duckdb_api_test
