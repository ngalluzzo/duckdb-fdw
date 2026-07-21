#include "package_fixture_execution.hpp"

#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"
#include "runtime/support/package_fixture_json_variant_internal.hpp"
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
	        duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE};
}

bool IsLink(const std::string &name) {
	if (name.size() != 4) {
		return false;
	}
	for (std::size_t index = 0; index < 4; index++) {
		const auto byte = name[index];
		const auto folded = byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') : byte;
		if (folded != "link"[index]) {
			return false;
		}
	}
	return true;
}

void RemoveLink(RuntimeFixtureResponsePage &page) {
	std::vector<RuntimeFixtureResponseHeader> retained;
	for (auto &header : page.headers) {
		if (!IsLink(header.name)) {
			retained.push_back(std::move(header));
		}
	}
	page.headers = std::move(retained);
}

std::string RestNextLink(const duckdb_api::internal::AdmittedPaginatedRestRequestProfile &profile, uint64_t next_page) {
	std::string result = "<" + profile.Scheme() + "://" + profile.Host();
	if (profile.Port() != 443) {
		result += ":" + std::to_string(profile.Port());
	}
	result += profile.Path() + "?";
	bool first = true;
	for (const auto &field : profile.QueryParameters()) {
		if (!first) {
			result.push_back('&');
		}
		first = false;
		result += field.name + "=" +
		          (field.name == profile.PageNumberParameter() ? std::to_string(next_page) : field.encoded_value);
	}
	if (profile.ConditionalInput() ==
	    duckdb_api::internal::AdmittedPaginatedRestConditionalInput::LEGACY_VISIBILITY_PRIVATE) {
		if (!first) {
			result.push_back('&');
		}
		result += "visibility=private";
	}
	return result + ">; rel=\"next\"";
}

void SetLink(RuntimeFixtureResponsePage &page, std::string value) {
	RemoveLink(page);
	page.headers.push_back({"Link", std::move(value)});
}

RuntimeFixtureResponsePage SetTerminal(const duckdb_api::ScanPlan &plan, RuntimeFixtureResponsePage page,
                                       duckdb_api::ExecutionControl &control) {
	if (plan.Operation().Protocol() == duckdb_api::PlannedProtocol::REST) {
		RemoveLink(page);
		return page;
	}
	const auto &cursor = plan.Pagination().GraphqlCursor();
	page.body = internal::ReplaceRuntimeFixturePath(page.body, cursor.has_next_page.segments, "false", false, control);
	page.body = internal::ReplaceRuntimeFixturePath(page.body, cursor.end_cursor.segments, "null", false, control);
	return page;
}

void SetContinuation(const duckdb_api::ScanPlan &plan, RuntimeFixtureResponsePage &page, uint64_t page_index,
                     duckdb_api::ExecutionControl &control) {
	if (plan.Operation().Protocol() == duckdb_api::PlannedProtocol::REST) {
		auto admitted = duckdb_api::internal::TryAdmitPaginatedRestPlan(plan, PublicFixtureProfile());
		if (!admitted) {
			throw std::invalid_argument("closed resource variant REST plan was not admitted");
		}
		const auto next = admitted->FirstPage() + page_index * admitted->PageIncrement();
		SetLink(page, RestNextLink(*admitted, next));
		return;
	}
	const auto &cursor = plan.Pagination().GraphqlCursor();
	page.body = internal::ReplaceRuntimeFixturePath(page.body, cursor.has_next_page.segments, "true", false, control);
	page.body = internal::ReplaceRuntimeFixturePath(page.body, cursor.end_cursor.segments,
	                                                "\"runtime-resource-page-" + std::to_string(page_index) + "\"",
	                                                false, control);
}

std::vector<uint64_t> SplitUnits(uint64_t total, uint64_t per_page) {
	if (total == 0 || per_page == 0) {
		throw std::invalid_argument("closed resource variant received a zero admitted limit");
	}
	std::vector<uint64_t> result;
	while (total != 0) {
		const auto next = std::min(total, per_page);
		result.push_back(next);
		total -= next;
	}
	return result;
}

