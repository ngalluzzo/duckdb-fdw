#include "duckdb_api/authorization.hpp"
#include "duckdb_api/internal/runtime/transport/curl_http_transport.hpp"
#include "runtime/support/controlled_socket_service.hpp"
#include "runtime/support/loopback_curl_runtime.hpp"
#include "support/require.hpp"
#include "runtime/support/runtime_http_test_support.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using duckdb_api_test::ControlledSocketMode;
using duckdb_api_test::ControlledSocketService;
using duckdb_api_test::ManualControl;
using duckdb_api_test::Require;
using duckdb_api_test::RequireExecutionError;

duckdb_api::internal::HttpRequest InstalledRepositoryRequest(std::string target) {
	duckdb_api::internal::HttpRequest request;
	request.method = "GET";
	request.scheme = "https";
	request.host = "api.github.com";
	request.port = 443;
	request.target = std::move(target);
	request.headers = {{"Accept", "application/vnd.github+json"},
	                   {"User-Agent", "duckdb-api/0.6.0"},
	                   {"X-GitHub-Api-Version", "2022-11-28"},
	                   {"Authorization", "Bearer fixture-token"}};
	return request;
}

void TestInstalledRepositoryRequestPolicy() {
	using duckdb_api::internal::ClassifyInstalledHttpRequest;
	using duckdb_api::internal::InstalledHttpRequestKind;
	Require(ClassifyInstalledHttpRequest(InstalledRepositoryRequest("/user/repos?per_page=100&page=1")) ==
	            InstalledHttpRequestKind::AUTHENTICATED_REPOSITORIES,
	        "installed transport rejected the admitted unselective target");
	Require(ClassifyInstalledHttpRequest(
	            InstalledRepositoryRequest("/user/repos?per_page=100&page=1&visibility=private")) ==
	            InstalledHttpRequestKind::AUTHENTICATED_REPOSITORIES,
	        "installed transport rejected the admitted selective target");
	const std::string denied[] = {"/user/repos?per_page=100&page=1&visibility=public",
	                              "/user/repos?per_page=100&page=1&visibility=private&visibility=private",
	                              "/user/repos?per_page=100&page=1&visibility=private&sort=id",
	                              "/user/repos?per_page=100&page=1&sort=id",
	                              "/user/repos?per_page=100&page=1&visibility=private2"};
	for (const auto &target : denied) {
		Require(ClassifyInstalledHttpRequest(InstalledRepositoryRequest(target)) ==
		            InstalledHttpRequestKind::UNSUPPORTED,
		        "installed transport accepted a target outside the admitted field set");
	}
	auto body = InstalledRepositoryRequest("/user/repos?per_page=100&page=1");
	body.body = "request-body-canary";
	body.content_type = "application/json";
	Require(ClassifyInstalledHttpRequest(body) == InstalledHttpRequestKind::UNSUPPORTED,
	        "installed REST transport accepted body-bearing request authority");
	auto content_type = InstalledRepositoryRequest("/user/repos?per_page=100&page=1");
	content_type.content_type = "application/json";
	Require(ClassifyInstalledHttpRequest(content_type) == InstalledHttpRequestKind::UNSUPPORTED,
	        "installed REST transport accepted content type without a body");
}

void TestExactAnonymousCurlRequest() {
	ControlledSocketService service(ControlledSocketMode::SUCCESS);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
	ManualControl control;
	auto stream = runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(), control);
	Require(runtime->Observation().request_count == 0 && service.ConnectionCount() == 0, "Open performed socket work");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.IsSchemaAligned() && batch.rows.size() == 2,
	        "real curl path did not decode the first bounded batch");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 33,
	        "real curl path did not decode the second bounded batch");
	Require(!stream->Next(control, batch), "real curl path did not exhaust cleanly");
	Require(service.WaitForRequest(std::chrono::seconds(2)), "controlled service did not receive a request");
	const auto wire = service.Request();
	Require(wire.find("GET /search/users?q=duckdb+in%3Alogin&per_page=3 HTTP/1.1\r\n") == 0,
	        "curl request target or HTTP version drifted");
	Require(wire.find("\r\nHost: 127.0.0.1:" + std::to_string(service.Port()) + "\r\n") != std::string::npos,
	        "controlled curl authority drifted");
	Require(wire.find("\r\nAccept: application/vnd.github+json\r\n") != std::string::npos &&
	            wire.find("\r\nUser-Agent: duckdb-api/0.6.0\r\n") != std::string::npos &&
	            wire.find("\r\nX-GitHub-Api-Version: 2022-11-28\r\n") != std::string::npos,
	        "curl fixed request headers drifted");
	Require(wire.find("Authorization:") == std::string::npos, "anonymous curl request emitted credential authority");
	const auto observation = runtime->Observation();
	Require(observation.request_count == 1 && observation.socket_policy_checks == 1 && service.ConnectionCount() == 1,
	        "curl did not perform exactly one policy-checked request attempt");
}

