#include "duckdb_api/authorization.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "runtime/support/http_scan_executor_test_support.hpp"
#include "semantics/support/permanent_rest_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::BuildAmbiguousPredicateFallbackPlanFixture;
using duckdb_api_test::BuildValidAuthenticatedRepositoriesPlanFixture;
using duckdb_api_test::BuildValidPaginatedPlanFixture;
using duckdb_api_test::BuildValidPermanentRestScanPlanFixture;
using duckdb_api_test::BuildVisibilityPrivateCompleteResidualPlanFixture;
using duckdb_api_test::BuildVisibilityPrivatePlanFixture;
using duckdb_api_test::ControlledHttpResponse;
using duckdb_api_test::ControlledResponse;
using duckdb_api_test::ControlledTransportFailure;
using duckdb_api_test::GeneratedHttpBearerToken;
using duckdb_api_test::ManualHttpExecutionControl;
using duckdb_api_test::Require;

std::string Repository(uint64_t id, const std::string &name) {
	return std::string("[{\"id\":") + std::to_string(id) + ",\"full_name\":\"" + name +
	       "\",\"private\":false,\"fork\":false,\"archived\":false,\"visibility\":\"public\"}]";
}

std::string RepositoryPage(uint64_t first_id, uint64_t count) {
	std::string result = "[";
	for (uint64_t index = 0; index < count; index++) {
		if (index != 0) {
			result += ",";
		}
		const auto id = first_id + index;
		result += Repository(id, "repository-" + std::to_string(id)).substr(1);
		result.pop_back();
	}
	result += "]";
	return result;
}

std::string PrivateRepository(uint64_t id, const std::string &name) {
	return std::string("[{\"id\":") + std::to_string(id) + ",\"full_name\":\"" + name +
	       "\",\"private\":true,\"fork\":false,\"archived\":false,\"visibility\":\"private\"}]";
}

std::string NextLink(uint64_t page) {
	return std::string("<https://api.github.com/user/repos?per_page=100&page=") + std::to_string(page) +
	       ">; rel=\"next\"";
}

std::string SelectiveNextLink(uint64_t page, const std::string &visibility = "private") {
	return std::string("<https://api.github.com/user/repos?per_page=100&page=") + std::to_string(page) +
	       "&visibility=" + visibility + ">; rel=\"next\"";
}

std::string GenericPage(uint64_t id, const std::string &label) {
	return std::string("{\"records\":[{\"record_id\":") + std::to_string(id) + ",\"record_label\":\"" + label + "\"}]}";
}

std::string GenericNextLink(uint64_t page) {
	return std::string("<https://api.github.com/fixtures/linked-records?batch_size=3&cursor_page=") +
	       std::to_string(page) + ">; rel=next";
}

std::string PermanentRestPage(uint64_t id, const std::string &label_json) {
	return std::string("{\"payload\":{\"records\":[{\"identity\":{\"record_id\":") + std::to_string(id) +
	       "},\"attributes\":{\"label\":" + label_json + "}}]}}";
}

std::string PermanentRestNextLink(uint64_t page) {
	return std::string("<https://api.github.com/fixtures/materialized-records?view=summary&scope_name=") +
	       "north+america%2F%CE%B2&per_page=25&page=" + std::to_string(page) + ">; rel=next";
}

