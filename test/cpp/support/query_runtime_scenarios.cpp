#include "support/query_runtime_scenarios.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace duckdb_api_test {
namespace {

duckdb_api::TypedRow Row(int64_t id, const std::string &login, bool site_admin) {
	duckdb_api::TypedRow result;
	result.values.push_back(duckdb_api::TypedValue::BigInt(id));
	result.values.push_back(duckdb_api::TypedValue::Varchar(login));
	result.values.push_back(duckdb_api::TypedValue::Boolean(site_admin));
	return result;
}

void ThrowScenarioError(QueryRuntimeScenario scenario) {
	switch (scenario) {
	case QueryRuntimeScenario::TRANSPORT_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::TRANSPORT, "", "remote transport failed safely");
	case QueryRuntimeScenario::HTTP_STATUS_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::HTTP_STATUS, "",
		                                 "remote service returned a non-success status");
	case QueryRuntimeScenario::DECODE_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::DECODE, "", "response is not valid JSON");
	case QueryRuntimeScenario::SCHEMA_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::SCHEMA, "id", "value cannot be converted to BIGINT");
	case QueryRuntimeScenario::POLICY_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::POLICY, "", "request is not authorized");
	case QueryRuntimeScenario::RESOURCE_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "", "response exceeds its byte budget");
	case QueryRuntimeScenario::AUTHENTICATION_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::AUTHENTICATION, "secret",
		                                 "named credential could not authenticate");
	case QueryRuntimeScenario::AUTHORIZATION_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::AUTHORIZATION, "",
		                                 "authenticated principal is not authorized");
	case QueryRuntimeScenario::INTERNAL_ERROR:
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "", "top-secret-provider-detail");
	case QueryRuntimeScenario::UNKNOWN_ERROR:
		throw std::runtime_error("top-secret-unknown-provider-detail");
	default:
		return;
	}
}

class QueryScenarioStream : public duckdb_api::BatchStream {
public:
	QueryScenarioStream(QueryRuntimeScenario scenario_p, std::shared_ptr<QueryLifecycleProbe> probe_p)
	    : scenario(scenario_p), probe(std::move(probe_p)), offset(0), cancelled(false), closed(false) {
	}

	~QueryScenarioStream() noexcept override {
		Close();
	}

	bool Next(duckdb_api::ExecutionControl &control, duckdb_api::TypedBatch &batch) override {
		probe->next_calls.fetch_add(1, std::memory_order_relaxed);
		batch.Clear();
		if (closed) {
			return false;
		}
		if (scenario == QueryRuntimeScenario::BLOCKING) {
			probe->active_waiters.fetch_add(1, std::memory_order_relaxed);
			probe->condition.notify_all();
			while (!cancelled.load(std::memory_order_relaxed) && !control.IsCancellationRequested()) {
				std::unique_lock<std::mutex> guard(probe->mutex);
				probe->condition.wait_for(guard, std::chrono::milliseconds(2));
			}
			probe->active_waiters.fetch_sub(1, std::memory_order_relaxed);
			throw duckdb_api::ExecutionCancelled();
		}
		ThrowScenarioError(scenario);

		batch.column_kinds = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
		                      duckdb_api::ValueKind::BOOLEAN};
		if (scenario == QueryRuntimeScenario::STREAMING) {
			const auto first_id = static_cast<int64_t>(offset + 1);
			batch.rows.push_back(Row(first_id, "stream", false));
			batch.rows.push_back(Row(first_id + 1, "stream", true));
			offset += 2;
			probe->batches.fetch_add(1, std::memory_order_relaxed);
			probe->rows.fetch_add(batch.rows.size(), std::memory_order_relaxed);
			return true;
		}
		if (scenario == QueryRuntimeScenario::MISALIGNED_BATCH) {
			batch.rows.push_back(Row(1, "duck", false));
			batch.rows[0].values.pop_back();
			return true;
		}
		if (scenario == QueryRuntimeScenario::OVERSIZED_BATCH) {
			batch.rows.push_back(Row(1, "duck", false));
			batch.rows.push_back(Row(2, "other", true));
			batch.rows.push_back(Row(3, "duckdb", true));
			return true;
		}
		if (offset == 0) {
			batch.rows.push_back(Row(1, "duck", false));
			batch.rows.push_back(Row(2, "other", true));
			offset = 2;
		} else if (offset == 2) {
			batch.rows.push_back(Row(3, "duckdb", true));
			offset = 3;
		} else {
			return false;
		}
		probe->batches.fetch_add(1, std::memory_order_relaxed);
		probe->rows.fetch_add(batch.rows.size(), std::memory_order_relaxed);
		return true;
	}

	void Cancel() noexcept override {
		if (!cancelled.exchange(true, std::memory_order_relaxed)) {
			probe->cancellations.fetch_add(1, std::memory_order_relaxed);
		}
		probe->condition.notify_all();
	}

	void Close() noexcept override {
		if (!closed.exchange(true, std::memory_order_relaxed)) {
			probe->streams_closed.fetch_add(1, std::memory_order_relaxed);
		}
		probe->condition.notify_all();
	}

private:
	QueryRuntimeScenario scenario;
	std::shared_ptr<QueryLifecycleProbe> probe;
	std::size_t offset;
	std::atomic<bool> cancelled;
	std::atomic<bool> closed;
};

class QueryScenarioExecutor : public duckdb_api::ScanExecutor {
public:
	QueryScenarioExecutor(QueryRuntimeScenario scenario_p, std::shared_ptr<QueryLifecycleProbe> probe_p)
	    : scenario(scenario_p), probe(std::move(probe_p)) {
	}

	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &control) const override {
		if (control.IsCancellationRequested()) {
			throw duckdb_api::ExecutionCancelled();
		}
		if (scenario == QueryRuntimeScenario::OPEN_EXECUTION_CANCELLED) {
			throw duckdb_api::ExecutionCancelled();
		}
		if (scenario == QueryRuntimeScenario::OPEN_INTERNAL_ERROR) {
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "top-secret-open-field",
			                                 "top-secret-open-structured-detail");
		}
		if (scenario == QueryRuntimeScenario::OPEN_POLICY_ERROR) {
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::POLICY, "authority",
			                                 "request is outside the approved policy");
		}
		if (scenario == QueryRuntimeScenario::OPEN_RESOURCE_ERROR) {
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::RESOURCE, "response_bytes",
			                                 "response exceeds its byte budget");
		}
		if (scenario == QueryRuntimeScenario::OPEN_UNKNOWN_EXCEPTION) {
			throw std::runtime_error("top-secret-open-unknown-detail");
		}
		if (scenario == QueryRuntimeScenario::NULL_STREAM) {
			return std::unique_ptr<duckdb_api::BatchStream>();
		}
		probe->streams_opened.fetch_add(1, std::memory_order_relaxed);
		return std::unique_ptr<duckdb_api::BatchStream>(new QueryScenarioStream(scenario, probe));
	}

private:
	QueryRuntimeScenario scenario;
	std::shared_ptr<QueryLifecycleProbe> probe;
};

} // namespace

QueryLifecycleProbe::QueryLifecycleProbe()
    : streams_opened(0), next_calls(0), batches(0), rows(0), cancellations(0), streams_closed(0), active_waiters(0) {
}

std::shared_ptr<const duckdb_api::ScanExecutor> BuildQueryScenarioExecutor(QueryRuntimeScenario scenario,
                                                                           std::shared_ptr<QueryLifecycleProbe> probe) {
	return std::shared_ptr<const duckdb_api::ScanExecutor>(new QueryScenarioExecutor(scenario, std::move(probe)));
}

} // namespace duckdb_api_test
