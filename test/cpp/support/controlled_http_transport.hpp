#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {

enum class ControlledHttpMode { RESPONSE, TRANSPORT_FAILURE, BLOCK_UNTIL_CANCEL };

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
};

// Test-only Remote Runtime composition. Query tests consume only this helper
// and the public executor contract; the implementation translation unit alone
// sees the private transport seam. It is never linked into an installed target.
class ControlledHttpRuntime {
public:
	struct State;

	std::shared_ptr<const duckdb_api::ScanExecutor> Executor() const;

	void Respond(uint32_t status, std::string body);
	void FailWithUnknownTransportDiagnostic(std::string diagnostic);
	void BlockUntilCancelled();
	ControlledRequestObservation Observation() const;

private:
	friend std::shared_ptr<ControlledHttpRuntime> BuildControlledHttpRuntime(uint64_t max_wall_milliseconds,
	                                                                         uint64_t max_decoded_records);
	ControlledHttpRuntime(std::shared_ptr<State> state, std::shared_ptr<const duckdb_api::ScanExecutor> executor);

	std::shared_ptr<State> state;
	std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

std::shared_ptr<ControlledHttpRuntime>
BuildControlledHttpRuntime(uint64_t max_wall_milliseconds = duckdb_api::MAX_EXECUTION_MILLISECONDS,
                           uint64_t max_decoded_records = 3);

} // namespace duckdb_api_test