RuntimeFixtureTranscript BuildPageSequence(const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &base,
                                           const internal::RuntimeFixtureJsonShape &shape,
                                           const std::vector<uint64_t> &record_counts,
                                           duckdb_api::ExecutionControl &control) {
	if (base.pages.empty()) {
		throw std::invalid_argument("closed resource variant requires a base response page");
	}
	RuntimeFixtureTranscript result {base.authorization, {}};
	for (std::size_t index = 0; index < record_counts.size(); index++) {
		auto page = base.pages[0];
		if (shape.records_are_array) {
			page.body = internal::RepeatFirstRuntimeFixtureRecord(page.body, shape, record_counts[index], control);
		}
		if (index + 1 == record_counts.size()) {
			page = SetTerminal(plan, std::move(page), control);
		} else {
			SetContinuation(plan, page, static_cast<uint64_t>(index + 1), control);
		}
		result.pages.push_back(std::move(page));
	}
	return result;
}

std::string RecordFailureField(const duckdb_api::ScanPlan &plan, const internal::RuntimeFixtureJsonShape &shape) {
	if (plan.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL) {
		std::string result;
		for (const auto &segment : shape.records_path) {
			if (!result.empty()) {
				result.push_back('.');
			}
			result += segment;
		}
		return result;
	}
	return shape.records_path.empty() ? std::string() : shape.records_path.back();
}

void RequireExecutionBoundary(const RuntimeFixtureExecutionObservation &execution, uint64_t requests) {
	if (!execution.succeeded || execution.has_runtime_error || execution.cancellation_observed ||
	    execution.request_count != requests || execution.requests.size() != requests || !execution.transport_observed ||
	    !execution.stream_close_invoked) {
		throw std::logic_error("closed Runtime resource boundary did not succeed through the exact request sequence");
	}
}

void RequireExecutionRejection(const RuntimeFixtureExecutionObservation &execution, const std::string &field,
                               uint64_t requests) {
	if (execution.succeeded || execution.cancellation_observed || !execution.has_runtime_error ||
	    execution.runtime_error_stage != duckdb_api::ErrorStage::RESOURCE || execution.runtime_error_field != field ||
	    !execution.rows.empty() || execution.request_count != requests || execution.requests.size() != requests ||
	    !execution.transport_observed || !execution.stream_close_invoked) {
		throw std::logic_error(
		    "closed Runtime resource rejection lost its stage, field, requests, or all-or-nothing result");
	}
}

RuntimeFixtureVariantObservation DirectRootObjectRecordVariant(const duckdb_api::ScanPlan &plan,
                                                               RuntimeFixtureBoundaryVariant boundary) {
	// A successful root-object response always has cardinality one, so no JSON
	// transcript can carry the plan's synthetic cardinality boundary. Exercise
	// the same production accounting primitive directly and keep this exception
	// confined to Runtime's non-installed closed service.
	const auto &budget = plan.Budgets();
	duckdb_api::internal::ScanResourceProfile profile {
	    {1, budget.header_bytes, budget.response_bytes, budget.decompressed_bytes, budget.decoded_records,
	     budget.decoded_memory_bytes, 1, 0},
	    {1, 1, budget.header_bytes, budget.response_bytes, budget.decompressed_bytes, budget.decoded_records,
	     budget.decoded_memory_bytes, budget.wall_milliseconds, 1, 0}};
	duckdb_api::internal::ScanResourceAccounting accounting(profile);
	accounting.BeginPage(std::chrono::steady_clock::now());
	accounting.CommitTransport({0, 0, 0});
	auto execution = internal::NewRuntimeFixtureObservation(plan, RuntimeFixtureCancellationPoint::NONE);
	if (boundary == RuntimeFixtureBoundaryVariant::BOUNDARY) {
		accounting.CommitDecodedPage({budget.decoded_records, 0});
		accounting.CompletePage(false, std::chrono::steady_clock::now());
		execution.succeeded = true;
		return {std::move(execution), RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED, budget.decoded_records,
		        budget.decoded_records};
	}
	try {
		accounting.CommitDecodedPage({budget.decoded_records + 1, 0});
	} catch (const duckdb_api::internal::ScanResourceError &error) {
		if (error.Field() != "decoded_records") {
			throw;
		}
		execution.has_runtime_error = true;
		execution.runtime_error_stage = duckdb_api::ErrorStage::RESOURCE;
		execution.runtime_error_field = error.Field();
		return {std::move(execution), RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED, budget.decoded_records + 1,
		        budget.decoded_records};
	}
	throw std::logic_error("root-object record one-over variant did not fail authoritative accounting");
}

} // namespace