std::unique_ptr<duckdb_api::BatchStream> Open(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
                                              ManualHttpExecutionControl &control, uint64_t token_suffix) {
	const auto plan = BuildValidAuthenticatedRepositoriesPlanFixture("github_default");
	auto token = GeneratedHttpBearerToken(token_suffix);
	runtime->ExpectBearer("Bearer " + token);
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenSelective(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
              ManualHttpExecutionControl &control, uint64_t token_suffix) {
	const auto plan = BuildVisibilityPrivatePlanFixture("github_default");
	auto token = GeneratedHttpBearerToken(token_suffix);
	runtime->ExpectBearer("Bearer " + token);
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenSelectiveComplete(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
                      ManualHttpExecutionControl &control, uint64_t token_suffix) {
	const auto plan = BuildVisibilityPrivateCompleteResidualPlanFixture("github_default");
	auto token = GeneratedHttpBearerToken(token_suffix);
	runtime->ExpectBearer("Bearer " + token);
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenAmbiguous(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
              ManualHttpExecutionControl &control, uint64_t token_suffix) {
	const auto plan = BuildAmbiguousPredicateFallbackPlanFixture("github_default");
	auto token = GeneratedHttpBearerToken(token_suffix);
	runtime->ExpectBearer("Bearer " + token);
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenGeneric(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime, ManualHttpExecutionControl &control,
            uint64_t token_suffix) {
	const auto plan = BuildValidPaginatedPlanFixture("fixture_secret");
	auto token = GeneratedHttpBearerToken(token_suffix);
	runtime->ExpectBearer("Bearer " + token);
	return runtime->Executor()->OpenWithAuthorization(
	    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
}

std::unique_ptr<duckdb_api::BatchStream>
OpenPermanentRest(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
                  ManualHttpExecutionControl &control) {
	const auto plan = BuildValidPermanentRestScanPlanFixture();
	return runtime->Executor()->Open(plan, control);
}

void RequireFailure(const std::function<void()> &action, duckdb_api::ErrorStage stage, const std::string &field,
                    const std::string &forbidden = "") {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == stage && error.Field() == field, "pagination failure used the wrong safe diagnostic");
		Require(error.SafeMessage().size() <= 128, "pagination failure diagnostic was unbounded");
		Require(forbidden.empty() || error.SafeMessage().find(forbidden) == std::string::npos,
		        "pagination failure exposed remote or credential content");
	}
	Require(rejected, "pagination counterexample was accepted");
}

void TestSequentialBackpressureAndEmptyMiddlePage() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, Repository(1, "first"), {NextLink(2)}),
	                          ControlledResponse(200, "[]", {NextLink(3)}),
	                          ControlledResponse(200, Repository(3, "third"))});
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 701);
	Require(runtime->Observations().empty(), "paginated Open performed transport I/O");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.IsSchemaAligned() &&
	            batch.column_kinds == std::vector<duckdb_api::ValueKind>(
	                                      {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                                       duckdb_api::ValueKind::BOOLEAN, duckdb_api::ValueKind::BOOLEAN,
	                                       duckdb_api::ValueKind::BOOLEAN, duckdb_api::ValueKind::VARCHAR}) &&
	            batch.rows[0].values[1].varchar_value == "first",
	        "first repository page did not produce one typed nonempty batch");
	Require(runtime->Observations().size() == 1, "Runtime prefetched another page before the first page was consumed");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 3,
	        "same-pull traversal did not cross the empty nonterminal page");
	Require(runtime->Observations().size() == 3, "empty middle page request sequence drifted");
	Require(!stream->Next(control, batch) && batch.rows.empty(), "terminal page did not exhaust cleanly");
	const auto observations = runtime->Observations();
	for (std::size_t index = 0; index < observations.size(); index++) {
		Require(observations[index].target ==
		            "/user/repos?per_page=100&page=" + std::to_string(static_cast<uint64_t>(index + 1)),
		        "Runtime did not reconstruct the canonical increasing page target");
		Require(observations[index].headers.size() == 4 && observations[index].headers[3].first == "Authorization" &&
		            observations[index].headers[3].second == "<redacted>",
		        "safe request observation exposed or omitted bearer placement");
		Require(observations[index].max_metadata_bytes == duckdb_api::PAGINATION_MAX_HEADER_BYTES_PER_PAGE,
		        "transport did not receive the page-scoped normalized-metadata allowance");
	}
	Require(runtime->ConsumeBearerExpectation(3), "one scan did not use the same opaque capability on every page");
}

void TestGenericPaginatedRestUsesCopiedExecutableFacts() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, GenericPage(1, "first"), {GenericNextLink(2)}),
	                          ControlledResponse(200, GenericPage(2, "second"))});
	ManualHttpExecutionControl control;
	auto stream = OpenGeneric(runtime, control, 730);
	Require(runtime->Observations().empty(), "generic paginated REST Open performed transport I/O");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values.size() == 2 &&
	            batch.rows[0].values[0].bigint_value == 1 && batch.rows[0].values[1].varchar_value == "first",
	        "generic paginated REST profile did not decode its copied JSON-path schema");
	Require(stream->Next(control, batch) && batch.rows[0].values[0].bigint_value == 2 &&
	            batch.rows[0].values[1].varchar_value == "second" && !stream->Next(control, batch),
	        "generic paginated REST profile did not traverse its exact copied Link contract");
	const auto observations = runtime->Observations();
	Require(observations.size() == 2 &&
	            observations[0].target == "/fixtures/linked-records?batch_size=3&cursor_page=1" &&
	            observations[1].target == "/fixtures/linked-records?batch_size=3&cursor_page=2" &&
	            observations[0].headers.size() == 2 && observations[0].headers[0].first == "X-Connector-Fixture" &&
	            observations[0].headers[1].first == "Authorization" && runtime->ConsumeBearerExpectation(2),
	        "generic paginated REST execution drifted to the native repository identity");
}

