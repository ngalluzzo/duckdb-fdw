#include "duckdb_api/internal/scan_resource_accounting.hpp"
#include "support/require.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api::internal::DecodedPageResourceUsage;
using duckdb_api::internal::ScanResourceAccounting;
using duckdb_api::internal::ScanResourceError;
using duckdb_api::internal::ScanResourceProfile;
using duckdb_api::internal::ScanResourceState;
using duckdb_api::internal::TransportResourceUsage;
using duckdb_api_test::Require;

using Clock = std::chrono::steady_clock;

const std::string CANARY = "private-repository-canary";

ScanResourceProfile Profile() {
	return {{1, 10, 20, 30, 4, 50, 1}, {3, 3, 30, 60, 90, 12, 50, 100, 1}};
}

void RequireError(const std::function<void()> &action, const std::string &field, const std::string &label) {
	bool rejected = false;
	try {
		action();
	} catch (const ScanResourceError &error) {
		rejected = true;
		Require(error.Field() == field, label + " used the wrong resource field");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        label + " produced an empty or unbounded diagnostic");
		Require(error.SafeMessage().find(CANARY) == std::string::npos, label + " exposed caller or response content");
	}
	Require(rejected, label + " did not fail");
}

void CommitEmptyPage(ScanResourceAccounting &accounting) {
	accounting.CommitTransport({0, 0, 0});
	accounting.CommitDecodedPage({0, 0});
}

void TestProfileValidation() {
	auto invalid = Profile();
	invalid.page.request_attempts = 2;
	RequireError([&]() { ScanResourceAccounting accounting(invalid); }, "resource_profile",
	             "multiple per-page attempts");

	invalid = Profile();
	invalid.page.active_requests = 2;
	RequireError([&]() { ScanResourceAccounting accounting(invalid); }, "resource_profile",
	             "multiple per-page requests");

	invalid = Profile();
	invalid.scan.active_requests = 2;
	RequireError([&]() { ScanResourceAccounting accounting(invalid); }, "resource_profile", "multiple scan requests");

	invalid = Profile();
	invalid.page.header_bytes = 0;
	RequireError([&]() { ScanResourceAccounting accounting(invalid); }, "resource_profile", "zero page ceiling");

	invalid = Profile();
	invalid.scan.pages = 0;
	RequireError([&]() { ScanResourceAccounting accounting(invalid); }, "resource_profile", "zero scan pages");

	invalid = Profile();
	invalid.scan.max_wall_milliseconds = std::numeric_limits<uint64_t>::max();
	RequireError([&]() { ScanResourceAccounting accounting(invalid); }, "resource_profile",
	             "unrepresentable wall budget");
}

void TestExactSequentialLifecycle() {
	ScanResourceAccounting accounting(Profile());
	const Clock::time_point start;
	Require(accounting.State() == ScanResourceState::READY && !accounting.DeadlineStarted(),
	        "new accounting state was not ready");
	RequireError([&]() { accounting.Deadline(); }, "resource_state", "deadline before first page");

	Clock::time_point deadline;
	for (uint64_t page = 1; page <= 3; page++) {
		const auto allowance = accounting.BeginPage(start + std::chrono::milliseconds(page - 1));
		if (page == 1) {
			deadline = allowance.deadline;
			Require(deadline == start + std::chrono::milliseconds(100),
			        "first page did not establish the scan deadline");
		} else {
			Require(allowance.deadline == deadline, "a later page reset the scan deadline");
		}
		Require(allowance.header_bytes == 10 && allowance.wire_response_bytes == 20 &&
		            allowance.decompressed_response_bytes == 30 && allowance.decoded_records == 4 &&
		            allowance.decoded_memory_bytes == 50,
		        "page allowance did not preserve exact per-page ceilings");
		Require(accounting.State() == ScanResourceState::REQUEST_ACTIVE && accounting.Counters().active_requests == 1,
		        "BeginPage did not reserve one request before returning authority");

		accounting.CommitTransport({10, 20, 30});
		Require(accounting.State() == ScanResourceState::TRANSPORT_COMMITTED &&
		            accounting.Counters().active_requests == 0,
		        "transport commit did not release active-request authority");
		accounting.CommitDecodedPage({4, page * 10});
		Require(accounting.Counters().retained_decoded_memory_bytes == page * 10,
		        "decoded-page memory was not retained until page completion");
		accounting.CompletePage(page != 3, start + std::chrono::milliseconds(page));
		Require(accounting.Counters().retained_decoded_memory_bytes == 0, "completed page retained decoded memory");
	}

	const auto &counters = accounting.Counters();
	Require(accounting.State() == ScanResourceState::EXHAUSTED, "final page did not exhaust accounting state");
	Require(counters.request_attempts == 3 && counters.pages == 3 && counters.header_bytes == 30 &&
	            counters.wire_response_bytes == 60 && counters.decompressed_response_bytes == 90 &&
	            counters.decoded_records == 12 && counters.peak_decoded_memory_bytes == 30 &&
	            counters.active_requests == 0,
	        "exact-boundary counters drifted");
}

