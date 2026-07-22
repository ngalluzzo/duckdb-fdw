#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

#include "duckdb_api/internal/runtime/authentication/api_key_authenticator.hpp"
#include "duckdb_api/internal/runtime/authentication/bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_paginated_scan.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_paginated_scan.hpp"
#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
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
		result.columns.push_back(JsonColumnPlan(column.name, column.source_path, column.kind, column.nullable));
	}
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

// Single-response REST stream. Pagination lives in the separate Remote Runtime
// service and is selected only by complete plan validation at executor
// dispatch.
class HttpBatchStream final : public BatchStream {
public:
	HttpBatchStream(std::unique_ptr<const AdmittedRestRequestProfile> admitted_profile_p,
	                ScanAuthorization authorization_p, std::shared_ptr<const HttpTransport> transport_p,
	                uint64_t max_wall_milliseconds_p)
	    : admitted_profile(std::move(admitted_profile_p)), transport(std::move(transport_p)),
	      max_wall_milliseconds(max_wall_milliseconds_p),
	      authorization(new ScanAuthorization(std::move(authorization_p))), cancelled(false), closed(false),
	      attempted(false), exhausted(false), deadline_initialized(false), offset(0) {
		for (const auto &column : admitted_profile->Columns()) {
			column_kinds.push_back(column.kind);
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
			           std::chrono::milliseconds(
			               std::min(admitted_profile->Budgets().wall_milliseconds, max_wall_milliseconds));
			deadline_initialized = true;
		}
		try {
			CheckExecutionState(combined, deadline);
			if (!attempted) {
				attempted = true;
				const HttpLimits limits {0,
				                         admitted_profile->Budgets().header_bytes,
				                         admitted_profile->Budgets().response_bytes,
				                         admitted_profile->Budgets().decompressed_bytes,
				                         0,
				                         deadline};
				auto request = BuildAdmittedRestRequest(*admitted_profile);
				if (admitted_profile->RequiresBearer()) {
					request = BearerAuthenticator::AuthorizeRest(*admitted_profile, std::move(request), *authorization);
				} else if (admitted_profile->RequiresApiKey()) {
					request = ApiKeyAuthenticator::AuthorizeRest(*admitted_profile, std::move(request), *authorization);
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
				const bool authenticated = admitted_profile->RequiresBearer() || admitted_profile->RequiresApiKey();
				if (authenticated && response.status == 401) {
					throw ExecutionError(ErrorStage::AUTHENTICATION, "http_status",
					                     "HTTP endpoint rejected authentication");
				}
				if (authenticated && response.status == 403) {
					throw ExecutionError(ErrorStage::AUTHORIZATION, "http_status",
					                     "HTTP endpoint denied authorization");
				}
				if (response.status < 200 || response.status >= 300) {
					throw ExecutionError(ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status");
				}
				decoded = DecodeJsonRows(response.body, BuildDecodePlan(*admitted_profile, deadline), combined);
				CheckExecutionState(combined, deadline);
			}

			CheckExecutionState(combined, deadline);
			if (offset >= decoded.size()) {
				exhausted = true;
				return false;
			}
			const auto remaining = decoded.size() - offset;
			const auto count = std::min(remaining, static_cast<std::size_t>(admitted_profile->Budgets().batch_rows));
			TypedBatch produced;
			produced.column_kinds = column_kinds;
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

	const std::unique_ptr<const AdmittedRestRequestProfile> admitted_profile;
	const std::shared_ptr<const HttpTransport> transport;
	const uint64_t max_wall_milliseconds;
	std::unique_ptr<ScanAuthorization> authorization;
	std::vector<ValueKind> column_kinds;
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

private:
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
	const HttpExecutionProfile public_profile {
	    PlannedUrlScheme::HTTPS,        "", 0, false, false, false, PAGINATION_MAX_EXECUTION_MILLISECONDS,
	    V1_MAX_DECODED_RECORDS_PER_PAGE};
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
	    profile.max_decoded_records == 0 || profile.max_decoded_records > V1_MAX_DECODED_RECORDS_PER_PAGE) {
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