void TestPermanentRestPlanExecutesCopiedRelationalFacts() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, PermanentRestPage(7, "\"first\""), {PermanentRestNextLink(3)}),
	                          ControlledResponse(200, PermanentRestPage(9, "null"))});
	ManualHttpExecutionControl control;
	auto stream = OpenPermanentRest(runtime, control);
	Require(runtime->Observations().empty(), "permanent REST Open performed transport I/O");

	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.IsSchemaAligned() && batch.rows.size() == 1 &&
	            batch.rows[0].values.size() == 2 && batch.rows[0].values[0].bigint_value == 7 &&
	            batch.rows[0].values[1].valid && batch.rows[0].values[1].varchar_value == "first",
	        "permanent REST plan did not decode its nested first page");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 9 &&
	            !batch.rows[0].values[1].valid,
	        "permanent REST plan did not preserve its nullable nested column");
	Require(!stream->Next(control, batch), "permanent REST plan did not exhaust after its terminal page");

	const auto observations = runtime->Observations();
	Require(
	    observations.size() == 2 &&
	        observations[0].target ==
	            "/fixtures/materialized-records?view=summary&scope_name=north+america%2F%CE%B2&per_page=25&page=1" &&
	        observations[1].target ==
	            "/fixtures/materialized-records?view=summary&scope_name=north+america%2F%CE%B2&per_page=25&page=3",
	    "permanent REST plan lost binding order, relation encoding, or page increment");
	for (const auto &observation : observations) {
		Require(observation.headers.size() == 1 && observation.headers[0].first == "X-Connector-Fixture" &&
		            observation.headers[0].second == "rest-materialization",
		        "permanent REST plan changed its anonymous fixed-header authority");
	}
}

void TestFullPageDrainsBeforeNextRequest() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, RepositoryPage(1, 100), {NextLink(2)}),
	                          ControlledResponse(200, Repository(101, "page-two"))});
	ManualHttpExecutionControl control;
	auto stream = Open(runtime, control, 709);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows.size() == 64 && runtime->Observations().size() == 1,
	        "first full-page pull did not return exactly 64 rows without prefetch");
	Require(stream->Next(control, batch) && batch.rows.size() == 36 && runtime->Observations().size() == 1,
	        "second full-page pull did not drain the remaining 36 rows before page 2");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 101 &&
	            runtime->Observations().size() == 2,
	        "page 2 did not wait until all 100 page-1 rows were transferred");
	Require(!stream->Next(control, batch) && runtime->ConsumeBearerExpectation(2),
	        "full-page backpressure scan did not exhaust with one authorization identity");
}

void TestTerminalEmptyPageAndAuthorityDenial() {
	ManualHttpExecutionControl control;
	const auto empty_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	empty_runtime->RespondSequence({ControlledResponse(200, "[]")});
	auto empty = Open(empty_runtime, control, 702);
	duckdb_api::TypedBatch batch;
	Require(!empty->Next(control, batch) && batch.rows.empty() && empty_runtime->Observations().size() == 1,
	        "terminal empty page did not return clean exhaustion without true+empty");
	Require(empty_runtime->ConsumeBearerExpectation(1), "terminal empty page lost its authorization evidence");

	const std::string hostile = "<https://credential-canary.invalid/user/repos?per_page=100&page=2>; rel=\"next\"";
	const auto hostile_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	hostile_runtime->RespondSequence({ControlledResponse(200, Repository(1, "private/canary"), {hostile})});
	auto denied = Open(hostile_runtime, control, 703);
	RequireFailure([&]() { (void)denied->Next(control, batch); }, duckdb_api::ErrorStage::POLICY, "pagination.next",
	               hostile);
	Require(hostile_runtime->Observations().size() == 1,
	        "authority-escaping Link caused another credential-bearing request");
	Require(hostile_runtime->ConsumeBearerExpectation(1), "denied Link disturbed first-page capability identity");

	const auto malformed_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	malformed_runtime->RespondSequence({ControlledResponse(
	    200, Repository(1, "first"), {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"/relative\""})});
	auto malformed = Open(malformed_runtime, control, 710);
	RequireFailure([&]() { (void)malformed->Next(control, batch); }, duckdb_api::ErrorStage::POLICY, "pagination.next");
	Require(malformed_runtime->Observations().size() == 1 && malformed_runtime->ConsumeBearerExpectation(1),
	        "invalid relation type became partial success or requested another page");
}

