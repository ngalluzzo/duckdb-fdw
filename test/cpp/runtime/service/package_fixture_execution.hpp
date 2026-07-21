#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <cstddef>
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

// Closed decoder counterexamples selected by column ordinal. Applicability is
// validated against the immutable planned column kind and nullability before a
// derived response reaches the production decoder.
enum class RuntimeFixtureColumnVariant {
	TYPE_MISMATCH_REJECTED,
	MISSING_REJECTED,
	NULL_REJECTED,
	BIGINT_MINIMUM,
	BIGINT_MAXIMUM,
	BIGINT_UNDERFLOW_REJECTED,
	BIGINT_OVERFLOW_REJECTED,
	BIGINT_FRACTION_REJECTED,
	VARCHAR_STRING_BUDGET_BOUNDARY,
	VARCHAR_STRING_BUDGET_ONE_OVER_REJECTED
};

struct RuntimeFixtureColumnScenario {
	std::size_t column_ordinal;
	RuntimeFixtureColumnVariant variant;
};

enum class RuntimeFixtureBoundaryVariant { BOUNDARY, ONE_OVER_REJECTED };

enum class RuntimeFixtureRelationResourceField {
	RESPONSE_BYTES_PER_PAGE,
	RESPONSE_BYTES_PER_SCAN,
	RECORDS_PER_PAGE,
	RECORDS_PER_SCAN,
	EXTRACTED_STRING_BYTES
};

enum class RuntimeFixtureGraphqlBodyResourceField { SERIALIZED_BODY_BYTES_PER_REQUEST, SERIALIZED_BODY_BYTES_PER_SCAN };

// Protocol-specific pagination failures stay distinct so a consumer never
// infers the selected mutation from a plan label or coverage-key spelling.
enum class RuntimeFixturePaginationFailureVariant {
	REST_MALFORMED_TARGET_REJECTED,
	REST_REPLAYED_TARGET_REJECTED,
	REST_MAX_PAGES_EXHAUSTED,
	GRAPHQL_MISSING_CURSOR_REJECTED,
	GRAPHQL_REPEATED_CURSOR_REJECTED,
	GRAPHQL_MAX_PAGES_EXHAUSTED
};

// Successful return is evidence that Runtime observed and internally
// validated the selected fixed production boundary. Boundary and one-over
// results are different alternatives rather than caller-interpreted booleans.
enum class RuntimeFixtureVariantOutcome { VALUE_SUCCEEDED, BOUNDARY_SUCCEEDED, EXPECTED_REJECTION, ONE_OVER_REJECTED };

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

struct RuntimeFixtureVariantObservation {
	RuntimeFixtureExecutionObservation execution;
	RuntimeFixtureVariantOutcome outcome;
	uint64_t observed_units;
	uint64_t admitted_limit;
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

	RuntimeFixtureVariantObservation ExecuteColumnVariant(const duckdb_api::ScanPlan &plan,
	                                                      const RuntimeFixtureTranscript &transcript,
	                                                      RuntimeFixtureColumnScenario scenario,
	                                                      duckdb_api::ExecutionControl &control) const;
	RuntimeFixtureVariantObservation ExecuteRelationResourceVariant(const duckdb_api::ScanPlan &plan,
	                                                                const RuntimeFixtureTranscript &transcript,
	                                                                RuntimeFixtureRelationResourceField field,
	                                                                RuntimeFixtureBoundaryVariant boundary,
	                                                                duckdb_api::ExecutionControl &control) const;
	RuntimeFixtureVariantObservation ExecuteGraphqlBodyResourceVariant(const duckdb_api::ScanPlan &plan,
	                                                                   const RuntimeFixtureTranscript &transcript,
	                                                                   RuntimeFixtureGraphqlBodyResourceField field,
	                                                                   RuntimeFixtureBoundaryVariant boundary,
	                                                                   duckdb_api::ExecutionControl &control) const;
	RuntimeFixtureVariantObservation ExecutePaginationFailureVariant(const duckdb_api::ScanPlan &plan,
	                                                                 const RuntimeFixtureTranscript &transcript,
	                                                                 RuntimeFixturePaginationFailureVariant variant,
	                                                                 duckdb_api::ExecutionControl &control) const;
};

} // namespace duckdb_api_test
