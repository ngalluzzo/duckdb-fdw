#include "duckdb_api/authorization.hpp"
#include "duckdb_api/connector.hpp"
#include "support/controlled_socket_service.hpp"
#include "support/loopback_curl_runtime.hpp"
#include "support/require.hpp"
#include "support/runtime_http_test_support.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		duckdb_api_test::ControlledSocketService service(duckdb_api_test::ControlledSocketMode::PAGINATED_REPOSITORIES);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		const auto connector = duckdb_api::BuildNativeGithubConnector();
		const auto plan = duckdb_api_test::BuildAuthenticatedRepositoriesRuntimePlan(connector);
		duckdb_api_test::ManualControl control;
		auto token = duckdb_api_test::RuntimeCurlBearerToken(801);
		const auto authorization_header = "Authorization: Bearer " + token + "\r\n";
		auto stream = runtime->Executor()->OpenWithAuthorization(
		    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		duckdb_api_test::Require(service.ConnectionCount() == 0, "paginated curl Open performed socket I/O");

		duckdb_api::TypedBatch batch;
		duckdb_api_test::Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
		                             batch.rows[0].values[0].bigint_value == 1 && service.ConnectionCount() == 1,
		                         "real curl page 1 did not produce its typed row without prefetch");
		duckdb_api_test::Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
		                             batch.rows[0].values[0].bigint_value == 3 && service.ConnectionCount() == 3,
		                         "real curl did not cross empty page 2 and produce page 3 in the same pull");
		duckdb_api_test::Require(!stream->Next(control, batch), "real curl pagination did not exhaust cleanly");
		duckdb_api_test::Require(service.WaitForRequestCount(3, std::chrono::seconds(2)),
		                         "controlled socket did not receive all three page requests");
		const auto requests = service.Requests();
		duckdb_api_test::Require(requests.size() == 3, "real curl pagination request count drifted");
		for (std::size_t index = 0; index < requests.size(); index++) {
			const auto target =
			    "GET /user/repos?per_page=100&page=" + std::to_string(static_cast<uint64_t>(index + 1)) +
			    " HTTP/1.1\r\n";
			duckdb_api_test::Require(requests[index].find(target) == 0 &&
			                             requests[index].find(authorization_header) != std::string::npos,
			                         "real curl did not emit the canonical increasing target with one bearer header");
		}
		duckdb_api_test::Require(runtime->Observation().request_count == 3 &&
		                             runtime->Observation().socket_policy_checks == 3 && service.ConnectionCount() == 3,
		                         "real curl pagination was not one fresh policy-checked request per page");
		std::cout << "curl HTTP pagination tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl HTTP pagination tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