void TestSelectiveInputPersistsAcrossRequestsAndLinks() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, PrivateRepository(1, "first"), {SelectiveNextLink(2)}),
	                          ControlledResponse(200, PrivateRepository(2, "second"))});
	ManualHttpExecutionControl control;
	auto stream = OpenSelective(runtime, control, 720);
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.rows[0].values.size() == 6 &&
	            batch.rows[0].values[5].varchar_value == "private",
	        "selective repository page lost the required visibility field");
	Require(stream->Next(control, batch) && batch.rows[0].values[0].bigint_value == 2,
	        "selective repository scan did not advance to its second page");
	Require(!stream->Next(control, batch), "selective repository scan did not exhaust cleanly");
	const auto observations = runtime->Observations();
	Require(observations.size() == 2, "selective repository scan used the wrong request count");
	for (std::size_t index = 0; index < observations.size(); index++) {
		Require(observations[index].target ==
		            "/user/repos?per_page=100&page=" + std::to_string(static_cast<uint64_t>(index + 1)) +
		                "&visibility=private",
		        "selective repository request did not preserve its admitted conditional input");
	}
	Require(runtime->ConsumeBearerExpectation(2), "selective pages did not retain one authorization capability");
}

void TestStructuredClassificationsUseOnlyTypedRequestAuthority() {
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;

	const auto superset_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	superset_runtime->Respond(200, PrivateRepository(1, "superset"));
	auto superset = OpenSelective(superset_runtime, control, 727);
	Require(superset_runtime->Observations().empty(), "Superset plan Open performed transport I/O");
	Require(superset->Next(control, batch) && !superset->Next(control, batch) &&
	            superset_runtime->ConsumeBearerExpectation(1),
	        "Superset plan did not execute through the selected typed request profile");
	const auto superset_observation = superset_runtime->Observation();
	Require(superset_observation.target == "/user/repos?per_page=100&page=1&visibility=private",
	        "Superset classification changed or omitted the selected typed request");

	const auto superset_complete_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	superset_complete_runtime->Respond(200, PrivateRepository(2, "superset-complete"));
	auto superset_complete = OpenSelectiveComplete(superset_complete_runtime, control, 729);
	Require(superset_complete_runtime->Observations().empty(),
	        "Superset complete-residual plan Open performed transport I/O");
	Require(superset_complete->Next(control, batch) && !superset_complete->Next(control, batch) &&
	            superset_complete_runtime->ConsumeBearerExpectation(1),
	        "Superset complete-residual plan did not execute through the selected typed request profile");
	const auto superset_complete_observation = superset_complete_runtime->Observation();
	Require(superset_complete_observation.target == superset_observation.target &&
	            superset_complete_observation.max_header_bytes == superset_observation.max_header_bytes &&
	            superset_complete_observation.max_response_bytes == superset_observation.max_response_bytes &&
	            superset_complete_observation.max_decompressed_bytes == superset_observation.max_decompressed_bytes &&
	            superset_complete_observation.max_metadata_bytes == superset_observation.max_metadata_bytes,
	        "the complete DuckDB residual changed Superset request or resource authority");

	const auto ambiguous_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	ambiguous_runtime->Respond(200, Repository(2, "ambiguous"));
	auto ambiguous = OpenAmbiguous(ambiguous_runtime, control, 728);
	Require(ambiguous_runtime->Observations().empty(), "ambiguous fallback Open performed transport I/O");
	Require(ambiguous->Next(control, batch) && !ambiguous->Next(control, batch) &&
	            ambiguous_runtime->ConsumeBearerExpectation(1),
	        "ambiguous fallback did not execute through the unrestricted typed profile");
	const auto ambiguous_observation = ambiguous_runtime->Observation();
	Require(ambiguous_observation.target == "/user/repos?per_page=100&page=1",
	        "Ambiguous classification acquired conditional request authority");
	Require(superset_observation.max_header_bytes == ambiguous_observation.max_header_bytes &&
	            superset_observation.max_response_bytes == ambiguous_observation.max_response_bytes &&
	            superset_observation.max_decompressed_bytes == ambiguous_observation.max_decompressed_bytes &&
	            superset_observation.max_metadata_bytes == ambiguous_observation.max_metadata_bytes,
	        "classification changed page resource authority");
}

