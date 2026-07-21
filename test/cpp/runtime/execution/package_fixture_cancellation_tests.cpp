#include "package_fixture_execution.hpp"

#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using duckdb_api_test::Require;
using duckdb_api_test::RuntimeFixtureAuthorizationState;
using duckdb_api_test::RuntimeFixtureCancellationPoint;
using duckdb_api_test::RuntimeFixtureResponsePage;
using duckdb_api_test::RuntimeFixtureScenario;
using duckdb_api_test::RuntimeFixtureTranscript;
using duckdb_api_test::RuntimePackageFixtureExecutionService;

class ManualControl final : public duckdb_api::ExecutionControl {
public:
	ManualControl() : cancelled(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire);
	}

private:
	std::atomic<bool> cancelled;
};

RuntimeFixtureResponsePage Response(std::string body) {
	return {200, {}, std::move(body)};
}

std::string SearchPage() {
	return "{\"items\":[{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false}]}";
}

std::string GraphqlNode(const std::string &id) {
	return "{\"id\":\"" + id + "\",\"nameWithOwner\":\"duckdb/" + id +
	       "\",\"owner\":{\"login\":\"duckdb\"},\"stargazerCount\":42,\"primaryLanguage\":null,"
	       "\"isPrivate\":false,\"isArchived\":false,\"updatedAt\":\"2026-07-01T00:00:00Z\"}";
}

std::string GraphqlPage(const std::string &id, bool has_next, const char *cursor) {
	return "{\"data\":{\"viewer\":{\"repositories\":{\"nodes\":[" + GraphqlNode(id) +
	       "],\"pageInfo\":{\"hasNextPage\":" + (has_next ? "true" : "false") + ",\"endCursor\":" + cursor +
	       "}}}},\"errors\":[]}";
}

void RequireCancelled(const duckdb_api_test::RuntimeFixtureExecutionObservation &result,
                      RuntimeFixtureCancellationPoint point, uint64_t requests) {
	Require(!result.succeeded && result.cancellation_observed && result.cancellation_point == point &&
	            result.checkpoint_reached && !result.has_runtime_error && result.rows.empty() &&
	            result.request_count == requests && result.requests.size() == requests &&
	            result.stream_cancel_invoked && result.stream_close_invoked,
	        "closed Runtime cancellation scenario lost its checkpoint, cleanup, or all-or-nothing result");
}

void TestCancellationBeforeRequest() {
	ManualControl control;
	const auto result = RuntimePackageFixtureExecutionService().ExecuteScenario(
	    duckdb_api_test::BuildValidAnonymousPlanFixture(),
	    {RuntimeFixtureAuthorizationState::ANONYMOUS, {Response(SearchPage())}},
	    RuntimeFixtureScenario::CancelAt(RuntimeFixtureCancellationPoint::BEFORE_REQUEST), control);
	RequireCancelled(result, RuntimeFixtureCancellationPoint::BEFORE_REQUEST, 0);
	Require(!result.transport_observed, "before-request cancellation reached controlled transport");
}

void TestCancellationWhileTransportBlocked() {
	ManualControl control;
	const auto result = RuntimePackageFixtureExecutionService().ExecuteScenario(
	    duckdb_api_test::BuildValidAnonymousPlanFixture(),
	    {RuntimeFixtureAuthorizationState::ANONYMOUS, {Response(SearchPage())}},
	    RuntimeFixtureScenario::CancelAt(RuntimeFixtureCancellationPoint::TRANSPORT), control);
	RequireCancelled(result, RuntimeFixtureCancellationPoint::TRANSPORT, 1);
	Require(result.transport_observed, "transport cancellation did not reach the production transport boundary");
}

void TestCancellationAtDecoderCheckpoint() {
	ManualControl control;
	const auto result = RuntimePackageFixtureExecutionService().ExecuteScenario(
	    duckdb_api_test::BuildValidAnonymousPlanFixture(),
	    {RuntimeFixtureAuthorizationState::ANONYMOUS, {Response(SearchPage())}},
	    RuntimeFixtureScenario::CancelAt(RuntimeFixtureCancellationPoint::DECODE), control);
	RequireCancelled(result, RuntimeFixtureCancellationPoint::DECODE, 1);
	Require(result.transport_observed, "decode cancellation occurred before the controlled response returned");
}

void TestCancellationAtPageBoundary() {
	ManualControl control;
	const RuntimeFixtureTranscript transcript {
	    RuntimeFixtureAuthorizationState::BEARER_PRESENT,
	    {Response(GraphqlPage("R1", true, "\"next\"")), Response(GraphqlPage("R2", false, "null"))}};
	const auto result = RuntimePackageFixtureExecutionService().ExecuteScenario(
	    duckdb_api_test::BuildValidGraphqlScanPlanFixture("fixture_secret"), transcript,
	    RuntimeFixtureScenario::CancelAt(RuntimeFixtureCancellationPoint::PAGE_BOUNDARY), control);
	RequireCancelled(result, RuntimeFixtureCancellationPoint::PAGE_BOUNDARY, 1);
	Require(result.requests[0].body.find("\"cursor\":null") != std::string::npos,
	        "page-boundary cancellation lost the first canonical GraphQL request");
}

void TestCancellationDuringStreamClose() {
	ManualControl control;
	const auto result = RuntimePackageFixtureExecutionService().ExecuteScenario(
	    duckdb_api_test::BuildValidAnonymousPlanFixture(),
	    {RuntimeFixtureAuthorizationState::ANONYMOUS, {Response(SearchPage())}},
	    RuntimeFixtureScenario::CancelAt(RuntimeFixtureCancellationPoint::STREAM_CLOSE), control);
	RequireCancelled(result, RuntimeFixtureCancellationPoint::STREAM_CLOSE, 1);
	Require(result.post_close_exhaustion_observed,
	        "stream-close cancellation did not prove idempotent post-close exhaustion");
}

} // namespace

int main() {
	try {
		TestCancellationBeforeRequest();
		TestCancellationWhileTransportBlocked();
		TestCancellationAtDecoderCheckpoint();
		TestCancellationAtPageBoundary();
		TestCancellationDuringStreamClose();
		std::cout << "package fixture Runtime cancellation tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package fixture Runtime cancellation tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
