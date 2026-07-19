#include "duckdb_api/internal/http_paginated_scan.hpp"

#include "duckdb_api/internal/fixed_github_user_bearer_authenticator.hpp"
#include "duckdb_api/internal/decoded_page_buffer.hpp"
#include "duckdb_api/internal/http_scan_executor.hpp"
#include "duckdb_api/internal/json_decoder.hpp"
#include "duckdb_api/internal/link_pagination.hpp"
#include "duckdb_api/internal/scan_resource_accounting.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {
namespace {

bool HasRepositoryColumns(const std::vector<PlannedColumn> &columns) {
	return columns.size() == 5 && columns[0].name == "id" && columns[0].logical_type == "BIGINT" &&
	       !columns[0].nullable && columns[0].extractor == "$.id" && columns[1].name == "full_name" &&
	       columns[1].logical_type == "VARCHAR" && !columns[1].nullable && columns[1].extractor == "$.full_name" &&
	       columns[2].name == "private" && columns[2].logical_type == "BOOLEAN" && !columns[2].nullable &&
	       columns[2].extractor == "$.private" && columns[3].name == "fork" && columns[3].logical_type == "BOOLEAN" &&
	       !columns[3].nullable && columns[3].extractor == "$.fork" && columns[4].name == "archived" &&
	       columns[4].logical_type == "BOOLEAN" && !columns[4].nullable && columns[4].extractor == "$.archived";
}

bool HasFixedOperation(const PlannedRestOperation &operation, const HttpExecutionProfile &profile) {
	return operation.protocol == PlannedProtocol::REST && operation.method == PlannedHttpMethod::GET &&
	       operation.cardinality == PlannedCardinality::ZERO_TO_MANY &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.origin.scheme == profile.scheme &&
	       operation.origin.host == profile.host && operation.origin.port == profile.port &&
	       operation.operation_name == "github_authenticated_repositories" && operation.path == "/user/repos" &&
	       operation.query_parameters.size() == 2 && operation.query_parameters[0].name == "per_page" &&
	       operation.query_parameters[0].encoded_value == "100" && operation.query_parameters[1].name == "page" &&
	       operation.query_parameters[1].encoded_value == "1" && operation.headers.size() == 3 &&
	       operation.headers[0].name == "Accept" && operation.headers[0].value == "application/vnd.github+json" &&
	       operation.headers[1].name == "User-Agent" && operation.headers[1].value == "duckdb-api/0.5.0" &&
	       operation.headers[2].name == "X-GitHub-Api-Version" && operation.headers[2].value == "2022-11-28" &&
	       operation.response_source == PlannedResponseSource::ROOT_ARRAY && operation.records_extractor == "$";
}

bool HasFixedAuthorization(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	const auto &obligation = plan.AuthenticationObligation();
	const auto *destination = obligation.Destination();
	return plan.Authentication() == FeatureState::ENABLED && plan.SecretReference().IsPresent() &&
	       obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       destination->scheme == profile.scheme && destination->host == profile.host &&
	       destination->port == profile.port;
}

bool HasFixedNetwork(const NetworkCapability &network, const HttpExecutionProfile &profile) {
	return profile.scheme == PlannedUrlScheme::HTTPS && network.allowed_schemes.size() == 1 &&
	       network.allowed_schemes[0] == "https" && network.allowed_hosts.size() == 1 &&
	       network.allowed_hosts[0] == profile.host && !network.redirects_enabled &&
	       network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool SamePageBudgets(const ResourceBudgets &left, const ResourceBudgets &right) {
	return left.request_attempts == right.request_attempts && left.response_bytes == right.response_bytes &&
	       left.header_bytes == right.header_bytes && left.decompressed_bytes == right.decompressed_bytes &&
	       left.decoded_records == right.decoded_records &&
	       left.extracted_string_bytes == right.extracted_string_bytes && left.json_nesting == right.json_nesting &&
	       left.decoded_memory_bytes == right.decoded_memory_bytes && left.batch_rows == right.batch_rows &&
	       left.wall_milliseconds == right.wall_milliseconds && left.concurrency == right.concurrency;
}

bool HasFixedPagination(const ScanPlan &plan) {
	const auto &pagination = plan.Pagination();
	if (pagination.Strategy() != PlannedPaginationStrategy::LINK_HEADER ||
	    pagination.Dependency() != PlannedPageDependency::SEQUENTIAL ||
	    pagination.Consistency() != PlannedPageConsistency::MUTABLE ||
	    pagination.LinkRelation() != PlannedLinkRelation::NEXT ||
	    pagination.TargetScope() != PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH ||
	    pagination.SupportsTotal() || pagination.SupportsResume() ||
	    !pagination.PageBudgets().IsWithinPaginatedPageBounds() ||
	    !pagination.ScanBudgets().IsWithinPaginatedScanBounds() ||
	    !SamePageBudgets(plan.Budgets(), pagination.PageBudgets())) {
		return false;
	}
	const auto &target = pagination.Target();
	const auto &page = pagination.PageBudgets();
	const auto &scan = pagination.ScanBudgets();
	return target.origin.scheme == PlannedUrlScheme::HTTPS && target.origin.host == "api.github.com" &&
	       target.origin.port == 443 && target.path == "/user/repos" && target.page_size_parameter == "per_page" &&
	       target.page_size == 100 && target.page_number_parameter == "page" && target.first_page == 1 &&
	       target.page_increment == 1 && scan.request_attempts == scan.pages &&
	       scan.response_bytes >= page.response_bytes && scan.header_bytes >= page.header_bytes &&
	       scan.decompressed_bytes >= page.decompressed_bytes && scan.decoded_records >= page.decoded_records &&
	       scan.decoded_memory_bytes >= page.decoded_memory_bytes;
}

ValueKind PlannedValueKind(const PlannedColumn &column) {
	if (column.logical_type == "BIGINT") {
		return ValueKind::BIGINT;
	}
	if (column.logical_type == "VARCHAR") {
		return ValueKind::VARCHAR;
	}
	if (column.logical_type == "BOOLEAN") {
		return ValueKind::BOOLEAN;
	}
	throw ExecutionError(ErrorStage::POLICY, "", "scan plan contains an unsupported output type");
}

std::vector<ValueKind> BuildColumnKinds(const ScanPlan &plan) {
	std::vector<ValueKind> result;
	result.reserve(plan.OutputColumns().size());
	for (const auto &column : plan.OutputColumns()) {
		result.push_back(PlannedValueKind(column));
	}
	return result;
}

HttpRequest BuildPageRequest(const ScanPlan &plan, uint64_t page) {
	// `page` is the only mutable request fact and comes from LinkPaginationState.
	// Authority, path, field names, page size, and headers are reconstructed from
	// the already validated immutable plan; no received target string is sent.
	HttpRequest request;
	const auto &operation = plan.Operation();
	const auto &target = plan.Pagination().Target();
	request.method = "GET";
	request.scheme = "https";
	request.host = operation.origin.host;
	request.port = operation.origin.port;
	request.target = target.path + "?" + target.page_size_parameter + "=" + std::to_string(target.page_size) + "&" +
	                 target.page_number_parameter + "=" + std::to_string(page);
	for (const auto &header : operation.headers) {
		request.headers.push_back({header.name, header.value});
	}
	return request;
}

JsonDecodePlan BuildRepositoryDecodePlan(const ScanPlan &plan, const ResourceBudgets &budgets,
                                         uint64_t decoded_memory_bytes,
                                         std::chrono::steady_clock::time_point deadline) {
	JsonDecodePlan result;
	result.response_source = JsonResponseSource::ROOT_ARRAY;
	for (const auto &column : plan.OutputColumns()) {
		result.columns.push_back({column.name, column.extractor.substr(2), PlannedValueKind(column)});
	}
	result.max_records = budgets.decoded_records;
	result.max_string_bytes = budgets.extracted_string_bytes;
	result.max_json_nesting = budgets.json_nesting;
	result.max_decoded_memory_bytes = decoded_memory_bytes;
	result.deadline = deadline;
	return result;
}

ScanResourceProfile BuildResourceProfile(const ScanPlan &plan, uint64_t max_wall_milliseconds) {
	const auto &page = plan.Pagination().PageBudgets();
	const auto &scan = plan.Pagination().ScanBudgets();
	return {{page.request_attempts, page.header_bytes, page.response_bytes, page.decompressed_bytes,
	         page.decoded_records, page.decoded_memory_bytes, page.concurrency},
	        {scan.request_attempts, scan.pages, scan.header_bytes, scan.response_bytes, scan.decompressed_bytes,
	         scan.decoded_records, scan.decoded_memory_bytes, std::min(scan.wall_milliseconds, max_wall_milliseconds),
	         scan.concurrency}};
}

void CheckState(ExecutionControl &control, std::chrono::steady_clock::time_point deadline) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
	if (std::chrono::steady_clock::now() >= deadline) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
	}
}

