#include "duckdb_api/internal/http_scan_executor.hpp"

#include "duckdb_api/internal/fixed_github_user_bearer_authenticator.hpp"
#include "duckdb_api/internal/http_paginated_scan.hpp"
#include "duckdb_api/internal/json_decoder.hpp"

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

enum class InstalledOperation { ANONYMOUS_SEARCH, AUTHENTICATED_USER };

bool HasExpectedColumns(const std::vector<PlannedColumn> &columns) {
	return columns.size() == 3 && columns[0].name == "id" && columns[0].logical_type == "BIGINT" &&
	       !columns[0].nullable && columns[0].extractor == "$.id" && columns[1].name == "login" &&
	       columns[1].logical_type == "VARCHAR" && !columns[1].nullable && columns[1].extractor == "$.login" &&
	       columns[2].name == "site_admin" && columns[2].logical_type == "BOOLEAN" && !columns[2].nullable &&
	       columns[2].extractor == "$.site_admin";
}

const char *SchemeName(PlannedUrlScheme scheme) {
	switch (scheme) {
	case PlannedUrlScheme::HTTP:
		return "http";
	case PlannedUrlScheme::HTTPS:
		return "https";
	}
	throw ExecutionError(ErrorStage::POLICY, "", "execution profile contains an unknown URL scheme");
}

bool HasFixedHeaders(const std::vector<PlannedHttpHeader> &headers) {
	return headers.size() == 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.5.0" &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool HasCommonOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return operation.protocol == PlannedProtocol::REST && operation.method == PlannedHttpMethod::GET &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.origin.scheme == profile.scheme &&
	       operation.origin.host == profile.host && operation.origin.port == profile.port &&
	       HasFixedHeaders(operation.headers);
}

bool HasExpectedAnonymousOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return HasCommonOperation(operation, profile) && operation.operation_name == "github_search_duckdb_login_page" &&
	       operation.cardinality == PlannedCardinality::ZERO_TO_MANY && operation.path == "/search/users" &&
	       operation.query_parameters.size() == 2 && operation.query_parameters[0].name == "q" &&
	       operation.query_parameters[0].encoded_value == "duckdb+in%3Alogin" &&
	       operation.query_parameters[1].name == "per_page" && operation.query_parameters[1].encoded_value == "3" &&
	       operation.response_source == PlannedResponseSource::JSON_PATH_MANY &&
	       operation.records_extractor == "$.items[*]";
}

bool HasExpectedAuthenticatedOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return HasCommonOperation(operation, profile) && operation.operation_name == "github_authenticated_user" &&
	       operation.cardinality == PlannedCardinality::EXACTLY_ONE_ON_SUCCESS && operation.path == "/user" &&
	       operation.query_parameters.empty() && operation.response_source == PlannedResponseSource::ROOT_OBJECT &&
	       operation.records_extractor == "$";
}

bool HasAnonymousObligation(const PlannedAuthenticationObligation &obligation) {
	return obligation.Requirement() == PlannedCredentialRequirement::NONE && obligation.LogicalCredential().empty() &&
	       obligation.Authenticator() == PlannedAuthenticator::NONE &&
	       obligation.Placement() == PlannedCredentialPlacement::NONE && obligation.Destination() == nullptr;
}

bool HasAuthenticatedObligation(const PlannedAuthenticationObligation &obligation,
                                const HttpExecutionProfile &profile) {
	const auto *destination = obligation.Destination();
	return obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       destination->scheme == profile.scheme && destination->host == profile.host &&
	       destination->port == profile.port;
}

