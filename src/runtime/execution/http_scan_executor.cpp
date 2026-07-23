#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

#include "duckdb_api/internal/runtime/authentication/api_key_authenticator.hpp"
#include "duckdb_api/internal/runtime/authentication/bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_paginated_scan.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_paginated_scan.hpp"
#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_retry_controller.hpp"
#include "duckdb_api/internal/runtime/decoding/decoded_page_buffer.hpp"
#include "duckdb_api/internal/runtime/decoding/json_decoder.hpp"
#include "duckdb_api/internal/runtime/policy/request_validation.hpp"

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

static const uint64_t V1_MAX_DECODED_RECORDS_PER_PAGE = 100;

JsonDecodePlan BuildDecodePlan(const AdmittedRestRequestProfile &profile,
                               std::chrono::steady_clock::time_point deadline) {
	const auto &budgets = profile.Budgets();
	JsonDecodePlan result;
	result.response_source =
	    profile.ResponseSource() == PlannedResponseSource::ROOT_OBJECT  ? JsonResponseSource::ROOT_OBJECT
	    : profile.ResponseSource() == PlannedResponseSource::ROOT_ARRAY ? JsonResponseSource::ROOT_ARRAY
	                                                                    : JsonResponseSource::JSON_PATH_MANY;
	result.records_path = profile.RecordsPath();
	for (const auto &column : profile.Columns()) {
		result.columns.push_back(JsonColumnPlan(column.name, column.source_path, column.type, column.nullable));
	}
	result.max_records = budgets.decoded_records;
	result.max_string_bytes = budgets.extracted_string_bytes;
	result.max_json_nesting = budgets.json_nesting;
	result.max_decoded_memory_bytes = budgets.decoded_memory_bytes;
	result.deadline = deadline;
	return result;
}

