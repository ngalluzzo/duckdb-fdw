#include "duckdb_api/internal/runtime/execution/graphql_paginated_scan.hpp"

#include "duckdb_api/internal/runtime/authentication/bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/decoding/decoded_page_buffer.hpp"
#include "duckdb_api/internal/runtime/decoding/graphql_response_decoder.hpp"
#include "duckdb_api/internal/runtime/pagination/graphql_cursor_pagination.hpp"
#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"
#include "duckdb_api/internal/runtime/transport/graphql_request_body.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {
namespace {

std::vector<OutputValueType> ColumnTypes(const AdmittedGraphqlRequestProfile &profile) {
	std::vector<OutputValueType> result;
	result.reserve(profile.Columns().size());
	for (const auto &column : profile.Columns()) {
		result.push_back(column.type);
	}
	return result;
}

ScanResourceProfile ResourceProfile(const AdmittedGraphqlRequestProfile &profile, uint64_t max_wall_milliseconds) {
	const auto &page = profile.PageBudgets();
	const auto &scan = profile.ScanBudgets();
	return {{page.request_attempts, page.header_bytes, page.response_bytes, page.decompressed_bytes,
	         page.decoded_records, page.decoded_memory_bytes, page.concurrency, page.serialized_request_body_bytes},
	        {scan.request_attempts, scan.pages, scan.header_bytes, scan.response_bytes, scan.decompressed_bytes,
	         scan.decoded_records, scan.decoded_memory_bytes, std::min(scan.wall_milliseconds, max_wall_milliseconds),
	         scan.concurrency, scan.serialized_request_body_bytes, 0}};
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
		throw ExecutionError(ErrorStage::AUTHENTICATION, "http_status", "HTTP endpoint rejected authentication",
		                     HttpStatusFailureProperties(status, true));
	}
	if (status == 403) {
		throw ExecutionError(ErrorStage::AUTHORIZATION, "http_status", "HTTP endpoint denied authorization",
		                     HttpStatusFailureProperties(status, true));
	}
	if (status != 200) {
		throw ExecutionError(ErrorStage::HTTP_STATUS, "", "HTTP endpoint returned a non-success status",
		                     HttpStatusFailureProperties(status, false));
	}
}