void TestAuthenticatedCurlRequestIsolationAndFailures() {
	std::string prior_token;
	for (uint64_t index = 0; index < 2; index++) {
		ControlledSocketService service(index == 0 ? ControlledSocketMode::AUTHENTICATED_SET_COOKIE
		                                           : ControlledSocketMode::AUTHENTICATED_SUCCESS);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto token = duckdb_api_test::RuntimeCurlBearerToken(index + 1);
		const auto expected_header = "Authorization: Bearer " + token + "\r\n";
		auto stream = runtime->Executor()->OpenWithAuthorization(
		    duckdb_api_test::BuildAuthenticatedRuntimePlan(),
		    duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		Require(service.ConnectionCount() == 0, "authorized Open performed socket work");
		duckdb_api::TypedBatch batch;
		Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 44,
		        "authenticated curl response did not decode one root object");
		Require(service.WaitForRequest(std::chrono::seconds(2)), "authenticated service saw no request");
		const auto wire = service.Request();
		Require(wire.find("GET /user HTTP/1.1\r\n") == 0 && wire.find(expected_header) != std::string::npos,
		        "authenticated curl request identity or bearer header drifted");
		Require(wire.find("\r\nCookie:") == std::string::npos &&
		            (prior_token.empty() || wire.find(prior_token) == std::string::npos),
		        "cookie or prior bearer authority crossed into a fresh scan");
		prior_token = expected_header.substr(std::string("Authorization: Bearer ").size());
	}

	struct StatusCase {
		ControlledSocketMode mode;
		duckdb_api::ErrorStage stage;
		const char *response_canary;
	};
	const StatusCase statuses[] = {{ControlledSocketMode::AUTHENTICATION_STATUS, duckdb_api::ErrorStage::AUTHENTICATION,
	                                "AUTHENTICATION_RESPONSE_CANARY"},
	                               {ControlledSocketMode::AUTHORIZATION_STATUS, duckdb_api::ErrorStage::AUTHORIZATION,
	                                "AUTHORIZATION_RESPONSE_CANARY"}};
	for (std::size_t index = 0; index < sizeof(statuses) / sizeof(statuses[0]); index++) {
		ControlledSocketService service(statuses[index].mode);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto token = duckdb_api_test::RuntimeCurlBearerToken(index + 20);
		const auto credential_canary = token;
		auto stream = runtime->Executor()->OpenWithAuthorization(
		    duckdb_api_test::BuildAuthenticatedRuntimePlan(),
		    duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		duckdb_api::TypedBatch batch;
		RequireExecutionError([&]() { stream->Next(control, batch); }, statuses[index].stage, credential_canary,
		                      statuses[index].response_canary);
		Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
		        "authenticated status failure did not remain one attempt");
	}

	ControlledSocketService redirect_sink(ControlledSocketMode::SUCCESS);
	ControlledSocketService redirect_source(ControlledSocketMode::REDIRECT, redirect_sink.Port());
	const auto redirect_runtime = duckdb_api_test::BuildLoopbackCurlRuntime(redirect_source.Port());
	ManualControl redirect_control;
	auto redirect_token = duckdb_api_test::RuntimeCurlBearerToken(40);
	const auto redirect_canary = redirect_token;
	const auto expected_redirect_header = "Authorization: Bearer " + redirect_token + "\r\n";
	auto redirect_stream = redirect_runtime->Executor()->OpenWithAuthorization(
	    duckdb_api_test::BuildAuthenticatedRuntimePlan(),
	    duckdb_api::ScanAuthorization::GithubUserBearer(std::move(redirect_token)), redirect_control);
	duckdb_api::TypedBatch redirect_batch;
	RequireExecutionError([&]() { redirect_stream->Next(redirect_control, redirect_batch); },
	                      duckdb_api::ErrorStage::HTTP_STATUS, redirect_canary);
	Require(redirect_source.WaitForRequest(std::chrono::seconds(2)) &&
	            redirect_source.Request().find(expected_redirect_header) != std::string::npos &&
	            redirect_source.ConnectionCount() == 1 && redirect_sink.ConnectionCount() == 0,
	        "redirect handling forwarded bearer authority or skipped the original request");
}

void TestAnonymousStatusRedirectAndTransportFailures() {
	struct FailureCase {
		ControlledSocketMode mode;
		duckdb_api::ErrorStage stage;
		const char *forbidden;
	};
	const FailureCase cases[] = {
	    {ControlledSocketMode::STATUS, duckdb_api::ErrorStage::HTTP_STATUS, "SECRET_STATUS_BODY"},
	    {ControlledSocketMode::REDIRECT, duckdb_api::ErrorStage::HTTP_STATUS, "127.0.0.1"},
	    {ControlledSocketMode::DISCONNECT, duckdb_api::ErrorStage::TRANSPORT, "127.0.0.1"}};
	for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		ControlledSocketService service(cases[index].mode);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto stream = runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(), control);
		duckdb_api::TypedBatch batch;
		RequireExecutionError([&]() { stream->Next(control, batch); }, cases[index].stage, cases[index].forbidden);
		RequireExecutionError([&]() { stream->Next(control, batch); }, cases[index].stage, cases[index].forbidden);
		Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
		        "real curl failure performed more than one attempt");
	}
}

} // namespace

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		TestInstalledRepositoryRequestPolicy();
		TestExactAnonymousCurlRequest();
		TestAuthenticatedCurlRequestIsolationAndFailures();
		TestAnonymousStatusRedirectAndTransportFailures();
		std::cout << "curl HTTP request tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl HTTP request tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