void RequireSelectiveLinkDenied(const std::string &link, uint64_t token_suffix) {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	runtime->RespondSequence({ControlledResponse(200, PrivateRepository(1, "private"), {link})});
	ManualHttpExecutionControl control;
	auto stream = OpenSelective(runtime, control, token_suffix);
	duckdb_api::TypedBatch batch;
	RequireFailure([&]() { (void)stream->Next(control, batch); }, duckdb_api::ErrorStage::POLICY, "pagination.next");
	Require(runtime->Observations().size() == 1 && runtime->ConsumeBearerExpectation(1),
	        "rejected selective Link caused another credential-bearing request");
}

void TestSelectiveLinkMustPreserveConditionalInput() {
	RequireSelectiveLinkDenied(NextLink(2), 721);
	RequireSelectiveLinkDenied(SelectiveNextLink(2, "public"), 722);
	RequireSelectiveLinkDenied(
	    "<https://api.github.com/user/repos?per_page=100&page=2&visibility=private&visibility=private>; rel=next", 723);
	RequireSelectiveLinkDenied(
	    "<https://api.github.com/user/repos?per_page=100&page=2&visibility=private&sort=id>; rel=next", 724);
}

void TestSelectiveProfileDoesNotLeakAcrossStreamLifecycles() {
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;

	runtime->Respond(200, PrivateRepository(1, "private"));
	auto selective = OpenSelective(runtime, control, 725);
	Require(selective->Next(control, batch) && !selective->Next(control, batch) && runtime->ConsumeBearerExpectation(1),
	        "selective stream did not release its first admitted profile cleanly");

	runtime->Respond(200, Repository(2, "public"));
	auto base = Open(runtime, control, 726);
	Require(base->Next(control, batch) && !base->Next(control, batch) && runtime->ConsumeBearerExpectation(1),
	        "following unselective stream did not complete independently");
	const auto observations = runtime->Observations();
	Require(observations.size() == 2 &&
	            observations[0].target == "/user/repos?per_page=100&page=1&visibility=private" &&
	            observations[1].target == "/user/repos?per_page=100&page=1",
	        "a completed selective profile leaked conditional authority into a later stream");
}

