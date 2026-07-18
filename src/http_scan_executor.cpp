#include "duckdb_api/internal/http_scan_executor.hpp"

#include "duckdb_api/internal/json_decoder.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {
namespace {

bool HasExpectedColumns(const std::vector<PlannedColumn> &columns) {
	return columns.size() == 3 && columns[0].name == "id" && columns[0].logical_type == "BIGINT" &&
	       !columns[0].nullable && columns[0].extractor == "$.id" && columns[1].name == "login" &&
	       columns[1].logical_type == "VARCHAR" && !columns[1].nullable && columns[1].extractor == "$.login" &&
	       columns[2].name == "site_admin" && columns[2].logical_type == "BOOLEAN" &&
	       !columns[2].nullable && columns[2].extractor == "$.site_admin";
}

bool HasExpectedOperation(const PlannedRestOperation &operation) {
	return operation.operation_name == "github_search_duckdb_login_page" && operation.protocol == PlannedProtocol::REST &&
	       operation.method == PlannedHttpMethod::GET && operation.cardinality == PlannedCardinality::ZERO_TO_MANY &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.base_url == "https://api.github.com" &&
	       operation.path == "/search/users" && operation.query_parameters.size() == 2 &&
	       operation.query_parameters[0].name == "q" &&
	       operation.query_parameters[0].encoded_value == "duckdb+in%3Alogin" &&
	       operation.query_parameters[1].name == "per_page" &&
	       operation.query_parameters[1].encoded_value == "3" && operation.headers.size() == 3 &&
	       operation.headers[0].name == "Accept" &&
	       operation.headers[0].value == "application/vnd.github+json" &&
	       operation.headers[1].name == "User-Agent" && operation.headers[1].value == "duckdb-api/0.3.0" &&
	       operation.headers[2].name == "X-GitHub-Api-Version" &&
	       operation.headers[2].value == "2022-11-28" && operation.records_extractor == "$.items[*]";
}

bool HasExpectedNetwork(const NetworkCapability &network) {
	return network.allowed_schemes.size() == 1 && network.allowed_schemes[0] == "https" &&
	       network.allowed_hosts.size() == 1 && network.allowed_hosts[0] == "api.github.com" &&
	       !network.redirects_enabled && !network.private_addresses_enabled && !network.link_local_addresses_enabled &&
	       !network.loopback_addresses_enabled;
}

void ValidateExecutablePlan(const ScanPlan &plan) {
	// Deliberately do not read SourceSnapshot, predicates, relational ownership,
	// ordering/limit delegation, or ClassificationReason. Relational Semantics
	// owns those facts; runtime validates only capability needed for safe I/O and
	// strict decoding.
	if (plan.ConnectorName() != "github" || plan.ConnectorVersion() != "0.3.0" ||
	    plan.RelationName() != "duckdb_login_search_page" || !HasExpectedOperation(plan.Operation()) ||
	    !HasExpectedColumns(plan.OutputColumns()) || plan.Pagination() != FeatureState::DISABLED ||
	    plan.Providers() != FeatureState::DISABLED || plan.Retry() != FeatureState::DISABLED ||
	    plan.Cache() != FeatureState::DISABLED || plan.Authentication() != FeatureState::DISABLED ||
	    !HasExpectedNetwork(plan.Network()) || !plan.Budgets().IsLiveRestBudget()) {
		throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
	}
}

HttpRequest BuildRequest(const PlannedRestOperation &operation) {
	HttpRequest request;
	request.method = "GET";
	request.scheme = "https";
	request.host = "api.github.com";
	request.port = 443;
	request.target = operation.path + "?" + operation.query_parameters[0].name + "=" +
	                 operation.query_parameters[0].encoded_value + "&" + operation.query_parameters[1].name + "=" +
	                 operation.query_parameters[1].encoded_value;
	for (std::size_t index = 0; index < operation.headers.size(); index++) {
		request.headers.push_back({operation.headers[index].name, operation.headers[index].value});
	}
	return request;
}

JsonDecodePlan BuildDecodePlan(const ScanPlan &plan, std::chrono::steady_clock::time_point deadline) {
	const auto &budgets = plan.Budgets();
	JsonDecodePlan result;
	result.records_field = "items";
	result.columns = {{"id", "id", ValueKind::BIGINT},
	                  {"login", "login", ValueKind::VARCHAR},
	                  {"site_admin", "site_admin", ValueKind::BOOLEAN}};
	result.max_records = budgets.decoded_records;
	result.max_string_bytes = budgets.extracted_string_bytes;
	result.max_json_nesting = budgets.json_nesting;
	result.max_decoded_memory_bytes = budgets.decoded_memory_bytes;
	result.deadline = deadline;
	return result;
}

void CheckCancellation(ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
}

class CombinedExecutionControl final : public ExecutionControl {
public:
	CombinedExecutionControl(ExecutionControl &outer_p, const std::atomic<bool> &cancelled_p)
	    : outer(outer_p), cancelled(cancelled_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire) || outer.IsCancellationRequested();
	}

private:
	ExecutionControl &outer;
	const std::atomic<bool> &cancelled;
};

