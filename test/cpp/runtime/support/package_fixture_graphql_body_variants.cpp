#include "package_fixture_execution.hpp"

#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"
#include "duckdb_api/internal/runtime/transport/graphql_request_body.hpp"
#include "runtime/support/package_fixture_observation_internal.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {
namespace {

duckdb_api::internal::HttpExecutionProfile PublicFixtureProfile() {
	return {duckdb_api::PlannedUrlScheme::HTTPS,
	        "",
	        0,
	        false,
	        false,
	        false,
	        duckdb_api::PAGINATION_MAX_EXECUTION_MILLISECONDS,
	        duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE,
	        duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP,
	        duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN,
	        duckdb_api::RETRY_MAX_DELAY_MILLISECONDS,
	        duckdb_api::RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN};
}

duckdb_api::internal::ScanResourceProfile
AccountingProfile(const duckdb_api::internal::AdmittedGraphqlRequestProfile &admitted) {
	const auto &page = admitted.PageBudgets();
	const auto &scan = admitted.ScanBudgets();
	return {{page.request_attempts, page.header_bytes, page.response_bytes, page.decompressed_bytes,
	         page.decoded_records, page.decoded_memory_bytes, page.concurrency, page.serialized_request_body_bytes},
	        {scan.request_attempts, scan.pages, scan.header_bytes, scan.response_bytes, scan.decompressed_bytes,
	         scan.decoded_records, scan.decoded_memory_bytes, scan.wall_milliseconds, scan.concurrency,
	         scan.serialized_request_body_bytes, 0, 0, 0, 0}};
}

void ValidateBodySelector(RuntimeFixtureGraphqlBodyResourceField field, RuntimeFixtureBoundaryVariant boundary) {
	switch (boundary) {
	case RuntimeFixtureBoundaryVariant::BOUNDARY:
	case RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED:
		break;
	default:
		throw std::invalid_argument("unknown closed Runtime GraphQL body boundary variant");
	}
	switch (field) {
	case RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_REQUEST:
	case RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_SCAN:
		return;
	default:
		throw std::invalid_argument("unknown closed Runtime GraphQL body resource field");
	}
}

uint64_t AddChecked(uint64_t left, uint64_t right) {
	if (right > std::numeric_limits<uint64_t>::max() - left) {
		throw std::logic_error("actual GraphQL request-body observation overflowed");
	}
	return left + right;
}

uint64_t ValidateExecutorBaseline(const RuntimeFixtureExecutionObservation &execution,
                                  const duckdb_api::internal::AdmittedGraphqlRequestProfile &admitted, bool per_scan) {
	if (!execution.succeeded || execution.has_runtime_error || execution.cancellation_observed ||
	    !execution.transport_observed || execution.request_count == 0 ||
	    execution.requests.size() != execution.request_count || !execution.stream_close_invoked) {
		throw std::logic_error("GraphQL body resource baseline lost real executor or lifecycle evidence");
	}
	uint64_t total = 0;
	uint64_t maximum = 0;
	for (const auto &request : execution.requests) {
		const auto bytes = static_cast<uint64_t>(request.body.size());
		if (request.method != "POST" || request.content_type != "application/json" || request.body.empty() ||
		    !duckdb_api::internal::IsAdmittedGraphqlBody(admitted, request.body) ||
		    bytes > admitted.PageBudgets().serialized_request_body_bytes) {
			throw std::logic_error("GraphQL body resource baseline lost canonical production serialization");
		}
		total = AddChecked(total, bytes);
		maximum = std::max(maximum, bytes);
	}
	if (total > admitted.ScanBudgets().serialized_request_body_bytes) {
		throw std::logic_error("GraphQL body resource baseline exceeded admitted scan authority");
	}
	return per_scan ? total : maximum;
}

void ProveAccountingThreshold(const duckdb_api::internal::AdmittedGraphqlRequestProfile &admitted, bool per_scan,
                              bool one_over, duckdb_api::ExecutionControl &control) {
	const auto page_limit = admitted.PageBudgets().serialized_request_body_bytes;
	const auto limit = per_scan ? admitted.ScanBudgets().serialized_request_body_bytes : page_limit;
	if (limit == std::numeric_limits<uint64_t>::max()) {
		throw std::invalid_argument("GraphQL body resource limit cannot form an isolated one-over probe");
	}
	auto profile = AccountingProfile(admitted);
	if (per_scan) {
		// Make page authority strictly wider than scan authority. The active
		// allowance can then only have come from the selected scan counter.
		profile.page.serialized_request_body_bytes = limit + 1;
		profile.scan.serialized_request_body_bytes = limit;
	} else {
		// Keep scan authority wide enough for page+1 so an over-limit commit can
		// only be rejected by the selected page ceiling.
		profile.page.serialized_request_body_bytes = limit;
		profile.scan.serialized_request_body_bytes = limit + 1;
	}
	duckdb_api::internal::ScanResourceAccounting accounting(profile);
	const auto now = std::chrono::steady_clock::now();
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
	const auto allowance = accounting.BeginPage(now);
	if (allowance.serialized_request_body_bytes != limit || accounting.Counters().pages != 1 ||
	    accounting.Counters().request_attempts != 1 ||
	    accounting.State() != duckdb_api::internal::ScanResourceState::REQUEST_ACTIVE) {
		throw std::logic_error("GraphQL body accounting probe did not isolate the selected ledger scope");
	}
	const auto attempted = limit + static_cast<uint64_t>(one_over);
	try {
		accounting.CommitRequestBody(attempted);
	} catch (const duckdb_api::internal::ScanResourceError &error) {
		if (one_over && error.Field() == "request_body_bytes" &&
		    accounting.State() == duckdb_api::internal::ScanResourceState::FAILED &&
		    accounting.Counters().serialized_request_body_bytes == 0 && accounting.Counters().active_requests == 0) {
			return;
		}
		throw;
	}
	if (one_over) {
		throw std::logic_error("GraphQL body one-over did not fail production scan accounting");
	}
	accounting.CommitTransport({0, 0, 0});
	accounting.CommitDecodedPage({0, 0});
	accounting.CompletePage(false, now);
	if (accounting.Counters().serialized_request_body_bytes != limit ||
	    accounting.State() != duckdb_api::internal::ScanResourceState::EXHAUSTED) {
		throw std::logic_error("GraphQL body boundary did not debit the exact admitted ledger limit");
	}
}

} // namespace

