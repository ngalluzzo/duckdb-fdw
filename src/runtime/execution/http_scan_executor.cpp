#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

#include "duckdb_api/internal/runtime/authentication/fixed_github_user_bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/execution/http_paginated_scan.hpp"
#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/decoding/json_decoder.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {
namespace {

static_assert(std::is_nothrow_move_assignable<TypedBatch>::value,
              "batch ownership transfer must not fail after rows are committed");

static const uint64_t NATIVE_PRODUCT_MAX_DECODED_RECORDS_PER_PAGE = 100;

HttpRequest BuildRequest(const PlannedRestOperation &operation) {
	HttpRequest request;
	request.method = "GET";
	request.scheme = operation.origin.scheme == PlannedUrlScheme::HTTPS ? "https" : "http";
	request.host = operation.origin.host;
	request.port = operation.origin.port;
	request.target = operation.path;
	for (std::size_t index = 0; index < operation.query_parameters.size(); index++) {
		request.target += index == 0 ? "?" : "&";
		request.target +=
		    operation.query_parameters[index].name + "=" + operation.query_parameters[index].encoded_value;
	}
	for (const auto &header : operation.headers) {
		request.headers.push_back({header.name, header.value});
	}
	return request;
}

JsonDecodePlan BuildDecodePlan(const ScanPlan &plan, std::chrono::steady_clock::time_point deadline) {
	const auto &budgets = plan.Budgets();
	JsonDecodePlan result;
	result.response_source = plan.Operation().Rest().response_source == PlannedResponseSource::ROOT_OBJECT
	                             ? JsonResponseSource::ROOT_OBJECT
	                             : JsonResponseSource::JSON_PATH_MANY;
	result.records_field = result.response_source == JsonResponseSource::JSON_PATH_MANY ? "items" : "";
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

void CheckExecutionState(ExecutionControl &control, std::chrono::steady_clock::time_point deadline) {
	CheckCancellation(control);
	if (std::chrono::steady_clock::now() >= deadline) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
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

// Single-response compatibility stream for the two 0.4 profiles. Pagination
// lives in the separate Remote Runtime service and is selected only by exact
// plan validation at executor dispatch.
class HttpBatchStream final : public BatchStream {
public:
	HttpBatchStream(const ScanPlan &plan_p, ScanAuthorization authorization_p,
	                AdmittedHttpOperation installed_operation_p, std::shared_ptr<const HttpTransport> transport_p,
	                uint64_t max_wall_milliseconds_p)
	    : plan(plan_p), transport(std::move(transport_p)), max_wall_milliseconds(max_wall_milliseconds_p),
	      authorization(new ScanAuthorization(std::move(authorization_p))), installed_operation(installed_operation_p),
	      cancelled(false), closed(false), attempted(false), exhausted(false), deadline_initialized(false), offset(0) {
	}

	~HttpBatchStream() noexcept override {
		Close();
	}

	bool Next(ExecutionControl &control, TypedBatch &batch) override {
		std::unique_lock<std::mutex> state_guard;
		try {
			state_guard = std::unique_lock<std::mutex>(state_mutex);
		} catch (...) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "scan stream synchronization failed");
		}
		batch.Clear();
		if (closed) {
			return false;
		}
		if (terminal_exception) {
			std::rethrow_exception(terminal_exception);
		}
		if (exhausted) {
			return false;
		}
		if (cancelled.load(std::memory_order_acquire)) {
			throw ExecutionCancelled();
		}
		CombinedExecutionControl combined(control, cancelled);
		if (!deadline_initialized) {
			deadline = std::chrono::steady_clock::now() +
			           std::chrono::milliseconds(std::min(plan.Budgets().wall_milliseconds, max_wall_milliseconds));
			deadline_initialized = true;
		}
		try {
			CheckExecutionState(combined, deadline);
			if (!attempted) {
				attempted = true;
				const HttpLimits limits {0,
				                         plan.Budgets().header_bytes,
				                         plan.Budgets().response_bytes,
				                         plan.Budgets().decompressed_bytes,
				                         0,
				                         deadline};
				auto request = BuildRequest(plan.Operation().Rest());
				if (installed_operation == AdmittedHttpOperation::AUTHENTICATED_USER) {
					request = FixedGithubUserBearerAuthenticator::Authorize(plan, std::move(request), *authorization);
				}
				auto response = transport->Execute(request, limits, combined);
				authorization.reset();
				CheckExecutionState(combined, deadline);
				if (response.header_bytes > limits.max_header_bytes ||
				    response.response_bytes > limits.max_response_bytes ||
				    static_cast<uint64_t>(response.body.size()) > limits.max_decompressed_bytes ||
				    response.metadata.retained_bytes > limits.max_metadata_bytes) {
					throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP response exceeded an execution budget");
				}
				if (installed_operation == AdmittedHttpOperation::AUTHENTICATED_USER && response.status == 401) {
					throw ExecutionError(ErrorStage::AUTHENTICATION, "http_status",
					                     "HTTP endpoint rejected bearer authentication");
				}
				if (installed_operation == AdmittedHttpOperation::AUTHENTICATED_USER && response.status == 403) {
					throw ExecutionError(ErrorStage::AUTHORIZATION, "http_status",
					                     "HTTP endpoint denied bearer authorization");
				}
				if (response.status < 200 || response.status >= 300) {
					throw ExecutionError(ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status");
				}
				decoded = DecodeJsonRows(response.body, BuildDecodePlan(plan, deadline), combined);
				CheckExecutionState(combined, deadline);
			}

			CheckExecutionState(combined, deadline);
			if (offset >= decoded.size()) {
				exhausted = true;
				return false;
			}
			const auto remaining = decoded.size() - offset;
			const auto count = std::min(remaining, static_cast<std::size_t>(plan.Budgets().batch_rows));
			TypedBatch produced;
			produced.column_kinds = {ValueKind::BIGINT, ValueKind::VARCHAR, ValueKind::BOOLEAN};
			produced.rows.reserve(count);
			for (std::size_t index = 0; index < count; index++) {
				CheckExecutionState(combined, deadline);
				produced.rows.push_back(std::move(decoded[offset + index]));
			}
			CheckExecutionState(combined, deadline);
			offset += count;
			if (produced.rows.empty() || !produced.IsSchemaAligned()) {
				throw ExecutionError(ErrorStage::INTERNAL, "", "runtime produced a misaligned or empty typed batch");
			}
			batch = std::move(produced);
			return true;
		} catch (const ExecutionCancelled &) {
			RememberCurrentFailure();
			throw;
		} catch (const ExecutionError &) {
			RememberCurrentFailure();
			throw;
		} catch (const std::bad_alloc &) {
			FailWithExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                       "execution could not be allocated within its memory budget");
		} catch (...) {
			FailWithExecutionError(ErrorStage::TRANSPORT, "", "HTTP request failed");
		}
	}

	void Cancel() noexcept override {
		cancelled.store(true, std::memory_order_release);
		try {
			std::lock_guard<std::mutex> state_guard(state_mutex);
			if (!closed) {
				ReleasePrivateState();
			}
		} catch (...) {
		}
	}

	void Close() noexcept override {
		cancelled.store(true, std::memory_order_release);
		try {
			std::lock_guard<std::mutex> state_guard(state_mutex);
			if (closed) {
				return;
			}
			closed = true;
			ReleasePrivateState();
		} catch (...) {
		}
	}

private:
	void ReleasePrivateState() noexcept {
		authorization.reset();
		std::vector<TypedRow>().swap(decoded);
		offset = 0;
	}

	void Fail() noexcept {
		cancelled.store(true, std::memory_order_release);
		ReleasePrivateState();
	}

	void RememberCurrentFailure() noexcept {
		terminal_exception = std::current_exception();
		Fail();
	}

	[[noreturn]] void FailWithExecutionError(ErrorStage stage, std::string field, std::string safe_message) {
		try {
			throw ExecutionError(stage, std::move(field), std::move(safe_message));
		} catch (...) {
			RememberCurrentFailure();
			throw;
		}
	}

	const ScanPlan plan;
	const std::shared_ptr<const HttpTransport> transport;
	const uint64_t max_wall_milliseconds;
	std::unique_ptr<ScanAuthorization> authorization;
	const AdmittedHttpOperation installed_operation;
	mutable std::mutex state_mutex;
	std::atomic<bool> cancelled;
	bool closed;
	bool attempted;
	bool exhausted;
	std::exception_ptr terminal_exception;
	bool deadline_initialized;
	std::chrono::steady_clock::time_point deadline;
	std::vector<TypedRow> decoded;
	std::size_t offset;
};

class HttpScanExecutor final : public ScanExecutor {
public:
	HttpScanExecutor(std::shared_ptr<const HttpTransport> transport_p, HttpExecutionProfile profile_p)
	    : transport(std::move(transport_p)), profile(std::move(profile_p)) {
	}

