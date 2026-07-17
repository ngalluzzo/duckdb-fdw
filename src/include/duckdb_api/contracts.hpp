#pragma once

#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {

enum class ErrorStage : uint8_t { DECODE, SCHEMA, POLICY, INTERNAL };

class ExecutionError : public std::exception {
public:
	ExecutionError(ErrorStage stage, std::string field, std::string safe_message);

	const char *what() const noexcept override;
	ErrorStage Stage() const;
	const std::string &Field() const;
	const std::string &SafeMessage() const;

private:
	ErrorStage stage;
	std::string field;
	std::string safe_message;
};

struct ItemRow {
	int64_t id;
	std::string name;
	bool active;
};

class FixtureSource;

class FixtureReadBuffer {
public:
	FixtureReadBuffer(duckdb::ClientContext &context, FixtureSource &source, duckdb::idx_t max_bytes,
	                  std::chrono::steady_clock::time_point deadline);

	void Checkpoint();
	void Append(const std::string &chunk);
	const std::string &Contents() const;

private:
	duckdb::ClientContext &context;
	FixtureSource &source;
	duckdb::idx_t max_bytes;
	std::chrono::steady_clock::time_point deadline;
	std::string contents;
};

class FixtureSource {
public:
	virtual ~FixtureSource();
	virtual const std::string &ContentDigest() const = 0;
	virtual void Read(FixtureReadBuffer &buffer) = 0;
	virtual void OnStreamOpen();
	virtual void OnBatch(duckdb::idx_t row_count);
	virtual void OnInterruption();
	virtual void OnStreamClose();
};

class FixtureFactory {
public:
	virtual ~FixtureFactory();
	virtual const std::string &ContentDigest() const = 0;
	virtual std::unique_ptr<FixtureSource> Open() const = 0;
};

class BatchStream {
public:
	virtual ~BatchStream();
	virtual bool Next(duckdb::ClientContext &context, std::vector<ItemRow> &rows) = 0;
	virtual void Cancel() = 0;
	virtual void Close() = 0;
};

std::unique_ptr<BatchStream> OpenBatchStream(const ScanPlan &plan, const FixtureFactory &factory);

} // namespace duckdb_api