RuntimeFixtureVariantObservation RuntimePackageFixtureExecutionService::ExecuteGraphqlBodyResourceVariant(
    const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &transcript,
    RuntimeFixtureGraphqlBodyResourceField field, RuntimeFixtureBoundaryVariant boundary,
    duckdb_api::ExecutionControl &control) const {
	ValidateBodySelector(field, boundary);
	internal::ValidateRuntimeFixtureTranscript(transcript);
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
	auto admitted = duckdb_api::internal::TryAdmitGraphqlPlan(plan, PublicFixtureProfile());
	if (!admitted) {
		throw std::invalid_argument("GraphQL body resource variant requires an admitted immutable GraphQL plan");
	}
	const bool per_scan = field == RuntimeFixtureGraphqlBodyResourceField::SERIALIZED_BODY_BYTES_PER_SCAN;
	const bool one_over = boundary == RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED;
	const auto limit = per_scan ? admitted->ScanBudgets().serialized_request_body_bytes
	                            : admitted->PageBudgets().serialized_request_body_bytes;
	auto execution =
	    internal::RunRuntimeFixtureScenario(plan, transcript, RuntimeFixtureScenario::Standard(), control, true);
	const auto actual_units = ValidateExecutorBaseline(execution, *admitted, per_scan);
	if (!one_over && actual_units == limit) {
		return {std::move(execution),
		        RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED,
		        RuntimeFixtureVariantEvidencePath::EXECUTOR,
		        actual_units,
		        0,
		        limit};
	}
	ProveAccountingThreshold(*admitted, per_scan, one_over, control);
	return {std::move(execution),
	        one_over ? RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED
	                 : RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED,
	        per_scan ? RuntimeFixtureVariantEvidencePath::EXECUTOR_PLUS_SCAN_ACCOUNTING
	                 : RuntimeFixtureVariantEvidencePath::EXECUTOR_PLUS_PAGE_ACCOUNTING,
	        actual_units,
	        limit + static_cast<uint64_t>(one_over),
	        limit};
}

} // namespace duckdb_api_test
