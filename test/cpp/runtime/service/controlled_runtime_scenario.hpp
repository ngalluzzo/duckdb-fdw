#pragma once

#include "duckdb_api/execution.hpp"

#include <cstdint>
#include <memory>

namespace duckdb_api_test {

// Named Runtime-owned response programs for cross-team product integration.
// Consumers select an outcome but cannot author HTTP/GraphQL bytes, cursors,
// transport failures, or decoder state. Exact wire behavior remains in
// Runtime's private curl targets.
enum class ControlledRuntimeScenarioId {
	RETAINED_REST_USER,
	RICKANDMORTY_CHARACTER_EPISODES,
	GRAPHQL_MULTI_PAGE_NULL_DUPLICATE,
	GRAPHQL_RELATIONAL_COMPOSITION,
	GRAPHQL_APPLICATION_ERROR,
	GRAPHQL_LATE_HTTP_STATUS,
	BLOCK_UNTIL_CANCEL
};

struct ControlledRuntimeScenarioObservation {
	uint64_t request_count;
	uint64_t expected_request_count;
	bool has_terminal_stage;
	duckdb_api::ErrorStage terminal_stage;
};

// Test-only Runtime service. Its sole execution surface is the public
// ScanExecutor; observations contain counters and an expected safe stage, never
// request bodies, cursors, response payloads, credentials, or private types.
class ControlledRuntimeScenario {
public:
	struct State;

	std::shared_ptr<const duckdb_api::ScanExecutor> Executor() const;
	ControlledRuntimeScenarioObservation Observation() const;

private:
	friend std::shared_ptr<ControlledRuntimeScenario>
	BuildControlledRuntimeScenario(ControlledRuntimeScenarioId scenario);
	ControlledRuntimeScenario(std::shared_ptr<State> state, std::shared_ptr<const duckdb_api::ScanExecutor> executor);

	std::shared_ptr<State> state;
	std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

std::shared_ptr<ControlledRuntimeScenario> BuildControlledRuntimeScenario(ControlledRuntimeScenarioId scenario);

} // namespace duckdb_api_test