	std::unique_ptr<BatchStream> Open(const ScanPlan &plan, ExecutionControl &control) const override {
		CheckCancellation(control);
		if (TryAdmitRepositoryHttpPlan(plan, profile)) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authenticated execution requires a bearer authorization capability");
		}
		AdmittedHttpOperation installed = AdmittedHttpOperation::ANONYMOUS_SEARCH;
		if (!TryAdmitSingleResponseHttpPlan(plan, profile, installed)) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		if (installed != AdmittedHttpOperation::ANONYMOUS_SEARCH) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authenticated execution requires a bearer authorization capability");
		}
		return OpenSingle(plan, ScanAuthorization::Anonymous(), installed, control);
	}

protected:
	std::unique_ptr<BatchStream> OpenAuthorizationEnvelope(const ScanPlan &plan, ScanAuthorization authorization,
	                                                       ExecutionControl &control) const override {
		auto repository_profile = TryAdmitRepositoryHttpPlan(plan, profile);
		if (repository_profile) {
			if (AlternativeOf(authorization) != AuthorizationAlternative::GITHUB_USER_BEARER) {
				throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
				                     "authorization capability does not match the scan plan");
			}
			return OpenAuthenticatedRepositoriesScan(plan, std::move(repository_profile), std::move(authorization),
			                                         transport, profile.max_wall_milliseconds, control);
		}
		AdmittedHttpOperation installed = AdmittedHttpOperation::ANONYMOUS_SEARCH;
		if (!TryAdmitSingleResponseHttpPlan(plan, profile, installed)) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		const auto alternative = AlternativeOf(authorization);
		const bool matches = (installed == AdmittedHttpOperation::ANONYMOUS_SEARCH &&
		                      alternative == AuthorizationAlternative::ANONYMOUS) ||
		                     (installed == AdmittedHttpOperation::AUTHENTICATED_USER &&
		                      alternative == AuthorizationAlternative::GITHUB_USER_BEARER);
		if (!matches) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authorization capability does not match the scan plan");
		}
		return OpenSingle(plan, std::move(authorization), installed, control);
	}

