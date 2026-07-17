#pragma once

#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {

static const duckdb::idx_t MAX_FIXTURE_BYTES = 4096;
static const duckdb::idx_t MAX_DECODED_RECORDS = 32;
static const duckdb::idx_t MAX_NAME_BYTES = 128;
static const duckdb::idx_t MAX_JSON_NESTING = 16;
static const duckdb::idx_t OUTPUT_BATCH_ROWS = 2;
static const duckdb::idx_t MAX_EXECUTION_MILLISECONDS = 5000;

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

struct CompiledConnector {
	std::string connector_name;
	std::string version;
	std::string relation_name;
	std::string operation_name;
	std::string method;
	std::string path;
	std::string extractor;
	std::string fixture_digest;

	std::string Snapshot() const;
};

struct AdapterCapabilities {
	bool projection;
	bool filter;
	bool ordering;
	bool limit;
	bool offset;
	bool progress;
	bool cancellation;
	bool secrets;

	bool IsConservativePreview() const;
};

struct ResourceBudgets {
	duckdb::idx_t fixture_bytes;
	duckdb::idx_t decoded_records;
	duckdb::idx_t name_bytes;
	duckdb::idx_t json_nesting;
	duckdb::idx_t batch_rows;
	duckdb::idx_t wall_milliseconds;
	duckdb::idx_t concurrency;

	bool IsPreviewBudget() const;
};

struct ScanRequest {
	std::string connector_name;
	std::string relation_name;
	std::vector<std::string> explicit_inputs;
	std::vector<std::string> projected_columns;
	std::string predicate;
	std::vector<std::string> orderings;
	bool has_limit;
	bool has_offset;
	AdapterCapabilities capabilities;

	std::string Snapshot() const;
};

struct ScanPlan {
	std::string operation_name;
	std::string executor_name;
	std::string method;
	std::string path;
	std::string extractor;
	std::string fixture_digest;
	std::vector<std::string> output_columns;
	std::string remote_predicate;
	std::string runtime_residual_predicate;
	std::vector<std::string> remote_ordering;
	std::vector<std::string> runtime_ordering;
	bool has_remote_limit;
	bool has_remote_offset;
	bool has_runtime_limit;
	bool has_runtime_offset;
	std::vector<std::string> duckdb_owned_operations;
	bool pagination_enabled;
	bool providers_enabled;
	bool retry_enabled;
	bool cache_enabled;
	bool network_enabled;
	ResourceBudgets budgets;

	std::string Snapshot() const;
};

CompiledConnector BuildCompiledConnector(const std::string &fixture_digest);
ScanRequest BuildConservativeScanRequest();
ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

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