RuntimeFixtureVariantObservation RuntimePackageFixtureExecutionService::ExecuteRelationResourceVariant(
    const duckdb_api::ScanPlan &plan, const RuntimeFixtureTranscript &transcript,
    RuntimeFixtureRelationResourceField field, RuntimeFixtureBoundaryVariant boundary,
    duckdb_api::ExecutionControl &control) const {
	internal::ValidateRuntimeFixtureTranscript(transcript);
	const auto shape = internal::AdmitRuntimeFixtureJsonShape(plan);
	const bool one_over = boundary == RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED;
	const auto outcome =
	    one_over ? RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED : RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED;

	if (field == RuntimeFixtureRelationResourceField::EXTRACTED_STRING_BYTES) {
		std::size_t ordinal = shape.columns.size();
		for (std::size_t index = 0; index < shape.columns.size(); index++) {
			if (shape.columns[index].kind == duckdb_api::ValueKind::VARCHAR) {
				ordinal = index;
				break;
			}
		}
		if (ordinal == shape.columns.size()) {
			throw std::invalid_argument("extracted-string resource variant requires an admitted VARCHAR column");
		}
		const auto variant = one_over ? RuntimeFixtureColumnVariant::VARCHAR_STRING_BUDGET_ONE_OVER_REJECTED
		                              : RuntimeFixtureColumnVariant::VARCHAR_STRING_BUDGET_BOUNDARY;
		return ExecuteColumnVariant(plan, transcript, {ordinal, variant}, control);
	}

	const bool per_scan = field == RuntimeFixtureRelationResourceField::RESPONSE_BYTES_PER_SCAN ||
	                      field == RuntimeFixtureRelationResourceField::RECORDS_PER_SCAN;
	const bool response_bytes = field == RuntimeFixtureRelationResourceField::RESPONSE_BYTES_PER_PAGE ||
	                            field == RuntimeFixtureRelationResourceField::RESPONSE_BYTES_PER_SCAN;
	const bool paginated = plan.Pagination().Strategy() != duckdb_api::PlannedPaginationStrategy::DISABLED;
	const auto &page = plan.Budgets();
	const uint64_t limit = per_scan && paginated ? (response_bytes ? plan.Pagination().ScanBudgets().response_bytes
	                                                               : plan.Pagination().ScanBudgets().decoded_records)
	                                             : (response_bytes ? page.response_bytes : page.decoded_records);
	const auto per_page = response_bytes ? page.response_bytes : page.decoded_records;
	if (!response_bytes && !shape.records_are_array) {
		return DirectRootObjectRecordVariant(plan, boundary);
	}

	uint64_t attempted = limit + static_cast<uint64_t>(one_over);
	auto units = per_scan ? SplitUnits(limit, per_page) : std::vector<uint64_t> {attempted};
	if (per_scan && one_over) {
		units.back()++;
	}
	RuntimeFixtureTranscript derived;
	internal::RuntimeFixtureResponseAccountingOverrides overrides;
	if (response_bytes) {
		std::vector<uint64_t> empty_counts(units.size(), 0);
		derived = BuildPageSequence(plan, transcript, shape, empty_counts, control);
		overrides.wire_response_bytes = units;
		if (!per_scan && one_over) {
			overrides.wire_response_bytes[0] = attempted;
		}
	} else {
		derived = BuildPageSequence(plan, transcript, shape, units, control);
	}
	auto execution = internal::RunRuntimeFixtureScenario(plan, derived, RuntimeFixtureScenario::Standard(), control,
	                                                     true, response_bytes ? &overrides : nullptr);
	if (!one_over) {
		RequireExecutionBoundary(execution, static_cast<uint64_t>(derived.pages.size()));
		return {std::move(execution), outcome, limit, limit};
	}
	const auto failure_field = response_bytes ? std::string("response_bytes") : RecordFailureField(plan, shape);
	RequireExecutionRejection(execution, failure_field, static_cast<uint64_t>(derived.pages.size()));
	return {std::move(execution), outcome, attempted, limit};
}

} // namespace duckdb_api_test