ScanResourceProfile BuildSingleResourceProfile(const AdmittedRestRequestProfile &profile,
                                               uint64_t max_wall_milliseconds) {
	const auto &budget = profile.Budgets();
	return {{budget.request_attempts, budget.header_bytes, budget.response_bytes, budget.decompressed_bytes,
	         budget.decoded_records, budget.decoded_memory_bytes, budget.concurrency,
	         budget.serialized_request_body_bytes},
	        {profile.RetryPolicy().max_attempts_per_scan, 1, budget.header_bytes, budget.response_bytes,
	         budget.decompressed_bytes, budget.decoded_records, budget.decoded_memory_bytes,
	         std::min(budget.wall_milliseconds, max_wall_milliseconds), budget.concurrency,
	         budget.serialized_request_body_bytes, profile.RetryPolicy().max_cumulative_waiting_milliseconds_per_scan}};
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

// Single-response REST stream. Pagination lives in the separate Remote Runtime
// service and is selected only by complete plan validation at executor
// dispatch.
class HttpBatchStream final : public BatchStream {
public:
	HttpBatchStream(std::unique_ptr<const AdmittedRestRequestProfile> admitted_profile_p,
	                ScanAuthorization authorization_p, std::shared_ptr<const HttpTransport> transport_p,
	                uint64_t max_wall_milliseconds_p)
	    : admitted_profile(std::move(admitted_profile_p)), transport(std::move(transport_p)),
	      authorization(new ScanAuthorization(std::move(authorization_p))), cancelled(false), closed(false),
	      attempted(false), exhausted(false),
	      accounting(BuildSingleResourceProfile(*admitted_profile, max_wall_milliseconds_p)), decoded_memory_bytes(0),
	      offset(0), rows_emitted(0),
	      retry_seed(static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(this)) ^
	                 static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())),
	      current_step_exposure(ExposureState::UNACCEPTED), terminal_exposure(ExposureState::UNACCEPTED),
	      has_terminal_exposure(false) {
		for (const auto &column : admitted_profile->Columns()) {
			column_types.push_back(column.type);
		}
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
		batch = TypedBatch();
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
		try {
			if (accounting.DeadlineStarted()) {
				CheckExecutionState(combined, accounting.Deadline());
			} else {
				CheckCancellation(combined);
			}
			if (!attempted) {
				attempted = true;
				auto attempted_response = ExecuteHttpStepWithRetry(
				    admitted_profile->RetryPolicy(), accounting, transport, 0, retry_seed,
				    [this]() {
					    auto request = BuildAdmittedRestRequest(*admitted_profile);
					    if (admitted_profile->RequiresBearer()) {
						    request = BearerAuthenticator::AuthorizeRest(*admitted_profile, std::move(request),
						                                                 *authorization);
					    } else if (admitted_profile->RequiresApiKey()) {
						    request = ApiKeyAuthenticator::AuthorizeRest(*admitted_profile, std::move(request),
						                                                 *authorization);
					    }
					    return request;
				    },
				    combined);
				auto response = std::move(attempted_response.response);
				const auto deadline = attempted_response.allowance.deadline;
				CheckExecutionState(combined, deadline);
				const bool authenticated = admitted_profile->RequiresBearer() || admitted_profile->RequiresApiKey();
				if (authenticated && response.status == 401) {
					throw ExecutionError(
					    ErrorStage::AUTHENTICATION, "http_status", "HTTP endpoint rejected authentication",
					    HttpStatusFailureProperties(response.status, true, response.metadata.retry_after_present));
				}
				if (authenticated && response.status == 403) {
					throw ExecutionError(
					    ErrorStage::AUTHORIZATION, "http_status", "HTTP endpoint denied authorization",
					    HttpStatusFailureProperties(response.status, true, response.metadata.retry_after_present));
				}
				if (response.status < 200 || response.status >= 300) {
					throw ExecutionError(
					    ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status",
					    HttpStatusFailureProperties(response.status, false, response.metadata.retry_after_present));
				}
				auto page = DecodeJsonPage(response.body, BuildDecodePlan(*admitted_profile, deadline), combined);
				decoded_memory_bytes = page.retained_memory_bytes;
				accounting.CommitDecodedPage({static_cast<uint64_t>(page.rows.size()), page.retained_memory_bytes});
				decoded = std::move(page.rows);
				current_step_exposure = ExposureState::ACCEPTED_UNEXPOSED;
				CheckExecutionState(combined, deadline);
			}

			CheckExecutionState(combined, accounting.Deadline());
			if (offset >= decoded.size()) {
				accounting.CompletePage(false, std::chrono::steady_clock::now());
				exhausted = true;
				authorization.reset();
				return false;
			}
			const auto remaining = decoded.size() - offset;
			const auto count = std::min(remaining, static_cast<std::size_t>(admitted_profile->Budgets().batch_rows));
			RequireTypedBatchHandoffMemory(decoded_memory_bytes, admitted_profile->Budgets().decoded_memory_bytes,
			                               count, column_types.size());
			TypedBatch produced;
			produced.column_types = column_types;
			produced.rows.reserve(count);
			RequireTypedBatchHandoffMemory(decoded_memory_bytes, admitted_profile->Budgets().decoded_memory_bytes,
			                               produced.rows.capacity(), produced.column_types.capacity());
			for (std::size_t index = 0; index < count; index++) {
				CheckExecutionState(combined, accounting.Deadline());
				produced.rows.push_back(std::move(decoded[offset + index]));
			}
			CheckExecutionState(combined, accounting.Deadline());
			if (produced.rows.empty() || !produced.IsSchemaAligned(combined)) {
				throw ExecutionError(ErrorStage::INTERNAL, "", "runtime produced a misaligned or empty typed batch");
			}
			CheckExecutionState(combined, accounting.Deadline());
			offset += count;
			rows_emitted += static_cast<uint64_t>(count);
			batch = std::move(produced);
			current_step_exposure = ExposureState::EXPOSED;
			return true;
		} catch (const ExecutionCancelled &) {
			RememberCurrentFailure();
			throw;
		} catch (const ExecutionError &error) {
			FailWithExecutionError(
			    error.Stage(), error.Field(), error.SafeMessage(),
			    EnrichRetryFailureProperties(FailurePropertiesFromError(error), accounting.Counters().pages,
			                                 accounting.CurrentAttempt(), rows_emitted,
			                                 accounting.Counters().cumulative_waiting_milliseconds, CurrentExposure()));
		} catch (const ScanResourceError &error) {
			FailWithExecutionError(
			    ErrorStage::RESOURCE, error.Field(), error.SafeMessage(),
			    EnrichRetryFailureProperties(ResourceBudgetFailureProperties(error.Field()),
			                                 accounting.Counters().pages, accounting.CurrentAttempt(), rows_emitted,
			                                 accounting.Counters().cumulative_waiting_milliseconds, CurrentExposure()));
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

	ExecutionSnapshot Diagnostics() const noexcept override {
		try {
			std::lock_guard<std::mutex> state_guard(state_mutex);
			const auto &policy = admitted_profile->RetryPolicy();
			const auto &counters = accounting.Counters();
			return {policy.max_attempts_per_step,
			        policy.max_attempts_per_scan,
			        policy.max_delay_milliseconds,
			        policy.max_cumulative_waiting_milliseconds_per_scan,
			        counters.request_attempts,
			        counters.cumulative_waiting_milliseconds,
			        counters.pages,
			        CurrentExposure()};
		} catch (...) {
			return BatchStream::Diagnostics();
		}
	}

private:
	ExposureState CurrentExposure() const noexcept {
		if (has_terminal_exposure) {
			return terminal_exposure;
		}
		return current_step_exposure;
	}
	void ReleasePrivateState() noexcept {
		if (accounting.State() != ScanResourceState::EXHAUSTED) {
			accounting.AbortPage();
		}
		authorization.reset();
		std::vector<TypedRow>().swap(decoded);
		decoded_memory_bytes = 0;
		offset = 0;
	}

	void Fail() noexcept {
		cancelled.store(true, std::memory_order_release);
		ReleasePrivateState();
	}

	void RememberCurrentFailure() noexcept {
		terminal_exposure = CurrentExposure();
		has_terminal_exposure = true;
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

	[[noreturn]] void FailWithExecutionError(ErrorStage stage, std::string field, std::string safe_message,
	                                         FailureProperties properties) {
		try {
			throw ExecutionError(stage, std::move(field), std::move(safe_message), std::move(properties));
		} catch (...) {
			RememberCurrentFailure();
			throw;
		}
	}

	const std::unique_ptr<const AdmittedRestRequestProfile> admitted_profile;
	const std::shared_ptr<const HttpTransport> transport;
	std::unique_ptr<ScanAuthorization> authorization;
	std::vector<OutputValueType> column_types;
	mutable std::mutex state_mutex;
	std::atomic<bool> cancelled;
	bool closed;
	bool attempted;
	bool exhausted;
	std::exception_ptr terminal_exception;
	ScanResourceAccounting accounting;
	std::vector<TypedRow> decoded;
	uint64_t decoded_memory_bytes;
	std::size_t offset;
	// RFC 0021: cumulative rows emitted to DuckDB, for rows_exposed.
	uint64_t rows_emitted;
	const uint64_t retry_seed;
	ExposureState current_step_exposure;
	ExposureState terminal_exposure;
	bool has_terminal_exposure;
};

class HttpScanExecutor final : public ScanExecutor {
public:
	HttpScanExecutor(std::shared_ptr<const HttpTransport> transport_p, HttpExecutionProfile profile_p)
	    : transport(std::move(transport_p)), profile(std::move(profile_p)) {
	}

	std::unique_ptr<BatchStream> Open(const ScanPlan &plan, ExecutionControl &control) const override {
		CheckCancellation(control);
		try {
			if (plan.Operation().Protocol() == PlannedProtocol::GRAPHQL) {
				auto graphql_profile = TryAdmitGraphqlPlan(plan, profile);
				if (!graphql_profile) {
					throw ExecutionError(ErrorStage::POLICY, "",
					                     "scan plan is outside the installed execution profile");
				}
				if (graphql_profile->RequiresBearer()) {
					throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
					                     "authenticated execution requires a bearer authorization capability");
				}
				return OpenGraphqlPaginatedScan(std::move(graphql_profile), ScanAuthorization::Anonymous(), transport,
				                                profile.max_wall_milliseconds, control);
			}
		} catch (const ExecutionCancelled &) {
			throw;
		} catch (const ExecutionError &) {
			throw;
		} catch (...) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		auto paginated = TryAdmitPaginatedRestPlan(plan, profile);
		if (paginated) {
			if (paginated->RequiresBearer() || paginated->RequiresApiKey()) {
				throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
				                     "authenticated execution requires an authorization capability");
			}
			return OpenPaginatedRestScan(std::move(paginated), ScanAuthorization::Anonymous(), transport,
			                             profile.max_wall_milliseconds, control);
		}
		auto admitted = TryAdmitSingleResponseHttpPlan(plan, profile);
		if (!admitted) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		if (admitted->RequiresBearer() || admitted->RequiresApiKey()) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authenticated execution requires an authorization capability");
		}
		return OpenSingle(std::move(admitted), ScanAuthorization::Anonymous(), control);
	}

