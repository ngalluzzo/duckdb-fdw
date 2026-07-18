#pragma once

#include "duckdb_api/execution.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace duckdb_api_test {

static const char ACCEPTED_LIVE_SQL[] = "SELECT id, login, site_admin FROM duckdb_api_scan(connector := 'github', "
                                        "relation := 'duckdb_login_search_page') ORDER BY id";

enum class QueryRuntimeScenario {
	SUCCESS,
	STREAMING,
	BLOCKING,
	TRANSPORT_ERROR,
	HTTP_STATUS_ERROR,
	DECODE_ERROR,
	SCHEMA_ERROR,
	POLICY_ERROR,
	RESOURCE_ERROR,
	INTERNAL_ERROR,
	UNKNOWN_ERROR,
	NULL_STREAM,
	MISALIGNED_BATCH,
	OVERSIZED_BATCH
};

struct QueryLifecycleProbe {
	std::atomic<uint64_t> streams_opened;
	std::atomic<uint64_t> next_calls;
	std::atomic<uint64_t> batches;
	std::atomic<uint64_t> rows;
	std::atomic<uint64_t> cancellations;
	std::atomic<uint64_t> streams_closed;
	std::atomic<uint64_t> active_waiters;
	std::mutex mutex;
	std::condition_variable condition;

	QueryLifecycleProbe();
};

std::shared_ptr<const duckdb_api::ScanExecutor> BuildQueryScenarioExecutor(QueryRuntimeScenario scenario,
                                                                           std::shared_ptr<QueryLifecycleProbe> probe);

} // namespace duckdb_api_test
