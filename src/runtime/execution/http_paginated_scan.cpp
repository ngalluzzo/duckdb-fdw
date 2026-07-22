#include "duckdb_api/internal/runtime/execution/http_paginated_scan.hpp"

#include "duckdb_api/internal/runtime/authentication/api_key_authenticator.hpp"
#include "duckdb_api/internal/runtime/authentication/bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/decoding/decoded_page_buffer.hpp"
#include "duckdb_api/internal/runtime/decoding/json_decoder.hpp"
#include "duckdb_api/internal/runtime/pagination/link_pagination.hpp"
#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"

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

std::vector<ValueKind> BuildColumnKinds(const AdmittedPaginatedRestRequestProfile &profile) {
	std::vector<ValueKind> result;
	result.reserve(profile.Columns().size());
	for (const auto &column : profile.Columns()) {
		result.push_back(column.kind);
	}
	return result;
}

JsonDecodePlan BuildPaginatedRestDecodePlan(const AdmittedPaginatedRestRequestProfile &profile,
                                            const ResourceBudgets &budgets, uint64_t decoded_memory_bytes,
                                            std::chrono::steady_clock::time_point deadline) {
	JsonDecodePlan result;
	result.response_source = profile.ResponseSource() == PlannedResponseSource::ROOT_ARRAY
	                             ? JsonResponseSource::ROOT_ARRAY
	                             : JsonResponseSource::JSON_PATH_MANY;
	result.records_path = profile.RecordsPath();
	for (const auto &column : profile.Columns()) {
		result.columns.push_back(JsonColumnPlan(column.name, column.source_path, column.kind, column.nullable));
	}
	// response_next: declare the page-level continuation path so the decoder
	// extracts the body URL during the same pass that produces rows. The path
	// arrives as a json_path_v1 string ($.field.field); split it into segments
	// for the decoder.
	if (profile.PaginationStrategy() == PlannedPaginationStrategy::RESPONSE_NEXT_URL &&
	    !profile.NextUrlPath().empty()) {
		std::vector<std::string> segments;
		const auto &path = profile.NextUrlPath();
		std::size_t start = 0;
		if (path.size() >= 2 && path[0] == '$' && path[1] == '.') {
			start = 2;
		}
		while (start < path.size()) {
			const auto dot = path.find('.', start);
			if (dot == std::string::npos) {
				segments.emplace_back(path, start);
				break;
			}
			segments.emplace_back(path, start, dot - start);
			start = dot + 1;
		}
		result.page_continuation_path = std::move(segments);
	}
	result.max_records = budgets.decoded_records;
	result.max_string_bytes = budgets.extracted_string_bytes;
	result.max_json_nesting = budgets.json_nesting;
	result.max_decoded_memory_bytes = decoded_memory_bytes;
	result.deadline = deadline;
	return result;
}

ScanResourceProfile BuildResourceProfile(const AdmittedPaginatedRestRequestProfile &profile,
                                         uint64_t max_wall_milliseconds) {
	const auto &page = profile.PageBudgets();
	const auto &scan = profile.ScanBudgets();
	return {{page.request_attempts, page.header_bytes, page.response_bytes, page.decompressed_bytes,
	         page.decoded_records, page.decoded_memory_bytes, page.concurrency, page.serialized_request_body_bytes},
	        {scan.request_attempts, scan.pages, scan.header_bytes, scan.response_bytes, scan.decompressed_bytes,
	         scan.decoded_records, scan.decoded_memory_bytes, std::min(scan.wall_milliseconds, max_wall_milliseconds),
	         scan.concurrency, scan.serialized_request_body_bytes}};
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
		throw ExecutionError(ErrorStage::AUTHENTICATION, "http_status", "HTTP endpoint rejected authentication");
	}
	if (status == 403) {
		throw ExecutionError(ErrorStage::AUTHORIZATION, "http_status", "HTTP endpoint denied authorization");
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
	PaginatedBatchStream(std::unique_ptr<const AdmittedPaginatedRestRequestProfile> admitted_profile_p,
	                     ScanAuthorization authorization_p, std::shared_ptr<const HttpTransport> transport_p,
	                     uint64_t max_wall_milliseconds_p)
	    : admitted_profile(std::move(admitted_profile_p)), transport(std::move(transport_p)),
	      authorization(new ScanAuthorization(std::move(authorization_p))),
	      column_kinds(BuildColumnKinds(*admitted_profile)),
	      accounting(BuildResourceProfile(*admitted_profile, max_wall_milliseconds_p)), pagination(*admitted_profile),
	      cancelled(false), closed(false), exhausted(false), page_loaded(false), page_has_next(false), offset(0) {
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
		const auto count = std::min(remaining, static_cast<std::size_t>(admitted_profile->PageBudgets().batch_rows));
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
		auto request = BuildAdmittedPaginatedRestPageRequest(*admitted_profile, pagination.CurrentPage());
		if (admitted_profile->RequiresBearer()) {
			request =
			    BearerAuthenticator::AuthorizePaginatedRest(*admitted_profile, std::move(request), *authorization);
		} else if (admitted_profile->RequiresApiKey()) {
			request =
			    ApiKeyAuthenticator::AuthorizePaginatedRest(*admitted_profile, std::move(request), *authorization);
		}
		const HttpLimits limits {0,
		                         allowance.header_bytes,
		                         allowance.wire_response_bytes,
		                         allowance.decompressed_response_bytes,
		                         std::min(allowance.header_bytes, allowance.decoded_memory_bytes),
		                         allowance.deadline};
		auto response = transport->Execute(request, limits, control);
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
		auto page = DecodeJsonPage(response.body,
		                           BuildPaginatedRestDecodePlan(*admitted_profile, admitted_profile->PageBudgets(),
		                                                        decoder_memory, allowance.deadline),
		                           control);
		CheckState(control, allowance.deadline);
		// response_next reads the continuation signal from the decoded body
		// (page.next_url); link_next reads it from response headers. Both
		// share the same reconstruct-and-verify state machine.
		const auto transition = admitted_profile->PaginationStrategy() == PlannedPaginationStrategy::RESPONSE_NEXT_URL
		                            ? pagination.AdvanceBody(page.next_url)
		                            : pagination.Advance(response.metadata.link_field_values);
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

	const std::unique_ptr<const AdmittedPaginatedRestRequestProfile> admitted_profile;
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

std::unique_ptr<BatchStream>
OpenPaginatedRestScan(std::unique_ptr<const AdmittedPaginatedRestRequestProfile> admitted_profile,
                      ScanAuthorization authorization, std::shared_ptr<const HttpTransport> transport,
                      uint64_t max_wall_milliseconds, ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
	if (!admitted_profile) {
		throw ExecutionError(ErrorStage::POLICY, "", "paginated REST request profile was not admitted");
	}
	try {
		return std::unique_ptr<BatchStream>(new PaginatedBatchStream(
		    std::move(admitted_profile), std::move(authorization), std::move(transport), max_wall_milliseconds));
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
