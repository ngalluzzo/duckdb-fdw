#pragma once

#include "duckdb_api/execution.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace duckdb_api {

// Remote Runtime implementation contract; production adapters must consume
// ScanExecutor instead of these concrete provider interfaces.
class FixtureSource;

// Bounded response buffer exposed only to fixture providers and direct runtime
// tests. Every append is an execution checkpoint and enforces the byte ceiling.
class FixtureReadBuffer {
public:
	FixtureReadBuffer(ExecutionControl &control, FixtureSource &source, const std::atomic<bool> &cancelled,
	                  bool &interruption_reported, uint64_t max_bytes, std::chrono::steady_clock::time_point deadline);

	void Checkpoint();
	void Append(const std::string &chunk);
	const std::string &Contents() const;

private:
	ExecutionControl &control;
	FixtureSource &source;
	const std::atomic<bool> &cancelled;
	bool &interruption_reported;
	uint64_t max_bytes;
	std::chrono::steady_clock::time_point deadline;
	std::string contents;
};

// Fixture provider lifecycle. Hooks may instrument execution, but runtime
// cleanup never permits hook failures to escape Cancel, Close, or destruction.
class FixtureSource {
public:
	virtual ~FixtureSource() noexcept;
	virtual const std::string &ContentDigest() const = 0;
	virtual void Read(FixtureReadBuffer &buffer) = 0;
	virtual void OnStreamOpen();
	virtual void OnBatch(uint64_t row_count);
	virtual void OnInterruption();
	virtual void OnStreamClose();
};

class FixtureFactory {
public:
	virtual ~FixtureFactory() noexcept;
	virtual const std::string &ContentDigest() const = 0;
	virtual std::unique_ptr<FixtureSource> Open() const = 0;
};

// Constructs the immutable runtime service used by composition. The returned
// executor owns the provider for its full registration lifetime.
std::shared_ptr<const ScanExecutor> BuildFixtureScanExecutor(std::shared_ptr<FixtureFactory> factory);

} // namespace duckdb_api
