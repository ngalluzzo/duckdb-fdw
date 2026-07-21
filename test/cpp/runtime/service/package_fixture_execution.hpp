#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {

// Closed authorization state for offline package-fixture execution. Runtime
// creates and owns any synthetic bearer capability; callers can never supply
// credential bytes, an authenticator, a placement, or destination authority.
enum class RuntimeFixtureAuthorizationState { ANONYMOUS, BEARER_PRESENT, BEARER_MISSING };

// Closed Runtime-owned checkpoints used by the project fixture provider. The
// caller selects lifecycle evidence, never a poll count, callback, thread, or
// transport behavior. NONE preserves ordinary transcript execution.
enum class RuntimeFixtureCancellationPoint { NONE, BEFORE_REQUEST, TRANSPORT, DECODE, PAGE_BOUNDARY, STREAM_CLOSE };

// Closed Runtime failure families. The service validates that the production
// executor reached the selected boundary while preserving the executor's exact
// stable stage and field in RuntimeFixtureExecutionObservation. Runtime never
// receives an expected diagnostic string or a connector coverage key.
enum class RuntimeFixtureFailureExpectation {
	NONE,
	TRANSPORT,
	DECODE,
	GRAPHQL_APPLICATION_ERRORS,
	GRAPHQL_RESPONSE_ROLE,
	PAGINATION,
	RESOURCE
};

class RuntimeFixtureScenario {
public:
	static RuntimeFixtureScenario Standard();
	static RuntimeFixtureScenario CancelAt(RuntimeFixtureCancellationPoint point);
	static RuntimeFixtureScenario Expect(RuntimeFixtureFailureExpectation failure);

	RuntimeFixtureCancellationPoint Cancellation() const noexcept;
	RuntimeFixtureFailureExpectation Failure() const noexcept;

private:
	RuntimeFixtureScenario(RuntimeFixtureCancellationPoint cancellation, RuntimeFixtureFailureExpectation failure);

	RuntimeFixtureCancellationPoint cancellation;
	RuntimeFixtureFailureExpectation failure;
};

struct RuntimeFixtureResponseHeader {
	std::string name;
	std::string value;
};

// One identity-verified response page supplied by the fixture orchestrator.
// Payload identity and package-file custody are Connector responsibilities;
// Runtime treats the bounded body as controlled transport input and preserves
// only the response metadata used by production execution.
struct RuntimeFixtureResponsePage {
	uint32_t status;
	std::vector<RuntimeFixtureResponseHeader> headers;
	std::string body;
};

struct RuntimeFixtureTranscript {
	RuntimeFixtureAuthorizationState authorization;
	std::vector<RuntimeFixtureResponsePage> pages;
};

// Exact synthetic request material observed below production Runtime
// admission, authentication, materialization, pagination, and body rendering.
// Authorization values are replaced with `<redacted>` by the transport before
// this value is constructed. This non-installed test API is never a production
// diagnostic surface.
struct RuntimeFixtureRequestObservation {
	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string target;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	std::string content_type;
};

// Result of one isolated offline execution. Success owns all typed rows in
// stream order. Failure owns only Runtime's stable stage and field; partial
// rows never escape as a successful observation. The plan snapshot is safe
// Semantics explanation, not request authority or a serialization format.
struct RuntimeFixtureExecutionObservation {
	bool succeeded;
	std::vector<duckdb_api::TypedRow> rows;
	std::string safe_plan_snapshot;
	bool has_runtime_error;
	duckdb_api::ErrorStage runtime_error_stage;
	std::string runtime_error_field;
	std::vector<RuntimeFixtureRequestObservation> requests;
	bool transport_observed;
	uint64_t request_count;
	// Lifecycle evidence is populated by ExecuteScenario. Cancellation is a
	// terminal outcome rather than a partial success; rows are always empty.
	bool cancellation_observed;
	RuntimeFixtureCancellationPoint cancellation_point;
	bool checkpoint_reached;
	bool stream_cancel_invoked;
	bool stream_close_invoked;
	bool post_close_exhaustion_observed;
};

// Runtime-owned, test-only adapter for Connector's offline fixture
// orchestrator. Execute creates a fresh controlled transport and production
// executor for every call, performs no live network access, and retains no
// plan, transcript, authorization, control, stream, or observation state.
//
// The caller supplies a complete immutable ScanPlan already produced by
// Semantics. Runtime never receives a compiled package, package source, YAML,
// coverage vocabulary, expected rows, or request oracle. Cancellation is
// call-scoped and propagates as ExecutionCancelled. ExecutionError is captured
// only as its stable redacted stage/field pair.
class RuntimePackageFixtureExecutionService {
public:
	RuntimeFixtureExecutionObservation Execute(const duckdb_api::ScanPlan &plan,
	                                           const RuntimeFixtureTranscript &transcript,
	                                           duckdb_api::ExecutionControl &control) const;

	// Runs one closed Runtime-owned variant and captures cancellation as
	// structured evidence. The scenario may select exactly one cancellation
	// checkpoint or one failure family. It cannot program arbitrary hooks, and
	// every call owns a fresh executor, transport, control, and stream.
	RuntimeFixtureExecutionObservation ExecuteScenario(const duckdb_api::ScanPlan &plan,
	                                                   const RuntimeFixtureTranscript &transcript,
	                                                   RuntimeFixtureScenario scenario,
	                                                   duckdb_api::ExecutionControl &control) const;
};

} // namespace duckdb_api_test