void CheckStatus(uint32_t status) {
	if (status == 401) {
		throw ExecutionError(ErrorStage::AUTHENTICATION, "http_status", "HTTP endpoint rejected bearer authentication");
	}
	if (status == 403) {
		throw ExecutionError(ErrorStage::AUTHORIZATION, "http_status", "HTTP endpoint denied bearer authorization");
	}
	if (status != 200) {
		throw ExecutionError(ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status");
	}
}

class CombinedControl final : public ExecutionControl {
public:
	CombinedControl(ExecutionControl &outer_p, const std::atomic<bool> &cancelled_p)
	    : outer(outer_p), cancelled(cancelled_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire) || outer.IsCancellationRequested();
	}

private:
	ExecutionControl &outer;
	const std::atomic<bool> &cancelled;
};

// Pull-driven sequential page state. One stream owns one capability, one
// deadline/accounting state, one typed Link state, one decoded page, and at
// most one active request. The next page is not requested until all rows from
// the current page have been transferred. Empty nonterminal pages are crossed
// inside the same pull, preserving BatchStream's nonempty-success contract.
class PaginatedBatchStream final : public BatchStream {
public:
	PaginatedBatchStream(const ScanPlan &plan_p, ScanAuthorization authorization_p,
	                     std::shared_ptr<const HttpTransport> transport_p, uint64_t max_wall_milliseconds_p)
	    : plan(plan_p), transport(std::move(transport_p)),
	      authorization(new ScanAuthorization(std::move(authorization_p))), column_kinds(BuildColumnKinds(plan)),
	      accounting(BuildResourceProfile(plan, max_wall_milliseconds_p)), cancelled(false), closed(false),
	      exhausted(false), page_loaded(false), page_has_next(false), offset(0) {
	}

