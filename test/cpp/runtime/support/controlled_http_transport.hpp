#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {

enum class ControlledHttpMode { RESPONSE, TRANSPORT_FAILURE, BLOCK_UNTIL_CANCEL, BEARER_RESPONSE_BARRIER };

struct ControlledHttpResponse {
	uint32_t status;
	std::string body;
	std::vector<std::string> link_field_values;
	uint64_t header_bytes;
	bool transport_failure;
	std::string dependency_diagnostic;
};

ControlledHttpResponse ControlledResponse(uint32_t status, std::string body,
                                          std::vector<std::string> link_field_values = {});
ControlledHttpResponse ControlledTransportFailure(std::string dependency_diagnostic);

struct ControlledRequestObservation {
	uint64_t request_count;
	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string target;
	std::vector<std::pair<std::string, std::string>> headers;
	uint64_t max_header_bytes;
	uint64_t max_response_bytes;
	uint64_t max_decompressed_bytes;
	uint64_t max_metadata_bytes;
};

// Test-only Remote Runtime composition. Query tests consume only this helper
// and the public executor contract; the implementation translation unit alone
// sees the private transport seam. It is never linked into an installed target.
class ControlledHttpRuntime {
public:
	struct State;

	std::shared_ptr<const duckdb_api::ScanExecutor> Executor() const;

	void Respond(uint32_t status, std::string body);
	// Installs a bounded response sequence keyed by request order. Exhausting the
	// sequence fails transport; responses are never replayed or reused.
	void RespondSequence(std::vector<ControlledHttpResponse> responses);
	// Isolated credential-comparison input. Exact values never enter ordinary
	// observations; ConsumeBearerExpectation releases the expected header.
	void ExpectBearer(std::string exact_authorization_header);
	bool ConsumeBearerExpectation(uint64_t expected_request_count);
	void RespondWithBearerBarrier(std::string first_header, std::string first_body, std::string second_header,
	                              std::string second_body);
	void FailWithUnknownTransportDiagnostic(std::string diagnostic);
	void BlockUntilCancelled();
	bool WaitForRequestCount(uint64_t count, std::chrono::milliseconds timeout);
	void ReleaseBearerBarrier();
	ControlledRequestObservation Observation() const;
	std::vector<ControlledRequestObservation> Observations() const;

private:
	friend std::shared_ptr<ControlledHttpRuntime> BuildControlledHttpRuntime(uint64_t max_wall_milliseconds,
	                                                                         uint64_t max_decoded_records);
	ControlledHttpRuntime(std::shared_ptr<State> state, std::shared_ptr<const duckdb_api::ScanExecutor> executor);

	std::shared_ptr<State> state;
	std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

std::shared_ptr<ControlledHttpRuntime>
BuildControlledHttpRuntime(uint64_t max_wall_milliseconds = duckdb_api::MAX_EXECUTION_MILLISECONDS,
                           uint64_t max_decoded_records = duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE);

} // namespace duckdb_api_test
