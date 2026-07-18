#include "support/controlled_socket_service.hpp"
#include "support/loopback_curl_runtime.hpp"
#include "support/require.hpp"
#include "support/runtime_http_test_support.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>

namespace {

using duckdb_api_test::ControlledSocketMode;
using duckdb_api_test::ControlledSocketService;
using duckdb_api_test::ManualControl;
using duckdb_api_test::Require;
using duckdb_api_test::RequireExecutionError;

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
