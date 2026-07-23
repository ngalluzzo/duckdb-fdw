#include "duckdb_api/internal/runtime/transport/curl_response_accumulator.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/pagination/link_pagination.hpp"
#include "runtime/support/controlled_socket_service.hpp"
#include "runtime/support/private_curl_probe.hpp"
#include "support/require.hpp"
#include "runtime/support/runtime_http_test_support.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"

#include <csignal>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

uint64_t RetainedBytes(const std::vector<std::string> &values) {
	uint64_t result = static_cast<uint64_t>(values.capacity()) * sizeof(std::string);
	for (const auto &value : values) {
		const auto begin = reinterpret_cast<std::uintptr_t>(&value);
		const auto end = begin + sizeof(value);
		const auto data = reinterpret_cast<std::uintptr_t>(value.data());
		if (data < begin || data >= end) {
			result += static_cast<uint64_t>(value.capacity()) + 1;
		}
	}
	return result;
}

duckdb_api_test::PrivateCurlProbeOptions Options(uint16_t port, uint64_t *policy_checks) {
	return {"http://127.0.0.1:" + std::to_string(port) + "/search/users?q=duckdb+in%3Alogin&per_page=3",
	        "http",
	        "http",
	        "127.0.0.1",
	        port,
	        "",
	        "",
	        duckdb_api_test::PrivateCurlSocketPolicy::ALLOW_LOOPBACK_PORT,
	        2000,
	        policy_checks,
	        nullptr,
	        nullptr};
}

void TestPhysicalLinkCaptureOrderAndNormalization() {
	duckdb_api_test::ControlledSocketService service(duckdb_api_test::ControlledSocketMode::LINK_SUCCESS);
	duckdb_api_test::ManualControl control;
	uint64_t checks = 0;
	const auto result = duckdb_api_test::PerformPrivateCurlProbe(Options(service.Port(), &checks), control);
	const std::string previous = "<https://api.github.com/user/repos?per_page=100&page=1>; rel=prev";
	const std::string next = "<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"next\"";
	duckdb_api_test::Require(result.response.status == 200 && result.response.metadata.link_field_values.size() == 2 &&
	                             result.response.metadata.link_field_values[0] == previous &&
	                             result.response.metadata.link_field_values[1] == next,
	                         "curl did not capture physical Link values in receipt order with outer OWS removed");
	duckdb_api_test::Require(result.response.metadata.retained_bytes ==
	                             RetainedBytes(result.response.metadata.link_field_values),
	                         "curl Link metadata retained-byte accounting drifted");
	duckdb_api_test::Require(checks == 1 && service.ConnectionCount() == 1,
	                         "Link capture changed one-attempt socket policy");
}

void TestMetadataCapacityGrowthIsCharged() {
	duckdb_api_test::ControlledSocketService service(duckdb_api_test::ControlledSocketMode::MANY_LINK_SUCCESS);
	duckdb_api_test::ManualControl control;
	uint64_t checks = 0;
	const auto result = duckdb_api_test::PerformPrivateCurlProbe(Options(service.Port(), &checks), control);
	duckdb_api_test::Require(result.response.metadata.link_field_values.size() == 40 &&
	                             result.response.metadata.retained_bytes ==
	                                 RetainedBytes(result.response.metadata.link_field_values),
	                         "Link vector or string capacity was not charged as retained metadata");
}

void TestInterimMetadataResetAndFailureCleanup() {
	duckdb_api_test::ManualControl control;
	uint64_t checks = 0;
	duckdb_api_test::ControlledSocketService interim(duckdb_api_test::ControlledSocketMode::INTERIM_LINK_SUCCESS);
	const auto result = duckdb_api_test::PerformPrivateCurlProbe(Options(interim.Port(), &checks), control);
	const std::string terminal = "<https://api.github.com/user/repos?per_page=100&page=2>; rel=next";
	duckdb_api_test::Require(result.response.metadata.link_field_values.size() == 1 &&
	                             result.response.metadata.link_field_values[0] == terminal &&
	                             result.response.metadata.link_field_values[0].find("credential-canary") ==
	                                 std::string::npos,
	                         "interim Link metadata survived terminal response-section reset");
	duckdb_api_test::Require(result.response.metadata.link_field_values.capacity() == 1,
	                         "terminal Link fixture did not isolate post-interim retained capacity");

	checks = 0;
	duckdb_api_test::ControlledSocketService failed(duckdb_api_test::ControlledSocketMode::LINK_STATUS);
	const auto failed_result = duckdb_api_test::PerformPrivateCurlProbe(Options(failed.Port(), &checks), control);
	duckdb_api_test::Require(failed_result.response.status == 503 && failed_result.response.body.empty() &&
	                             failed_result.response.metadata.link_field_values.empty() &&
	                             failed_result.response.metadata.link_field_values.capacity() == 0 &&
	                             failed_result.response.metadata.retained_bytes == 0,
	                         "non-success curl response retained body or Link metadata");
}

void TestTrailerCannotGrantContinuationAuthority() {
	duckdb_api_test::ManualControl control;
	uint64_t checks = 0;
	duckdb_api_test::ControlledSocketService trailer(duckdb_api_test::ControlledSocketMode::TRAILER_LINK_SUCCESS);
	const auto result = duckdb_api_test::PerformPrivateCurlProbe(Options(trailer.Port(), &checks), control);
	duckdb_api_test::Require(result.response.status == 200 && result.response.metadata.link_field_values.empty(),
	                         "HTTP trailer Link metadata granted continuation authority");
	duckdb_api_test::Require(checks == 1 && trailer.ConnectionCount() == 1,
	                         "trailer exclusion changed one-attempt socket policy");
}