void TestLateFailuresAreTerminalAndRedacted() {
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;
	const auto status_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	status_runtime->RespondSequence({ControlledResponse(200, Repository(1, "delivered"), {NextLink(2)}),
	                                 ControlledResponse(429, "private status body")});
	auto status = Open(status_runtime, control, 704);
	Require(status->Next(control, batch), "late-status fixture did not first deliver a committed batch");
	RequireFailure([&]() { (void)status->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS, "",
	               "private status body");
	RequireFailure([&]() { (void)status->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS, "",
	               "private status body");
	Require(status_runtime->Observations().size() == 2,
	        "late status terminal state replayed its credential-bearing request");
	Require(status_runtime->ConsumeBearerExpectation(2), "late status page used another authorization identity");

	const auto partial_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	partial_runtime->RespondSequence({ControlledResponse(206, Repository(1, "partial"))});
	auto partial = Open(partial_runtime, control, 711);
	RequireFailure([&]() { (void)partial->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS, "");
	Require(partial_runtime->Observations().size() == 1 && partial_runtime->ConsumeBearerExpectation(1),
	        "first-page partial HTTP success became a complete relation or replayed");

	const auto late_partial_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	late_partial_runtime->RespondSequence({ControlledResponse(200, Repository(1, "delivered"), {NextLink(2)}),
	                                       ControlledResponse(206, Repository(2, "partial"))});
	auto late_partial = Open(late_partial_runtime, control, 712);
	Require(late_partial->Next(control, batch), "late-partial fixture did not deliver its first committed batch");
	RequireFailure([&]() { (void)late_partial->Next(control, batch); }, duckdb_api::ErrorStage::HTTP_STATUS, "");
	Require(late_partial_runtime->Observations().size() == 2 && late_partial_runtime->ConsumeBearerExpectation(2),
	        "late partial HTTP success became clean exhaustion or requested another page");

	const auto transport_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	const std::string diagnostic = "dependency token_canary https://secret.invalid";
	transport_runtime->RespondSequence(
	    {ControlledResponse(200, Repository(1, "delivered"), {NextLink(2)}), ControlledTransportFailure(diagnostic)});
	auto transport = Open(transport_runtime, control, 705);
	Require(transport->Next(control, batch), "late-transport fixture did not deliver its first batch");
	RequireFailure([&]() { (void)transport->Next(control, batch); }, duckdb_api::ErrorStage::TRANSPORT, "", diagnostic);
	Require(transport_runtime->Observations().size() == 2 && transport_runtime->ConsumeBearerExpectation(2),
	        "late transport failure retried or crossed authorization identity");
}

void TestCancellationCloseAndAggregatePageCeiling() {
	ManualHttpExecutionControl control;
	duckdb_api::TypedBatch batch;
	const auto close_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	close_runtime->RespondSequence({ControlledResponse(200, PrivateRepository(1, "first"), {SelectiveNextLink(2)}),
	                                ControlledResponse(200, Repository(2, "unrequested"))});
	auto closed = OpenSelectiveComplete(close_runtime, control, 706);
	Require(closed->Next(control, batch), "early-close fixture did not deliver its first page");
	closed->Close();
	closed->Close();
	Require(!closed->Next(control, batch) && close_runtime->Observations().size() == 1,
	        "early close allowed a later page request");
	Require(close_runtime->ConsumeBearerExpectation(1), "early-close request used the wrong capability");

	const auto cancel_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	cancel_runtime->RespondSequence({ControlledResponse(200, Repository(1, "first"), {NextLink(2)}),
	                                 ControlledResponse(200, Repository(2, "unrequested"))});
	auto cancelled = OpenAmbiguous(cancel_runtime, control, 707);
	Require(cancelled->Next(control, batch), "cancellation fixture did not deliver its first page");
	cancelled->Cancel();
	bool saw_cancel = false;
	try {
		(void)cancelled->Next(control, batch);
	} catch (const duckdb_api::ExecutionCancelled &) {
		saw_cancel = true;
	}
	Require(saw_cancel && cancel_runtime->Observations().size() == 1,
	        "between-page cancellation allowed another request or became exhaustion");
	Require(cancel_runtime->ConsumeBearerExpectation(1), "cancelled scan used the wrong capability");

	std::vector<ControlledHttpResponse> pages;
	for (uint64_t page = 1; page <= 32; page++) {
		pages.push_back(ControlledResponse(200, "[]", {NextLink(page + 1)}));
	}
	const auto budget_runtime = duckdb_api_test::BuildControlledHttpRuntime();
	budget_runtime->RespondSequence(std::move(pages));
	auto bounded = OpenAmbiguous(budget_runtime, control, 708);
	RequireFailure([&]() { (void)bounded->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE, "pages");
	Require(budget_runtime->Observations().size() == 32 && budget_runtime->ConsumeBearerExpectation(32),
	        "advertised-next-at-ceiling silently exhausted, retried, or exceeded 32 requests");
}

} // namespace

int main() {
	try {
		TestSequentialBackpressureAndEmptyMiddlePage();
		TestGenericPaginatedRestUsesCopiedExecutableFacts();
		TestPermanentRestPlanExecutesCopiedRelationalFacts();
		TestFullPageDrainsBeforeNextRequest();
		TestTerminalEmptyPageAndAuthorityDenial();
		TestSelectiveInputPersistsAcrossRequestsAndLinks();
		TestStructuredClassificationsUseOnlyTypedRequestAuthority();
		TestSelectiveLinkMustPreserveConditionalInput();
		TestSelectiveProfileDoesNotLeakAcrossStreamLifecycles();
		TestLateFailuresAreTerminalAndRedacted();
		TestCancellationCloseAndAggregatePageCeiling();
		std::cout << "HTTP scan pagination tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "HTTP scan pagination tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
