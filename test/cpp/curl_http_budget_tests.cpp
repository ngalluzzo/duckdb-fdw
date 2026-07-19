#include "support/controlled_socket_service.hpp"
#include "support/loopback_curl_runtime.hpp"
#include "support/private_curl_probe.hpp"
#include "support/require.hpp"
#include "support/runtime_http_test_support.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using duckdb_api_test::ControlledSocketMode;
using duckdb_api_test::ControlledSocketService;
using duckdb_api_test::ManualControl;
using duckdb_api_test::Require;
using duckdb_api_test::RequireExecutionError;

uint64_t CompleteRequestHeaderBlockBytes(const std::string &request) {
	const auto request_line_end = request.find("\r\n");
	const auto block_end = request.find("\r\n\r\n");
	Require(request_line_end != std::string::npos && block_end != std::string::npos &&
	            block_end >= request_line_end + 2,
	        "controlled curl request did not contain a complete header block");
	return static_cast<uint64_t>((block_end + 4) - (request_line_end + 2));
}

uint64_t FixedProjectHeaderBytesWithoutToken() {
	static const uint64_t line_framing = 4;
	return (sizeof("Accept") - 1) + (sizeof("application/vnd.github+json") - 1) + line_framing +
	       (sizeof("User-Agent") - 1) + (sizeof("duckdb-api/0.4.0") - 1) + line_framing +
	       (sizeof("X-GitHub-Api-Version") - 1) + (sizeof("2022-11-28") - 1) + line_framing +
	       (sizeof("Authorization") - 1) + (sizeof("Bearer ") - 1) + line_framing;
}

void TestOutboundHeaderBoundaries() {
	const auto limit = duckdb_api::ScanAuthorization::GithubUserBearerTokenByteLimit();
	Require(limit == 8 * 1024, "real-curl bearer-token boundary drifted");
	{
		ControlledSocketService service(ControlledSocketMode::AUTHENTICATED_SUCCESS);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto token = std::string(static_cast<std::size_t>(limit), 'e');
		const auto expected_authorization = "Authorization: Bearer " + token + "\r\n";
		auto stream = runtime->Executor()->OpenWithAuthorization(
		    duckdb_api_test::BuildAuthenticatedRuntimePlan(runtime->Connector()),
		    duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		duckdb_api::TypedBatch batch;
		Require(stream->Next(control, batch), "real curl rejected the exact-limit bearer token");
		Require(service.WaitForRequest(std::chrono::seconds(2)),
		        "exact-limit bearer request did not reach the controlled service");
		const auto request = service.Request();
		Require(request.find(expected_authorization) != std::string::npos,
		        "real curl truncated or replaced the exact-limit bearer value");
		Require(CompleteRequestHeaderBlockBytes(request) <= duckdb_api::HOST_MAX_HEADER_BYTES,
		        "complete real-curl request header block exceeded the native 16 KiB envelope");
	}
	{
		ControlledSocketService service(ControlledSocketMode::AUTHENTICATED_SUCCESS);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto token = std::string(static_cast<std::size_t>(limit + 1), 'o');
		bool rejected = false;
		try {
			(void)runtime->Executor()->OpenWithAuthorization(
			    duckdb_api_test::BuildAuthenticatedRuntimePlan(runtime->Connector()),
			    duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = true;
			Require(error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "header_bytes",
			        "one-byte-over real-curl case used the wrong safe resource diagnostic");
		}
		Require(rejected, "one-byte-over real-curl bearer token was accepted");
		Require(runtime->Observation().request_count == 0 && service.ConnectionCount() == 0,
		        "one-byte-over bearer token reached curl transport or the socket service");
	}
	{
		ControlledSocketService service(ControlledSocketMode::AUTHENTICATED_SUCCESS);
		ManualControl control;
		uint64_t policy_checks = 0;
		const duckdb_api_test::PrivateCurlProbeOptions options {
		    "http://127.0.0.1:" + std::to_string(service.Port()) + "/user",
		    "http",
		    "http",
		    "127.0.0.1",
		    service.Port(),
		    "",
		    "",
		    duckdb_api_test::PrivateCurlSocketPolicy::ALLOW_LOOPBACK_PORT,
		    1000,
		    &policy_checks};
		const auto fixed_header_bytes = FixedProjectHeaderBytesWithoutToken();
		Require(fixed_header_bytes == 125, "fixed project request-header byte accounting drifted");
		auto oversized_project_header =
		    std::string(static_cast<std::size_t>(duckdb_api::HOST_MAX_HEADER_BYTES - fixed_header_bytes + 1), 'a');
		bool rejected = false;
		try {
			(void)duckdb_api_test::PerformPrivateAuthorizedCurlProbe(options, std::move(oversized_project_header),
			                                                         control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = true;
			Require(error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "header_bytes",
			        "aggregate request-header overflow used the wrong safe resource diagnostic");
		}
		Require(rejected, "aggregate request-header overflow was accepted");
		Require(policy_checks == 0 && service.ConnectionCount() == 0,
		        "aggregate request-header overflow reached socket policy or transport I/O");
	}
}

void TestDecodeAndWireBudgetFailures() {
	struct FailureCase {
		ControlledSocketMode mode;
		duckdb_api::ErrorStage stage;
		const char *forbidden;
	};
	const FailureCase cases[] = {{ControlledSocketMode::MALFORMED, duckdb_api::ErrorStage::DECODE, "SECRET_MALFORMED"},
	                             {ControlledSocketMode::OVERSIZED_HEADER, duckdb_api::ErrorStage::RESOURCE, ""},
	                             {ControlledSocketMode::OVERSIZED_RESPONSE, duckdb_api::ErrorStage::RESOURCE, ""}};
	for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		ControlledSocketService service(cases[index].mode);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto stream = runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(runtime->Connector()), control);
		duckdb_api::TypedBatch batch;
		RequireExecutionError([&]() { stream->Next(control, batch); }, cases[index].stage, cases[index].forbidden);
		Require(!stream->Next(control, batch), "budget or decode failure was replayed");
		Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
		        "budget or decode failure performed more than one request");
	}
}

