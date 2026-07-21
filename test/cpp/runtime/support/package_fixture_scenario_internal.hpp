#pragma once

#include "package_fixture_execution.hpp"
#include "runtime/support/package_fixture_checkpoint.hpp"

#include <atomic>

namespace duckdb_api_test {
namespace internal {

// Call-scoped control for closed package-fixture variants. Decoder
// cancellation is armed by the controlled transport: the production
// executor's post-transport check remains clear and the decoder's first
// checkpoint observes cancellation.
class RuntimeFixtureScenarioControl final : public duckdb_api::ExecutionControl,
                                            public RuntimeFixtureCheckpointObserver {
public:
	RuntimeFixtureScenarioControl(duckdb_api::ExecutionControl &outer, RuntimeFixtureCancellationPoint selected_point);

	bool IsCancellationRequested() const noexcept override;
	void ControlledTransportResponseReady() noexcept override;

	void Reach(RuntimeFixtureCancellationPoint point);
	bool CheckpointReached() const noexcept;

private:
	enum class DecodeCheckpointState { INACTIVE, ARMED, POST_TRANSPORT_PASSED, CANCELLED };

	duckdb_api::ExecutionControl &outer;
	RuntimeFixtureCancellationPoint selected_point;
	mutable std::atomic<bool> cancelled;
	mutable std::atomic<bool> checkpoint_reached;
	mutable std::atomic<DecodeCheckpointState> decode_state;
};

void ValidateRuntimeFixtureScenario(RuntimeFixtureScenario scenario);
void ValidateRuntimeFixtureFailure(RuntimeFixtureFailureExpectation expectation,
                                   const RuntimeFixtureExecutionObservation &observation);

} // namespace internal
} // namespace duckdb_api_test