// RFC 0021: map a GraphqlCursorErrorKind to failure properties so the scan catch
// boundary classifies cursor failures structurally rather than by message text.
// The ErrorStage/field/safe_message still render unchanged; this only attaches
// the additive FailureClass. step/rows_exposed default to zero and are enriched
// at the catch boundary.
FailureProperties GraphqlCursorFailureProperties(GraphqlCursorErrorKind kind) {
	FailureProperties properties {};
	properties.phase = FailurePhase::PAGINATE;
	properties.attempt = 1;
	properties.replay_classification = ReplayClassification::REPLAYABLE_BEFORE_EXPOSURE;
	switch (kind) {
	case GraphqlCursorErrorKind::PROFILE:
		properties.failure_class = FailureClass::CONFIGURATION;
		return properties;
	case GraphqlCursorErrorKind::RESOURCE_BUDGET:
		properties.failure_class = FailureClass::RESOURCE_BUDGET;
		return properties;
	case GraphqlCursorErrorKind::PROTOCOL:
		properties.failure_class = FailureClass::PROTOCOL;
		return properties;
	}
	throw std::logic_error("unknown GraphqlCursorErrorKind");
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

class GraphqlBatchStream final : public BatchStream {
public:
	GraphqlBatchStream(std::unique_ptr<const AdmittedGraphqlRequestProfile> admitted_profile_p,
	                   ScanAuthorization authorization_p, std::shared_ptr<const HttpTransport> transport_p,
	                   uint64_t max_wall_milliseconds_p)
	    : admitted_profile(std::move(admitted_profile_p)), transport(std::move(transport_p)),
	      authorization(new ScanAuthorization(std::move(authorization_p))),
	      column_types(ColumnTypes(*admitted_profile)),
	      accounting(ResourceProfile(*admitted_profile, max_wall_milliseconds_p)),
	      cursor(admitted_profile->MaxPages(), 512), cancelled(false), closed(false), exhausted(false),
	      page_loaded(false), page_has_next(false), decoded_memory_bytes(0), decoded_memory_allowance(0), offset(0),
	      rows_emitted(0) {
	}

	~GraphqlBatchStream() noexcept override {
		Close();
	}

	bool Next(ExecutionControl &control, TypedBatch &batch) override {
		std::unique_lock<std::mutex> guard;
		try {
			guard = std::unique_lock<std::mutex>(mutex);
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
						cursor.Release();
						return false;
					}
				}
				FetchPage(combined);
			}
		} catch (const ExecutionCancelled &) {
			RememberCurrentFailure();
			throw;
		} catch (const ExecutionError &error) {
			FailWithExecutionError(
			    error.Stage(), error.Field(), error.SafeMessage(),
			    EnrichFailureProperties(FailurePropertiesFromError(error), accounting.Counters().pages, rows_emitted));
		} catch (const GraphqlCursorError &error) {
			FailWithExecutionError(ErrorStage::POLICY, error.Field(), error.SafeMessage(),
			                       EnrichFailureProperties(GraphqlCursorFailureProperties(error.Kind()),
			                                               accounting.Counters().pages, rows_emitted));
		} catch (const ScanResourceError &error) {
			FailWithExecutionError(ErrorStage::RESOURCE, error.Field(), error.SafeMessage(),
			                       EnrichFailureProperties(ResourceBudgetFailureProperties(error.Field()),
			                                               accounting.Counters().pages, rows_emitted));
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
		RequireTypedBatchHandoffMemory(decoded_memory_bytes, decoded_memory_allowance, count, column_types.size());
		TypedBatch produced;
		produced.column_types = column_types;
		produced.rows.reserve(count);
		RequireTypedBatchHandoffMemory(decoded_memory_bytes, decoded_memory_allowance, produced.rows.capacity(),
		                               produced.column_types.capacity());
		for (std::size_t index = 0; index < count; index++) {
			CheckState(control, accounting.Deadline());
			produced.rows.push_back(std::move(decoded.Rows()[offset + index]));
		}
		if (produced.rows.empty() || !produced.IsSchemaAligned(control)) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "runtime produced a misaligned typed batch");
		}
		// The destination batch becomes observable only after the final checkpoint;
		// cancellation concurrent with row movement cannot publish partial success.
		CheckState(control, accounting.Deadline());
		offset += count;
		rows_emitted += static_cast<uint64_t>(count);
		batch = std::move(produced);
		return true;
	}

	void FetchPage(ExecutionControl &control) {
		const auto allowance = accounting.BeginPage(std::chrono::steady_clock::now());
		CheckState(control, allowance.deadline);
		const auto *request_cursor = cursor.CurrentCursor();
		cursor.MarkRequestStarted();
		auto request = BuildAdmittedGraphqlRequest(*admitted_profile, request_cursor);
		accounting.CommitRequestBody(static_cast<uint64_t>(request.body.size()));
		CheckState(control, allowance.deadline);
		if (admitted_profile->RequiresBearer()) {
			request = BearerAuthenticator::AuthorizeGraphql(*admitted_profile, std::move(request), *authorization);
		}
		const HttpLimits limits {allowance.serialized_request_body_bytes,
		                         allowance.header_bytes,
		                         allowance.wire_response_bytes,
		                         allowance.decompressed_response_bytes,
		                         0,
		                         allowance.deadline};
		auto response = transport->Execute(request, limits, control);
		CheckState(control, allowance.deadline);
		if (response.header_bytes > limits.max_header_bytes || response.response_bytes > limits.max_response_bytes ||
		    static_cast<uint64_t>(response.body.size()) > limits.max_decompressed_bytes ||
		    response.metadata.retained_bytes != 0 || !response.metadata.link_field_values.empty()) {
			throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP response exceeded an execution budget");
		}
		accounting.CommitTransport(
		    {response.header_bytes, response.response_bytes, static_cast<uint64_t>(response.body.size())});
		CheckStatus(response.status);
		const auto cursor_memory_before = cursor.RetainedMemoryBytes();
		if (cursor_memory_before >= allowance.decoded_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL cursor state exhausted the decoded-memory budget");
		}
		const GraphqlDecodeLimits decode_limits {
		    allowance.decoded_records, admitted_profile->PageBudgets().extracted_string_bytes,
		    admitted_profile->PageBudgets().json_nesting, allowance.decoded_memory_bytes - cursor_memory_before,
		    allowance.deadline};
		auto page = DecodeGraphqlResponse(response.body, *admitted_profile, decode_limits, control);
		CheckState(control, allowance.deadline);
		page_has_next = page.has_next;
		cursor.Advance(page.has_next, std::move(page.end_cursor));
		const auto cursor_memory_after = cursor.RetainedMemoryBytes();
		if (page.retained_memory_bytes > allowance.decoded_memory_bytes ||
		    cursor_memory_after > allowance.decoded_memory_bytes - page.retained_memory_bytes) {
			throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
			                     "GraphQL page and cursor exceeded the decoded-memory budget");
		}
		const auto retained_memory = page.retained_memory_bytes + cursor_memory_after;
		accounting.CommitDecodedPage({static_cast<uint64_t>(page.rows.size()), retained_memory});
		decoded_memory_bytes = retained_memory;
		decoded_memory_allowance = allowance.decoded_memory_bytes;
		decoded.Install(std::move(page.rows));
		offset = 0;
		page_loaded = true;
	}

	void ReleasePrivateState() noexcept {
		if (accounting.State() != ScanResourceState::EXHAUSTED) {
			accounting.AbortPage();
		}
		authorization.reset();
		decoded.Release();
		decoded_memory_bytes = 0;
		decoded_memory_allowance = 0;
		cursor.Fail();
		offset = 0;
		page_loaded = false;
	}

	void RememberCurrentFailure() noexcept {
		terminal_exception = std::current_exception();
		cancelled.store(true, std::memory_order_release);
		ReleasePrivateState();
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

	const std::unique_ptr<const AdmittedGraphqlRequestProfile> admitted_profile;
	const std::shared_ptr<const HttpTransport> transport;
	std::unique_ptr<ScanAuthorization> authorization;
	const std::vector<OutputValueType> column_types;
	ScanResourceAccounting accounting;
	GraphqlCursorState cursor;
	mutable std::mutex mutex;
	std::atomic<bool> cancelled;
	bool closed;
	bool exhausted;
	std::exception_ptr terminal_exception;
	bool page_loaded;
	bool page_has_next;
	DecodedPageBuffer decoded;
	uint64_t decoded_memory_bytes;
	uint64_t decoded_memory_allowance;
	std::size_t offset;
	// RFC 0021: cumulative rows emitted to DuckDB across pages, for rows_exposed.
	uint64_t rows_emitted;
};

} // namespace

std::unique_ptr<BatchStream>
OpenGraphqlPaginatedScan(std::unique_ptr<const AdmittedGraphqlRequestProfile> admitted_profile,
                         ScanAuthorization authorization, std::shared_ptr<const HttpTransport> transport,
                         uint64_t max_wall_milliseconds, ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
	if (!admitted_profile) {
		throw ExecutionError(ErrorStage::POLICY, "", "GraphQL request profile was not admitted");
	}
	try {
		return std::unique_ptr<BatchStream>(new GraphqlBatchStream(
		    std::move(admitted_profile), std::move(authorization), std::move(transport), max_wall_milliseconds));
	} catch (const ExecutionError &) {
		throw;
	} catch (const ScanResourceError &error) {
		throw ExecutionError(ErrorStage::RESOURCE, error.Field(), error.SafeMessage());
	} catch (const GraphqlCursorError &error) {
		throw ExecutionError(ErrorStage::RESOURCE, error.Field(), error.SafeMessage(),
		                     GraphqlCursorFailureProperties(error.Kind()));
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "decoded_memory_bytes",
		                     "GraphQL scan stream could not be allocated within its memory budget");
	} catch (...) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "GraphQL scan stream initialization failed");
	}
}

} // namespace internal
} // namespace duckdb_api