protected:
	std::unique_ptr<BatchStream> OpenAuthorizationEnvelope(const ScanPlan &plan, ScanAuthorization authorization,
	                                                       ExecutionControl &control) const override {
		try {
			if (plan.Operation().Protocol() == PlannedProtocol::GRAPHQL) {
				auto graphql_profile = TryAdmitGraphqlPlan(plan, profile);
				if (!graphql_profile) {
					throw ExecutionError(ErrorStage::POLICY, "",
					                     "scan plan is outside the installed execution profile");
				}
				if (!MatchesRequiredCredential(AlternativeOf(authorization), graphql_profile->RequiresBearer(),
				                               false)) {
					throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
					                     "authorization capability does not match the scan plan");
				}
				return OpenGraphqlPaginatedScan(std::move(graphql_profile), std::move(authorization), transport,
				                                profile.max_wall_milliseconds, control);
			}
		} catch (const ExecutionCancelled &) {
			throw;
		} catch (const ExecutionError &) {
			throw;
		} catch (...) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		auto paginated_profile = TryAdmitPaginatedRestPlan(plan, profile);
		if (paginated_profile) {
			if (!MatchesRequiredCredential(AlternativeOf(authorization), paginated_profile->RequiresBearer(),
			                               paginated_profile->RequiresApiKey())) {
				throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
				                     "authorization capability does not match the scan plan");
			}
			return OpenPaginatedRestScan(std::move(paginated_profile), std::move(authorization), transport,
			                             profile.max_wall_milliseconds, control);
		}
		auto admitted = TryAdmitSingleResponseHttpPlan(plan, profile);
		if (!admitted) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		if (!MatchesRequiredCredential(AlternativeOf(authorization), admitted->RequiresBearer(),
		                               admitted->RequiresApiKey())) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authorization capability does not match the scan plan");
		}
		return OpenSingle(std::move(admitted), std::move(authorization), control);
	}

	std::unique_ptr<BatchStream> OpenCredentialProviderEnvelope(const ScanPlan &plan,
	                                                            const CredentialProvider &provider,
	                                                            ExecutionControl &control) const override {
		try {
			if (plan.Operation().Protocol() == PlannedProtocol::GRAPHQL) {
				auto graphql_profile = TryAdmitGraphqlPlan(plan, profile);
				if (!graphql_profile) {
					throw ExecutionError(ErrorStage::POLICY, "",
					                     "scan plan is outside the installed execution profile");
				}
				if (!graphql_profile->RequiresBearer() || !plan.SecretReference().IsPresent()) {
					throw ExecutionError(ErrorStage::AUTHENTICATION, "credential_provider",
					                     "scan plan does not admit credential-provider resolution");
				}
				auto authorization = ResolveCredential(plan, provider, control);
				return OpenGraphqlPaginatedScan(std::move(graphql_profile), std::move(authorization), transport,
				                                profile.max_wall_milliseconds, control);
			}
		} catch (const ExecutionCancelled &) {
			throw;
		} catch (const ExecutionError &) {
			throw;
		} catch (...) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		auto paginated_profile = TryAdmitPaginatedRestPlan(plan, profile);
		if (paginated_profile) {
			if ((!paginated_profile->RequiresBearer() && !paginated_profile->RequiresApiKey()) ||
			    !plan.SecretReference().IsPresent()) {
				throw ExecutionError(ErrorStage::AUTHENTICATION, "credential_provider",
				                     "scan plan does not admit credential-provider resolution");
			}
			auto authorization = ResolveCredential(plan, provider, control);
			return OpenPaginatedRestScan(std::move(paginated_profile), std::move(authorization), transport,
			                             profile.max_wall_milliseconds, control);
		}
		auto admitted = TryAdmitSingleResponseHttpPlan(plan, profile);
		if (!admitted) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		if ((!admitted->RequiresBearer() && !admitted->RequiresApiKey()) || !plan.SecretReference().IsPresent()) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "credential_provider",
			                     "scan plan does not admit credential-provider resolution");
		}
		auto authorization = ResolveCredential(plan, provider, control);
		return OpenSingle(std::move(admitted), std::move(authorization), control);
	}

