#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>

namespace duckdb_api {
namespace internal {

// DuckDB-free, redacted resource failure. Field names identify only the
// enforced capability; messages never contain remote data, destinations,
// credentials, or caller-supplied values. Executor integration maps this
// private error to Runtime's public resource error stage.
class ScanResourceError : public std::exception {
public:
	ScanResourceError(std::string field, std::string safe_message);

	const char *what() const noexcept override;
	const std::string &Field() const noexcept;
	const std::string &SafeMessage() const noexcept;

private:
	std::string field;
	std::string safe_message;
};

// Per-page ceilings. One call to BeginPage reserves the page's single request
// attempt before any side effect. Decoded memory is retained-at-once authority,
// not a cumulative byte total.
struct PageResourceLimits {
	uint64_t request_attempts;
	uint64_t header_bytes;
	uint64_t wire_response_bytes;
	uint64_t decompressed_response_bytes;
	uint64_t decoded_records;
	uint64_t decoded_memory_bytes;
	uint64_t active_requests;
};

// Per-scan ceilings shared by every page. Header, wire, decompressed, and
// decoded-record counters are cumulative. Decoded memory and active requests
// are instantaneous, and max_wall_milliseconds creates one deadline that is
// initialized by the first BeginPage and never reset.
struct ScanResourceLimits {
	uint64_t request_attempts;
	uint64_t pages;
	uint64_t header_bytes;
	uint64_t wire_response_bytes;
	uint64_t decompressed_response_bytes;
	uint64_t decoded_records;
	uint64_t retained_decoded_memory_bytes;
	uint64_t max_wall_milliseconds;
	uint64_t active_requests;
};

struct ScanResourceProfile {
	PageResourceLimits page;
	ScanResourceLimits scan;
};

// Limits passed to one page's transport and decoder. Every cumulative value is
// the minimum of page authority and the scan's remaining authority. The
// deadline is the same steady-clock time point for every page in a scan.
struct PageResourceAllowance {
	uint64_t header_bytes;
	uint64_t wire_response_bytes;
	uint64_t decompressed_response_bytes;
	uint64_t decoded_records;
	uint64_t decoded_memory_bytes;
	std::chrono::steady_clock::time_point deadline;
};

struct TransportResourceUsage {
	uint64_t header_bytes;
	uint64_t wire_response_bytes;
	uint64_t decompressed_response_bytes;
};

// decoded_memory_bytes includes every retained decoded row and normalized
// response-metadata allocation that remains live with the page.
struct DecodedPageResourceUsage {
	uint64_t decoded_records;
	uint64_t decoded_memory_bytes;
};

struct ScanResourceCounters {
	uint64_t request_attempts;
	uint64_t pages;
	uint64_t header_bytes;
	uint64_t wire_response_bytes;
	uint64_t decompressed_response_bytes;
	uint64_t decoded_records;
	uint64_t retained_decoded_memory_bytes;
	uint64_t peak_decoded_memory_bytes;
	uint64_t active_requests;
};

enum class ScanResourceState : uint8_t {
	READY,
	REQUEST_ACTIVE,
	TRANSPORT_COMMITTED,
	PAGE_DECODED,
	EXHAUSTED,
	FAILED
};

// Scan-owned sequential accounting state. The required ordering is:
//
//   BeginPage -> CommitTransport -> CommitDecodedPage -> CompletePage
//
// BeginPage debits one request and page before transport authority is returned.
// Commit methods validate the previously returned allowance before adding to
// aggregate counters. CompletePage releases retained decoded memory; when a
// next page is advertised it fails immediately if any cumulative scan budget
// or the one deadline has no remaining authority. AbortPage is the non-throwing
// cleanup path for a side-effect or parser failure and leaves reserved attempts
// debited. The class is not thread-safe; the owning BatchStream provides its
// synchronization and calls AbortPage during cancellation/close failure paths.
class ScanResourceAccounting {
public:
	explicit ScanResourceAccounting(const ScanResourceProfile &profile);

	PageResourceAllowance BeginPage(std::chrono::steady_clock::time_point now);
	void CommitTransport(const TransportResourceUsage &usage);
	void CommitDecodedPage(const DecodedPageResourceUsage &usage);
	void CompletePage(bool has_next, std::chrono::steady_clock::time_point now);
	void AbortPage() noexcept;

	const ScanResourceProfile &Profile() const noexcept;
	const ScanResourceCounters &Counters() const noexcept;
	ScanResourceState State() const noexcept;
	bool DeadlineStarted() const noexcept;
	std::chrono::steady_clock::time_point Deadline() const;

private:
	void RequireReadyForNext(std::chrono::steady_clock::time_point now);
	[[noreturn]] void Fail(std::string field, std::string message);

	ScanResourceProfile profile;
	ScanResourceCounters counters;
	ScanResourceState state;
	bool deadline_started;
	std::chrono::steady_clock::time_point deadline;
	PageResourceAllowance active_allowance;
};

} // namespace internal
} // namespace duckdb_api
