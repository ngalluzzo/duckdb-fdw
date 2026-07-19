#include "runtime/support/controlled_socket_service.hpp"
#include "runtime/support/private_curl_probe.hpp"
#include "support/require.hpp"
#include "runtime/support/runtime_http_test_support.hpp"

#include <csignal>
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
	        policy_checks};
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

} // namespace

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		TestPhysicalLinkCaptureOrderAndNormalization();
		TestMetadataCapacityGrowthIsCharged();
		TestInterimMetadataResetAndFailureCleanup();
		TestTrailerCannotGrantContinuationAuthority();
		std::cout << "curl Link metadata tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl Link metadata tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