private:
	ScanAuthorization ResolveCredential(const ScanPlan &plan, const CredentialProvider &provider,
	                                    ExecutionControl &control) const {
		return ResolveCredentialAfterAdmission(plan, provider, control);
	}

	std::unique_ptr<BatchStream> OpenSingle(std::unique_ptr<const AdmittedRestRequestProfile> admitted,
	                                        ScanAuthorization authorization, ExecutionControl &control) const {
		CheckCancellation(control);
		try {
			return std::unique_ptr<BatchStream>(new HttpBatchStream(std::move(admitted), std::move(authorization),
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
	                                           "",
	                                           0,
	                                           false,
	                                           false,
	                                           false,
	                                           PAGINATION_MAX_EXECUTION_MILLISECONDS,
	                                           V1_MAX_DECODED_RECORDS_PER_PAGE,
	                                           RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP,
	                                           RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN,
	                                           RETRY_MAX_DELAY_MILLISECONDS,
	                                           RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN};
	return BuildHttpScanExecutorForProfile(std::move(transport), public_profile);
}

std::shared_ptr<const ScanExecutor> BuildHttpScanExecutorForProfile(std::unique_ptr<HttpTransport> transport,
                                                                    const HttpExecutionProfile &profile) {
	if (!transport) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP executor requires a transport");
	}
	const bool has_exact_destination = !profile.host.empty() && profile.port != 0;
	const bool has_partial_destination = profile.host.empty() != (profile.port == 0);
	if (profile.scheme != PlannedUrlScheme::HTTPS || has_partial_destination ||
	    (has_exact_destination && !IsSafeDnsHost(profile.host)) || profile.private_addresses_enabled ||
	    profile.link_local_addresses_enabled || profile.loopback_addresses_enabled) {
		throw ExecutionError(ErrorStage::POLICY, "", "HTTP executor profile is invalid");
	}
	if (profile.max_wall_milliseconds == 0 || profile.max_wall_milliseconds > PAGINATION_MAX_EXECUTION_MILLISECONDS ||
	    profile.max_decoded_records == 0 || profile.max_decoded_records > V1_MAX_DECODED_RECORDS_PER_PAGE ||
	    profile.max_retry_attempts_per_step == 0 ||
	    profile.max_retry_attempts_per_step > RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP ||
	    profile.max_retry_attempts_per_scan == 0 ||
	    profile.max_retry_attempts_per_scan > RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN ||
	    profile.max_retry_delay_milliseconds > RETRY_MAX_DELAY_MILLISECONDS ||
	    profile.max_retry_waiting_milliseconds_per_scan > RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN) {
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