void TestAdvertisedNextFailsAtScanCeilings() {
	auto page_limited = Profile();
	page_limited.scan.pages = 1;
	ScanResourceAccounting pages(page_limited);
	const Clock::time_point start;
	pages.BeginPage(start);
	CommitEmptyPage(pages);
	RequireError([&]() { pages.CompletePage(true, start); }, "pages", "next page at page ceiling");
	Require(pages.State() == ScanResourceState::FAILED && pages.Counters().pages == 1,
	        "page-ceiling failure did not preserve the reserved page debit");

	auto attempt_limited = Profile();
	attempt_limited.scan.request_attempts = 1;
	ScanResourceAccounting attempts(attempt_limited);
	attempts.BeginPage(start);
	CommitEmptyPage(attempts);
	RequireError([&]() { attempts.CompletePage(true, start); }, "request_attempts", "next page at attempt ceiling");
	Require(attempts.State() == ScanResourceState::FAILED && attempts.Counters().request_attempts == 1,
	        "attempt-ceiling failure did not preserve the reserved attempt debit");

	auto byte_limited = Profile();
	byte_limited.scan.header_bytes = 10;
	ScanResourceAccounting bytes(byte_limited);
	bytes.BeginPage(start);
	bytes.CommitTransport({10, 0, 0});
	bytes.CommitDecodedPage({0, 0});
	RequireError([&]() { bytes.CompletePage(true, start); }, "header_bytes", "next page at aggregate byte ceiling");
}

void TestPerPageCeilingsFailClosed() {
	const Clock::time_point start;
	struct TransportCase {
		TransportResourceUsage usage;
		std::string field;
		std::string label;
	};
	const TransportCase transport_cases[] = {{{11, 0, 0}, "header_bytes", "page header ceiling"},
	                                         {{0, 21, 0}, "response_bytes", "page wire ceiling"},
	                                         {{0, 0, 31}, "decompressed_bytes", "page decompressed ceiling"}};
	for (const auto &test_case : transport_cases) {
		ScanResourceAccounting accounting(Profile());
		accounting.BeginPage(start);
		RequireError([&]() { accounting.CommitTransport(test_case.usage); }, test_case.field, test_case.label);
		Require(accounting.State() == ScanResourceState::FAILED && accounting.Counters().active_requests == 0,
		        test_case.label + " did not release request authority");
	}

	ScanResourceAccounting records(Profile());
	records.BeginPage(start);
	records.CommitTransport({0, 0, 0});
	RequireError([&]() { records.CommitDecodedPage({5, 0}); }, "decoded_records", "page record ceiling");
	Require(records.Counters().retained_decoded_memory_bytes == 0, "record-ceiling failure retained decoded memory");

	ScanResourceAccounting memory(Profile());
	memory.BeginPage(start);
	memory.CommitTransport({0, 0, 0});
	RequireError([&]() { memory.CommitDecodedPage({0, 51}); }, "decoded_memory_bytes", "page decoded-memory ceiling");
	Require(memory.Counters().retained_decoded_memory_bytes == 0, "memory-ceiling failure retained decoded memory");
}

void TestAggregateRemainingAuthority() {
	auto profile = Profile();
	profile.scan.header_bytes = 5;
	profile.scan.retained_decoded_memory_bytes = 40;
	ScanResourceAccounting narrowed(profile);
	const Clock::time_point start;
	const auto narrowed_allowance = narrowed.BeginPage(start);
	Require(narrowed_allowance.header_bytes == 5 && narrowed_allowance.decoded_memory_bytes == 40,
	        "aggregate authority did not narrow a wider per-page ceiling");
	narrowed.AbortPage();

	profile = Profile();
	profile.scan.header_bytes = 15;
	ScanResourceAccounting headers(profile);
	const auto first = headers.BeginPage(start);
	Require(first.header_bytes == 10, "first page header allowance drifted");
	headers.CommitTransport({9, 0, 0});
	headers.CommitDecodedPage({0, 0});
	headers.CompletePage(true, start);
	const auto second = headers.BeginPage(start);
	Require(second.header_bytes == 6, "second page did not receive remaining aggregate header authority");
	RequireError([&]() { headers.CommitTransport({7, 0, 0}); }, "header_bytes", "aggregate remaining header ceiling");

	profile = Profile();
	profile.scan.decoded_records = 6;
	ScanResourceAccounting records(profile);
	records.BeginPage(start);
	records.CommitTransport({0, 0, 0});
	records.CommitDecodedPage({4, 40});
	records.CompletePage(true, start);
	const auto record_allowance = records.BeginPage(start);
	Require(record_allowance.decoded_records == 2 && record_allowance.decoded_memory_bytes == 50,
	        "cumulative records and instantaneous memory were not accounted separately");
	records.CommitTransport({0, 0, 0});
	RequireError([&]() { records.CommitDecodedPage({3, 0}); }, "decoded_records", "aggregate remaining record ceiling");

	ScanResourceAccounting memory(Profile());
	memory.BeginPage(start);
	memory.CommitTransport({0, 0, 0});
	memory.CommitDecodedPage({0, 40});
	memory.CompletePage(true, start);
	Require(memory.BeginPage(start).decoded_memory_bytes == 50,
	        "released decoded memory incorrectly reduced the next page allowance");
	memory.CommitTransport({0, 0, 0});
	memory.CommitDecodedPage({0, 50});
	memory.CompletePage(false, start);
	Require(memory.Counters().peak_decoded_memory_bytes == 50 && memory.Counters().retained_decoded_memory_bytes == 0,
	        "instantaneous decoded-memory accounting drifted");
}