void TestCompressedWireBodyAtExactDecompressedLimit() {
	ControlledSocketService service(ControlledSocketMode::GZIP_EXACT_DECOMPRESSED_LIMIT);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
	ManualControl control;
	auto stream = runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(runtime->Connector()), control);
	duckdb_api::TypedBatch batch;
	Require(service.ResponseBodyBytes() < duckdb_api::HOST_MAX_RESPONSE_BYTES,
	        "gzip exact-limit fixture did not stay below the wire-byte ceiling");
	Require(stream->Next(control, batch) && batch.IsSchemaAligned() && batch.rows.size() == 1,
	        "gzip body at the exact decompressed ceiling was rejected");
	Require(batch.rows[0].values[0].bigint_value == 11 && !stream->Next(control, batch),
	        "exact-limit gzip response decoded unexpected rows");
	Require(service.ConnectionCount() == 1, "exact-limit gzip response replayed the request");
}

void TestCompressedWireBodyOverDecompressedLimit() {
	ControlledSocketService service(ControlledSocketMode::GZIP_OVER_DECOMPRESSED_LIMIT);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
	ManualControl control;
	auto stream = runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(runtime->Connector()), control);
	duckdb_api::TypedBatch batch;
	Require(service.ResponseBodyBytes() < duckdb_api::HOST_MAX_RESPONSE_BYTES,
	        "gzip over-limit fixture did not stay below the wire-byte ceiling");
	bool rejected = false;
	try {
		stream->Next(control, batch);
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "decompressed_bytes",
		        "gzip +1 counterexample did not exhaust the decompressed-byte ceiling");
	}
	Require(rejected, "gzip +1 counterexample was accepted");
	Require(!stream->Next(control, batch), "over-limit gzip response was replayed");
	Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
	        "over-limit gzip response performed more than one request");
}

} // namespace

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		TestOutboundHeaderBoundaries();
		TestDecodeAndWireBudgetFailures();
		TestCompressedWireBodyAtExactDecompressedLimit();
		TestCompressedWireBodyOverDecompressedLimit();
		std::cout << "curl HTTP budget tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl HTTP budget tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