	~PaginatedBatchStream() noexcept override {
		Close();
	}

	bool Next(ExecutionControl &control, TypedBatch &batch) override {
		std::unique_lock<std::mutex> guard;
		try {
			guard = std::unique_lock<std::mutex>(mutex);
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
		CombinedControl combined(control, cancelled);
		try {
			while (true) {
				if (accounting.DeadlineStarted()) {
					CheckState(combined, accounting.Deadline());
				} else if (combined.IsCancellationRequested()) {
					throw ExecutionCancelled();
				}
				if (page_loaded && offset < decoded.Rows().size()) {
					return ProduceBatch(combined, batch);
				}
				if (page_loaded) {
					decoded.Release();
					offset = 0;
					page_loaded = false;
					accounting.CompletePage(page_has_next, std::chrono::steady_clock::now());
					if (!page_has_next) {
						exhausted = true;
						authorization.reset();
						return false;
					}
				}
				FetchPage(combined);
			}
		} catch (const ExecutionCancelled &) {
			RememberCurrentFailure();
			throw;
		} catch (const ExecutionError &) {
			RememberCurrentFailure();
			throw;
		} catch (const LinkPaginationError &error) {
			FailWithExecutionError(ErrorStage::POLICY, error.Field(), error.SafeMessage());
		} catch (const ScanResourceError &error) {
			FailWithExecutionError(ErrorStage::RESOURCE, error.Field(), error.SafeMessage());
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
			std::lock_guard<std::mutex> guard(mutex);
			if (!closed) {
				ReleasePrivateState();
			}
		} catch (...) {
		}
	}

	void Close() noexcept override {
		cancelled.store(true, std::memory_order_release);
		try {
			std::lock_guard<std::mutex> guard(mutex);
			if (closed) {
				return;
			}
			closed = true;
			ReleasePrivateState();
		} catch (...) {
		}
	}

private:
	bool ProduceBatch(ExecutionControl &control, TypedBatch &batch) {
		CheckState(control, accounting.Deadline());
		const auto remaining = decoded.Rows().size() - offset;
		const auto count = std::min(remaining, static_cast<std::size_t>(plan.Budgets().batch_rows));
		if (count == 0) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "runtime attempted to produce an empty successful batch");
		}
		TypedBatch produced;
		produced.column_kinds = column_kinds;
		produced.rows.reserve(count);
		for (std::size_t index = 0; index < count; index++) {
			CheckState(control, accounting.Deadline());
			produced.rows.push_back(std::move(decoded.Rows()[offset + index]));
		}
		offset += count;
		if (produced.rows.empty() || !produced.IsSchemaAligned()) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "runtime produced a misaligned typed batch");
		}
		batch = std::move(produced);
		return true;
	}

	void FetchPage(ExecutionControl &control) {
		const auto allowance = accounting.BeginPage(std::chrono::steady_clock::now());
		CheckState(control, allowance.deadline);
		auto request = BuildPageRequest(plan, pagination.CurrentPage());
		request = FixedGithubUserBearerAuthenticator::Authorize(plan, std::move(request), *authorization);
		const HttpLimits limits {allowance.header_bytes, allowance.wire_response_bytes,
		                         allowance.decompressed_response_bytes,
		                         std::min(allowance.header_bytes, allowance.decoded_memory_bytes), allowance.deadline};
		auto response = transport->Get(request, limits, control);
		CheckState(control, allowance.deadline);
		if (response.header_bytes > limits.max_header_bytes || response.response_bytes > limits.max_response_bytes ||
		    static_cast<uint64_t>(response.body.size()) > limits.max_decompressed_bytes ||
		    response.metadata.retained_bytes > limits.max_metadata_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP response exceeded an execution budget");
		}
		accounting.CommitTransport(
		    {response.header_bytes, response.response_bytes, static_cast<uint64_t>(response.body.size())});
		CheckStatus(response.status);
		if (response.metadata.retained_bytes >= allowance.decoded_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "HTTP response metadata exhausted the decoded-page memory budget");
		}
		const auto decoder_memory = allowance.decoded_memory_bytes - response.metadata.retained_bytes;
		auto page = DecodeJsonPage(
		    response.body,
		    BuildRepositoryDecodePlan(plan, plan.Pagination().PageBudgets(), decoder_memory, allowance.deadline),
		    control);
		CheckState(control, allowance.deadline);
		const auto transition = pagination.Advance(response.metadata.link_field_values);
		const auto retained_memory = page.retained_memory_bytes + response.metadata.retained_bytes;
		accounting.CommitDecodedPage({static_cast<uint64_t>(page.rows.size()), retained_memory});
		decoded.Install(std::move(page.rows));
		offset = 0;
		page_has_next = transition.has_next;
		page_loaded = true;
	}

	void ReleasePrivateState() noexcept {
		if (accounting.State() != ScanResourceState::EXHAUSTED) {
			accounting.AbortPage();
		}
		authorization.reset();
		decoded.Release();
		offset = 0;
		page_loaded = false;
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
	std::unique_ptr<ScanAuthorization> authorization;
	const std::vector<ValueKind> column_kinds;
	ScanResourceAccounting accounting;
	LinkPaginationState pagination;
	mutable std::mutex mutex;
	std::atomic<bool> cancelled;
	bool closed;
	bool exhausted;
	std::exception_ptr terminal_exception;
	bool page_loaded;
	bool page_has_next;
	DecodedPageBuffer decoded;
	std::size_t offset;
};

} // namespace