// One stream owns one immutable plan, one deadline, and at most one request.
// attempted becomes true before transport entry, so no failure path can replay
// an uncommitted or partially sent request on a later pull.
class HttpBatchStream final : public BatchStream {
public:
	HttpBatchStream(const ScanPlan &plan_p, std::shared_ptr<const HttpTransport> transport_p)
	    : plan(plan_p), transport(std::move(transport_p)), cancelled(false), closed(false), attempted(false),
	      failed(false), offset(0) {
	}

	~HttpBatchStream() noexcept override {
		Close();
	}

	bool Next(ExecutionControl &control, TypedBatch &batch) override {
		batch.Clear();
		if (closed || failed) {
			return false;
		}
		CombinedExecutionControl combined(control, cancelled);
		if (combined.IsCancellationRequested()) {
			cancelled.store(true, std::memory_order_release);
			failed = true;
			throw ExecutionCancelled();
		}

		if (!attempted) {
			attempted = true;
			const auto deadline = std::chrono::steady_clock::now() +
			                      std::chrono::milliseconds(plan.Budgets().wall_milliseconds);
			const HttpLimits limits {plan.Budgets().header_bytes, plan.Budgets().response_bytes,
			                         plan.Budgets().decompressed_bytes, deadline};
			try {
				auto response = transport->Get(BuildRequest(plan.Operation()), limits, combined);
				CheckCancellation(combined);
				if (response.header_bytes > limits.max_header_bytes || response.response_bytes > limits.max_response_bytes ||
				    static_cast<uint64_t>(response.body.size()) > limits.max_decompressed_bytes) {
					throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP response exceeded an execution budget");
				}
				if (response.status < 200 || response.status >= 300) {
					throw ExecutionError(ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status");
				}
				decoded = DecodeJsonRows(response.body, BuildDecodePlan(plan, deadline), combined);
				CheckCancellation(combined);
			} catch (const ExecutionCancelled &) {
				failed = true;
				throw;
			} catch (const ExecutionError &) {
				failed = true;
				throw;
			} catch (const std::bad_alloc &) {
				failed = true;
				throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
				                     "execution could not be allocated within its memory budget");
			} catch (...) {
				failed = true;
				throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP request failed");
			}
		}

		if (offset >= decoded.size()) {
			return false;
		}
		TypedBatch produced;
		produced.column_kinds = {ValueKind::BIGINT, ValueKind::VARCHAR, ValueKind::BOOLEAN};
		const auto remaining = decoded.size() - offset;
		const auto count = std::min(remaining, static_cast<std::size_t>(plan.Budgets().batch_rows));
		try {
			produced.rows.reserve(count);
			for (std::size_t index = 0; index < count; index++) {
				CheckCancellation(combined);
				// Reserve completes before ownership moves, so batch delivery does
				// not duplicate decoded strings or exceed the decoded-memory ceiling.
				produced.rows.push_back(std::move(decoded[offset + index]));
			}
		} catch (const ExecutionCancelled &) {
			failed = true;
			throw;
		} catch (const ExecutionError &) {
			failed = true;
			throw;
		} catch (...) {
			failed = true;
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
				                     "output batch could not be allocated within its memory budget");
		}
		if (!produced.IsSchemaAligned()) {
			failed = true;
			throw ExecutionError(ErrorStage::INTERNAL, "", "runtime produced a misaligned typed batch");
		}
		offset += count;
		batch = std::move(produced);
		return true;
	}

	void Cancel() noexcept override {
		cancelled.store(true, std::memory_order_release);
	}

	void Close() noexcept override {
		if (closed) {
			return;
		}
		closed = true;
		cancelled.store(true, std::memory_order_release);
		decoded.clear();
		offset = 0;
	}

private:
	const ScanPlan plan;
	const std::shared_ptr<const HttpTransport> transport;
	std::atomic<bool> cancelled;
	bool closed;
	bool attempted;
	bool failed;
	std::vector<TypedRow> decoded;
	std::size_t offset;
};

class HttpScanExecutor final : public ScanExecutor {
public:
	explicit HttpScanExecutor(std::shared_ptr<const HttpTransport> transport_p) : transport(std::move(transport_p)) {
	}

	std::unique_ptr<BatchStream> Open(const ScanPlan &plan, ExecutionControl &control) const override {
		CheckCancellation(control);
		ValidateExecutablePlan(plan);
		CheckCancellation(control);
		try {
			return std::unique_ptr<BatchStream>(new HttpBatchStream(plan, transport));
		} catch (const ExecutionError &) {
			throw;
		} catch (const std::bad_alloc &) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "scan stream could not be allocated within its memory budget");
		} catch (...) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "scan stream initialization failed");
		}
	}

private:
	const std::shared_ptr<const HttpTransport> transport;
};

} // namespace

std::shared_ptr<const ScanExecutor> BuildHttpScanExecutor(std::unique_ptr<HttpTransport> transport) {
	if (!transport) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP executor requires a transport");
	}
	std::shared_ptr<const HttpTransport> shared(std::move(transport));
	return std::shared_ptr<const ScanExecutor>(new HttpScanExecutor(std::move(shared)));
}

} // namespace internal
} // namespace duckdb_api