void TestDeadlineAndRepresentability() {
	const Clock::time_point start;
	ScanResourceAccounting accounting(Profile());
	const auto first = accounting.BeginPage(start);
	CommitEmptyPage(accounting);
	accounting.CompletePage(true, start + std::chrono::milliseconds(99));
	const auto second = accounting.BeginPage(start + std::chrono::milliseconds(99));
	Require(second.deadline == first.deadline, "deadline changed between pages");
	CommitEmptyPage(accounting);
	RequireError([&]() { accounting.CompletePage(false, first.deadline); }, "wall_milliseconds",
	             "completion at deadline");

	ScanResourceAccounting too_late(Profile());
	const auto near_max = Clock::time_point::max() - std::chrono::milliseconds(50);
	RequireError([&]() { too_late.BeginPage(near_max); }, "wall_milliseconds", "unrepresentable absolute deadline");
	Require(too_late.State() == ScanResourceState::FAILED && too_late.Counters().pages == 0,
	        "deadline construction failure reserved a page");

	ScanResourceAccounting earliest(Profile());
	const auto earliest_allowance = earliest.BeginPage(Clock::time_point::min());
	Require(earliest_allowance.deadline == Clock::time_point::min() + std::chrono::milliseconds(100),
	        "representable deadline near the clock minimum drifted");
	earliest.AbortPage();
}

void TestOrderingAbortAndTerminalState() {
	const Clock::time_point start;
	ScanResourceAccounting wrong_order(Profile());
	RequireError([&]() { wrong_order.CommitTransport({0, 0, 0}); }, "resource_state", "transport before BeginPage");
	Require(wrong_order.State() == ScanResourceState::FAILED, "invalid call ordering did not make accounting terminal");

	ScanResourceAccounting active(Profile());
	active.BeginPage(start);
	RequireError([&]() { active.BeginPage(start); }, "resource_state", "concurrent BeginPage");
	Require(active.Counters().request_attempts == 1 && active.Counters().pages == 1 &&
	            active.Counters().active_requests == 0,
	        "concurrent BeginPage did not preserve debits and release active authority");

	ScanResourceAccounting aborted(Profile());
	aborted.BeginPage(start);
	aborted.AbortPage();
	Require(aborted.State() == ScanResourceState::FAILED && aborted.Counters().request_attempts == 1 &&
	            aborted.Counters().pages == 1 && aborted.Counters().active_requests == 0,
	        "AbortPage did not retain replay-unit debits while releasing authority");
	aborted.AbortPage();
	Require(aborted.State() == ScanResourceState::FAILED, "AbortPage was not idempotent");
}

void TestMaximumCounterBoundary() {
	const auto maximum = std::numeric_limits<uint64_t>::max();
	const ScanResourceProfile profile = {{1, maximum, maximum, maximum, maximum, maximum, 1},
	                                     {2, 2, maximum, maximum, maximum, maximum, maximum, 100, 1}};
	ScanResourceAccounting accounting(profile);
	const Clock::time_point start;
	const auto allowance = accounting.BeginPage(start);
	Require(allowance.header_bytes == maximum && allowance.wire_response_bytes == maximum &&
	            allowance.decompressed_response_bytes == maximum && allowance.decoded_records == maximum,
	        "maximum counter authority was narrowed by arithmetic overflow");
	accounting.CommitTransport({maximum, maximum, maximum});
	accounting.CommitDecodedPage({maximum, maximum});
	RequireError([&]() { accounting.CompletePage(true, start); }, "header_bytes",
	             "advertised next after maximum aggregate usage");
	Require(accounting.Counters().header_bytes == maximum && accounting.Counters().decoded_records == maximum,
	        "maximum exact-boundary commit overflowed or wrapped");
}

} // namespace

int main() {
	try {
		TestProfileValidation();
		TestExactSequentialLifecycle();
		TestAdvertisedNextFailsAtScanCeilings();
		TestPerPageCeilingsFailClosed();
		TestAggregateRemainingAuthority();
		TestDeadlineAndRepresentability();
		TestOrderingAbortAndTerminalState();
		TestMaximumCounterBoundary();
		std::cout << "Scan resource accounting tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "Scan resource accounting tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