bool IsInstalledAuthenticatedRepositoriesPlan(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	return plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.5.0" &&
	       plan.RelationName() == "authenticated_repositories" &&
	       plan.Domain() == BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS && HasRepositoryColumns(plan.OutputColumns()) &&
	       HasFixedOperation(plan.Operation(), profile) && HasFixedPagination(plan) &&
	       plan.Budgets().decoded_records <= profile.max_decoded_records &&
	       plan.Providers() == FeatureState::DISABLED && plan.Retry() == FeatureState::DISABLED &&
	       plan.Cache() == FeatureState::DISABLED && HasFixedAuthorization(plan, profile) &&
	       HasFixedNetwork(plan.Network(), profile);
}

std::unique_ptr<BatchStream> OpenAuthenticatedRepositoriesScan(const ScanPlan &plan, ScanAuthorization authorization,
                                                               std::shared_ptr<const HttpTransport> transport,
                                                               uint64_t max_wall_milliseconds,
                                                               ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
	try {
		return std::unique_ptr<BatchStream>(
		    new PaginatedBatchStream(plan, std::move(authorization), std::move(transport), max_wall_milliseconds));
	} catch (const ExecutionError &) {
		throw;
	} catch (const ScanResourceError &error) {
		throw ExecutionError(ErrorStage::RESOURCE, error.Field(), error.SafeMessage());
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
		                     "scan stream could not be allocated within its memory budget");
	} catch (...) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "scan stream initialization failed");
	}
}

} // namespace internal
} // namespace duckdb_api