bool HasExpectedNetwork(const NetworkCapability &network, const HttpExecutionProfile &profile) {
	return network.allowed_schemes.size() == 1 && network.allowed_schemes[0] == SchemeName(profile.scheme) &&
	       network.allowed_hosts.size() == 1 && network.allowed_hosts[0] == profile.host &&
	       !network.redirects_enabled && network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool TryValidateSingleResponsePlan(const ScanPlan &plan, const HttpExecutionProfile &profile,
                                   InstalledOperation &installed) {
	if (plan.ConnectorName() != "github" || plan.ConnectorVersion() != "0.5.0" ||
	    !HasExpectedColumns(plan.OutputColumns()) ||
	    plan.Pagination().Strategy() != PlannedPaginationStrategy::DISABLED ||
	    plan.Providers() != FeatureState::DISABLED || plan.Retry() != FeatureState::DISABLED ||
	    plan.Cache() != FeatureState::DISABLED || !HasExpectedNetwork(plan.Network(), profile) ||
	    !plan.Budgets().IsWithinLiveRestBounds() || plan.Budgets().decoded_records > profile.max_decoded_records) {
		return false;
	}
	if (plan.RelationName() == "duckdb_login_search_page" && plan.Domain() == BaseDomain::JSON_PATH_RECORDS &&
	    HasExpectedAnonymousOperation(plan.Operation(), profile) && plan.Authentication() == FeatureState::DISABLED &&
	    HasAnonymousObligation(plan.AuthenticationObligation()) && !plan.SecretReference().IsPresent()) {
		installed = InstalledOperation::ANONYMOUS_SEARCH;
		return true;
	}
	if (plan.RelationName() == "authenticated_user" && plan.Domain() == BaseDomain::SUCCESSFUL_ROOT_OBJECT &&
	    HasExpectedAuthenticatedOperation(plan.Operation(), profile) &&
	    plan.Authentication() == FeatureState::ENABLED &&
	    HasAuthenticatedObligation(plan.AuthenticationObligation(), profile) && plan.SecretReference().IsPresent()) {
		installed = InstalledOperation::AUTHENTICATED_USER;
		return true;
	}
	return false;
}

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
	result.response_source = plan.Operation().response_source == PlannedResponseSource::ROOT_OBJECT
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
	HttpBatchStream(const ScanPlan &plan_p, ScanAuthorization authorization_p, InstalledOperation installed_operation_p,
	                std::shared_ptr<const HttpTransport> transport_p, uint64_t max_wall_milliseconds_p)
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
				const HttpLimits limits {plan.Budgets().header_bytes, plan.Budgets().response_bytes,
				                         plan.Budgets().decompressed_bytes, 0, deadline};
				auto request = BuildRequest(plan.Operation());
				if (installed_operation == InstalledOperation::AUTHENTICATED_USER) {
					request = FixedGithubUserBearerAuthenticator::Authorize(plan, std::move(request), *authorization);
				}
				auto response = transport->Get(request, limits, combined);
				authorization.reset();
				CheckExecutionState(combined, deadline);
				if (response.header_bytes > limits.max_header_bytes ||
				    response.response_bytes > limits.max_response_bytes ||
				    static_cast<uint64_t>(response.body.size()) > limits.max_decompressed_bytes ||
				    response.metadata.retained_bytes > limits.max_metadata_bytes) {
					throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP response exceeded an execution budget");
				}
				if (installed_operation == InstalledOperation::AUTHENTICATED_USER && response.status == 401) {
					throw ExecutionError(ErrorStage::AUTHENTICATION, "http_status",
					                     "HTTP endpoint rejected bearer authentication");
				}
				if (installed_operation == InstalledOperation::AUTHENTICATED_USER && response.status == 403) {
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
	const InstalledOperation installed_operation;
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
		InstalledOperation installed = InstalledOperation::ANONYMOUS_SEARCH;
		if (!TryValidateSingleResponsePlan(plan, profile, installed)) {
			if (IsInstalledAuthenticatedRepositoriesPlan(plan, profile)) {
				throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
				                     "authenticated execution requires a bearer authorization capability");
			}
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		if (installed != InstalledOperation::ANONYMOUS_SEARCH) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authenticated execution requires a bearer authorization capability");
		}
		return OpenSingle(plan, ScanAuthorization::Anonymous(), installed, control);
	}

protected:
	std::unique_ptr<BatchStream> OpenAuthorizationEnvelope(const ScanPlan &plan, ScanAuthorization authorization,
	                                                       ExecutionControl &control) const override {
		if (IsInstalledAuthenticatedRepositoriesPlan(plan, profile)) {
			if (AlternativeOf(authorization) != AuthorizationAlternative::GITHUB_USER_BEARER) {
				throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
				                     "authorization capability does not match the scan plan");
			}
			return OpenAuthenticatedRepositoriesScan(plan, std::move(authorization), transport,
			                                         profile.max_wall_milliseconds, control);
		}
		InstalledOperation installed = InstalledOperation::ANONYMOUS_SEARCH;
		if (!TryValidateSingleResponsePlan(plan, profile, installed)) {
			throw ExecutionError(ErrorStage::POLICY, "", "scan plan is outside the installed execution profile");
		}
		const auto alternative = AlternativeOf(authorization);
		const bool matches =
		    (installed == InstalledOperation::ANONYMOUS_SEARCH && alternative == AuthorizationAlternative::ANONYMOUS) ||
		    (installed == InstalledOperation::AUTHENTICATED_USER &&
		     alternative == AuthorizationAlternative::GITHUB_USER_BEARER);
		if (!matches) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization",
			                     "authorization capability does not match the scan plan");
		}
		return OpenSingle(plan, std::move(authorization), installed, control);
	}

private:
	std::unique_ptr<BatchStream> OpenSingle(const ScanPlan &plan, ScanAuthorization authorization,
	                                        InstalledOperation installed, ExecutionControl &control) const {
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