private:
	std::unique_ptr<BatchStream> OpenSingle(const ScanPlan &plan, ScanAuthorization authorization,
	                                        AdmittedHttpOperation installed, ExecutionControl &control) const {
		CheckCancellation(control);
		try {
			return std::unique_ptr<BatchStream>(new HttpBatchStream(plan, std::move(authorization), installed,
			                                                        transport, profile.max_wall_milliseconds));
		} catch (const ExecutionError &) {
			throw;
		} catch (const std::bad_alloc &) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "scan stream could not be allocated within its memory budget");
		} catch (...) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "scan stream initialization failed");
		}
	}

	const std::shared_ptr<const HttpTransport> transport;
	const HttpExecutionProfile profile;
};

} // namespace

std::shared_ptr<const ScanExecutor> BuildHttpScanExecutor(std::unique_ptr<HttpTransport> transport) {
	const HttpExecutionProfile public_profile {PlannedUrlScheme::HTTPS,
	                                           "api.github.com",
	                                           443,
	                                           false,
	                                           false,
	                                           false,
	                                           PAGINATION_MAX_EXECUTION_MILLISECONDS,
	                                           NATIVE_PRODUCT_MAX_DECODED_RECORDS_PER_PAGE};
	return BuildHttpScanExecutorForProfile(std::move(transport), public_profile);
}

std::shared_ptr<const ScanExecutor> BuildHttpScanExecutorForProfile(std::unique_ptr<HttpTransport> transport,
                                                                    const HttpExecutionProfile &profile) {
	if (!transport) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP executor requires a transport");
	}
	if (profile.host.empty() || profile.host.find_first_of("/:@?#\r\n") != std::string::npos || profile.port == 0) {
		throw ExecutionError(ErrorStage::POLICY, "", "HTTP executor profile is invalid");
	}
	if (profile.max_wall_milliseconds == 0 || profile.max_wall_milliseconds > PAGINATION_MAX_EXECUTION_MILLISECONDS ||
	    profile.max_decoded_records == 0 || profile.max_decoded_records > NATIVE_PRODUCT_MAX_DECODED_RECORDS_PER_PAGE) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP executor profile is invalid");
	}
	try {
		std::shared_ptr<const HttpTransport> shared(std::move(transport));
		return std::shared_ptr<const ScanExecutor>(new HttpScanExecutor(std::move(shared), profile));
	} catch (const ExecutionError &) {
		throw;
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
		                     "HTTP executor could not be allocated within its memory budget");
	} catch (...) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP executor initialization failed");
	}
}

} // namespace internal
} // namespace duckdb_api
