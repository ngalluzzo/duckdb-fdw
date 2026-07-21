#include "package_fixture_execution.hpp"

#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"
#include "duckdb_api/internal/runtime/transport/graphql_request_body.hpp"
#include "runtime/support/package_fixture_observation_internal.hpp"

#include <algorithm>
#include <chrono>
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
	        duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE};
}

duckdb_api::internal::ScanResourceProfile
AccountingProfile(const duckdb_api::internal::AdmittedGraphqlRequestProfile &admitted) {
	const auto &page = admitted.PageBudgets();
	const auto &scan = admitted.ScanBudgets();
	return {{page.request_attempts, page.header_bytes, page.response_bytes, page.decompressed_bytes,
	         page.decoded_records, page.decoded_memory_bytes, page.concurrency, page.serialized_request_body_bytes},
	        {scan.request_attempts, scan.pages, scan.header_bytes, scan.response_bytes, scan.decompressed_bytes,
	         scan.decoded_records, scan.decoded_memory_bytes, scan.wall_milliseconds, scan.concurrency,
	         scan.serialized_request_body_bytes}};
}

std::vector<uint64_t> SplitUnits(uint64_t total, uint64_t per_request) {
	std::vector<uint64_t> result;
	while (total != 0) {
		const auto next = std::min(total, per_request);
		result.push_back(next);
		total -= next;
	}
	if (result.empty()) {
		throw std::invalid_argument("GraphQL body variant received a zero admitted limit");
	}
	return result;
}

void ValidateCanonicalBody(const duckdb_api::internal::AdmittedGraphqlRequestProfile &admitted,
                           const std::string *cursor) {
	const auto request = duckdb_api::internal::BuildAdmittedGraphqlRequest(admitted, cursor);
	if (request.body.empty() || !duckdb_api::internal::IsAdmittedGraphqlBody(admitted, request.body) ||
	    static_cast<uint64_t>(request.body.size()) > admitted.PageBudgets().serialized_request_body_bytes) {
		throw std::logic_error("GraphQL body variant lost canonical production serialization or admitted authority");
	}
}

} // namespace

RuntimeFixtureVariantObservation RuntimePackageFixtureExecutionService::ExecuteGraphqlBodyResourceVariant(
    const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &transcript,
    RuntimeFixtureGraphqlBodyResourceField field, RuntimeFixtureBoundaryVariant boundary,
    duckdb_api::ExecutionControl &control) const {
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
	const auto page_limit = admitted->PageBudgets().serialized_request_body_bytes;
	const auto limit = per_scan ? admitted->ScanBudgets().serialized_request_body_bytes : page_limit;
	auto units = per_scan ? SplitUnits(limit, page_limit) : std::vector<uint64_t> {limit};
	if (one_over) {
		units.back()++;
	}

	duckdb_api::internal::ScanResourceAccounting accounting(AccountingProfile(*admitted));
	const auto now = std::chrono::steady_clock::now();
	auto execution = internal::NewRuntimeFixtureObservation(plan, RuntimeFixtureCancellationPoint::NONE);
	const std::string cursor = "runtime-body-accounting-cursor";
	try {
		for (std::size_t index = 0; index < units.size(); index++) {
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
			accounting.BeginPage(now);
			ValidateCanonicalBody(*admitted, index == 0 ? nullptr : &cursor);
			accounting.CommitRequestBody(units[index]);
			accounting.CommitTransport({0, 0, 0});
			accounting.CommitDecodedPage({0, 0});
			const bool has_next = index + 1 != units.size();
			accounting.CompletePage(has_next, now);
		}
	} catch (const duckdb_api::internal::ScanResourceError &error) {
		if (!one_over || error.Field() != "request_body_bytes") {
			throw;
		}
		execution.has_runtime_error = true;
		execution.runtime_error_stage = duckdb_api::ErrorStage::RESOURCE;
		execution.runtime_error_field = error.Field();
		return {std::move(execution), RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED, limit + 1, limit};
	}
	if (one_over) {
		throw std::logic_error("GraphQL body one-over variant did not fail production scan accounting");
	}
	if (accounting.Counters().serialized_request_body_bytes != limit) {
		throw std::logic_error("GraphQL body boundary did not debit the exact admitted byte count");
	}
	execution.succeeded = true;
	return {std::move(execution), RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED, limit, limit};
}

} // namespace duckdb_api_test