void FeedHeader(duckdb_api::internal::CurlTransferState &state, const std::string &line) {
	auto mutable_line = line;
	duckdb_api_test::Require(duckdb_api::internal::ReadCurlHeader(&mutable_line[0], 1, mutable_line.size(), &state) ==
	                             mutable_line.size(),
	                         "curl accumulator rejected a bounded regression header");
}

void TestTargetedFoldAndProtocolRoleOverlap() {
	duckdb_api_test::ManualControl control;
	const duckdb_api::internal::CurlTransferProfile profile {"",      "",      nullptr, nullptr, nullptr,
	                                                         nullptr, nullptr, nullptr, nullptr, nullptr};
	{
		const duckdb_api::internal::HttpLimits limits {
		    0, 4096, 4096, 4096, 4096, std::chrono::steady_clock::now() + std::chrono::seconds(1), {"x-reset"}, false};
		duckdb_api::internal::CurlTransferState state(control, limits, profile);
		FeedHeader(state, "HTTP/1.1 429 Too Many Requests\r\n");
		FeedHeader(state, "X-Reset: 10\r\n");
		FeedHeader(state, " 0\r\n");
		duckdb_api_test::Require(state.rate_limit_fields.size() == 1 && state.rate_limit_fields[0].name == "x-reset" &&
		                             state.rate_limit_fields[0].value == "10 0" && state.metadata_bytes != 0,
		                         "folded targeted guidance discarded bytes and could become valid early guidance");
	}
	{
		const duckdb_api::internal::HttpLimits limits {0,
		                                               4096,
		                                               4096,
		                                               4096,
		                                               4096,
		                                               std::chrono::steady_clock::now() + std::chrono::seconds(1),
		                                               {"transfer-encoding", "content-encoding", "link"},
		                                               false};
		duckdb_api::internal::CurlTransferState state(control, limits, profile);
		const std::string link = "<https://api.github.com/user/repos?page=2>; rel=next";
		FeedHeader(state, "HTTP/1.1 429 Too Many Requests\r\n");
		FeedHeader(state, "Transfer-Encoding: chunked\r\n");
		FeedHeader(state, "Content-Encoding: identity\r\n");
		FeedHeader(state, "Link: " + link + "\r\n");
		duckdb_api_test::Require(
		    state.transfer_chunked && !state.transfer_encoding_unsupported && !state.content_encoded &&
		        state.rate_limit_fields.size() == 3 && state.rate_limit_fields[0].value == "chunked" &&
		        state.rate_limit_fields[1].value == "identity" && state.rate_limit_fields[2].value == link &&
		        state.link_field_values == std::vector<std::string> {link},
		    "targeted protocol fields were not retained in every authoritative typed role");
	}
	{
		const duckdb_api::internal::HttpLimits limits {
		    0, 4096, 4096, 4096, 4096, std::chrono::steady_clock::now() + std::chrono::seconds(1), {"link"}, false};
		duckdb_api::internal::CurlTransferState state(control, limits, profile);
		const std::string target = "<https://api.github.com/user/repos?per_page=100&page=2>";
		const std::string unfolded = target + " ; rel=next";
		FeedHeader(state, "HTTP/1.1 200 OK\r\n");
		FeedHeader(state, "Link: " + target + "\r\n");
		FeedHeader(state, " ; rel=next\r\n");
		duckdb_api_test::Require(state.rate_limit_fields.size() == 1 && state.rate_limit_fields[0].value == unfolded &&
		                             state.link_field_values == std::vector<std::string> {unfolded},
		                         "folded dual-role Link metadata diverged between rate-limit and pagination copies");

		const duckdb_api::internal::HttpExecutionProfile execution_profile {
		    duckdb_api::PlannedUrlScheme::HTTPS,
		    "api.github.com",
		    443,
		    false,
		    false,
		    false,
		    duckdb_api::MAX_EXECUTION_MILLISECONDS,
		    100,
		    duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP,
		    duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN,
		    duckdb_api::RETRY_MAX_DELAY_MILLISECONDS,
		    duckdb_api::RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN};
		auto admitted = duckdb_api::internal::TryAdmitPaginatedRestPlan(
		    duckdb_api_test::BuildValidAuthenticatedRepositoriesPlanFixture("fixture_secret"), execution_profile);
		duckdb_api_test::Require(admitted != nullptr, "folded Link regression fixture did not pass admission");
		duckdb_api::internal::LinkPaginationState pagination(*admitted);
		const auto transition = pagination.Advance(state.link_field_values);
		duckdb_api_test::Require(transition.has_next && transition.next_page == 2,
		                         "folded dual-role Link metadata silently truncated pagination");
	}
}

} // namespace

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		TestPhysicalLinkCaptureOrderAndNormalization();
		TestMetadataCapacityGrowthIsCharged();
		TestInterimMetadataResetAndFailureCleanup();
		TestTrailerCannotGrantContinuationAuthority();
		TestTargetedFoldAndProtocolRoleOverlap();
		std::cout << "curl Link metadata tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl Link metadata tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
