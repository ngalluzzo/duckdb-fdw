#include "duckdb_api/execution.hpp"
#include "support/require.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using duckdb_api_test::Require;

class CancelDuringAlignment final : public duckdb_api::ExecutionControl {
public:
	explicit CancelDuringAlignment(std::size_t cancel_at_p) : cancel_at(cancel_at_p), calls(0) {
	}

	bool IsCancellationRequested() const noexcept override {
		return ++calls >= cancel_at;
	}

private:
	std::size_t cancel_at;
	mutable std::size_t calls;
};

class LegacyExecutor final : public duckdb_api::ScanExecutor {
public:
	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		throw std::logic_error("legacy executor must not be opened by the close compatibility test");
	}
};

class LegacyStream final : public duckdb_api::BatchStream {
public:
	bool Next(duckdb_api::ExecutionControl &, duckdb_api::TypedBatch &) override {
		return false;
	}
	void Cancel() noexcept override {
	}
	void Close() noexcept override {
	}
};

static_assert(std::is_nothrow_destructible<duckdb_api::ExecutionControl>::value,
              "execution control teardown must be non-throwing");
static_assert(std::is_nothrow_destructible<duckdb_api::BatchStream>::value,
              "batch stream teardown must be non-throwing");
static_assert(std::is_nothrow_destructible<duckdb_api::ScanExecutor>::value,
              "scan executor teardown must be non-throwing");
static_assert(duckdb_api::ErrorStage::AUTHENTICATION != duckdb_api::ErrorStage::AUTHORIZATION,
              "authentication and authorization must remain distinct stages");
static_assert(duckdb_api::ErrorStage::REMOTE_PROTOCOL != duckdb_api::ErrorStage::DECODE,
              "remote protocol and local decode failures must remain distinct stages");

void TestTypedValuesAndSchemaAlignment() {
	const duckdb_api::TypedValue compatible {duckdb_api::ValueKind::BIGINT, 7, std::string(), false};
	Require(compatible.valid && compatible.kind == duckdb_api::ValueKind::BIGINT && compatible.bigint_value == 7,
	        "former four-field TypedValue construction lost source compatibility");
	const duckdb_api::TypedValue default_value;
	Require(!default_value.valid && default_value.kind == duckdb_api::ValueKind::VARCHAR,
	        "default TypedValue did not fail closed as a typed NULL");

	duckdb_api::TypedBatch batch;
	batch.column_types = {duckdb_api::ValueKind::BIGINT, duckdb_api::ValueKind::VARCHAR,
	                      duckdb_api::ValueKind::BOOLEAN};
	duckdb_api::TypedRow row;
	row.values.push_back(duckdb_api::TypedValue::BigInt(42));
	row.values.push_back(duckdb_api::TypedValue::Varchar("duckdb"));
	row.values.push_back(duckdb_api::TypedValue::Boolean(true));
	batch.rows.push_back(std::move(row));

	Require(batch.IsSchemaAligned(), "valid typed batch was not schema aligned");
	Require(batch.rows[0].values[0].valid && batch.rows[0].values[1].valid && batch.rows[0].values[2].valid,
	        "non-null factories did not retain valid scalar state");
	Require(batch.rows[0].values[0].bigint_value == 42, "BIGINT value was not retained losslessly");
	Require(batch.rows[0].values[1].varchar_value == "duckdb", "VARCHAR value was not retained");
	Require(batch.rows[0].values[2].boolean_value, "BOOLEAN value was not retained");

	batch.rows[0].values[2].kind = duckdb_api::ValueKind::VARCHAR;
	Require(!batch.IsSchemaAligned(), "typed batch accepted a row kind mismatch");
	batch.rows[0].values.pop_back();
	Require(!batch.IsSchemaAligned(), "typed batch accepted a row arity mismatch");
	batch.Clear();
	Require(batch.column_types.empty() && batch.rows.empty(), "typed batch clear retained values or schema");
}

void TestNullableTypedValuesRetainKind() {
	const auto null_bigint = duckdb_api::TypedValue::Null(duckdb_api::ValueKind::BIGINT);
	const auto null_varchar = duckdb_api::TypedValue::Null(duckdb_api::ValueKind::VARCHAR);
	const auto null_boolean = duckdb_api::TypedValue::Null(duckdb_api::ValueKind::BOOLEAN);
	Require(!null_bigint.valid && null_bigint.kind == duckdb_api::ValueKind::BIGINT && null_bigint.bigint_value == 0,
	        "NULL BIGINT did not retain kind with an inert payload");
	Require(!null_varchar.valid && null_varchar.kind == duckdb_api::ValueKind::VARCHAR &&
	            null_varchar.varchar_value.empty(),
	        "NULL VARCHAR did not retain kind with an inert payload");
	Require(!null_boolean.valid && null_boolean.kind == duckdb_api::ValueKind::BOOLEAN && !null_boolean.boolean_value,
	        "NULL BOOLEAN did not retain kind with an inert payload");

	duckdb_api::TypedBatch batch;
	batch.column_types = {duckdb_api::ValueKind::VARCHAR};
	duckdb_api::TypedRow row;
	row.values.push_back(null_varchar);
	batch.rows.push_back(std::move(row));
	Require(batch.IsSchemaAligned(), "typed NULL was mistaken for a schema mismatch");
}

void TestFlatArraySchemaAlignment() {
	duckdb_api::TypedBatch batch;
	batch.column_types = {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::VARCHAR, true)};
	std::vector<duckdb_api::TypedScalarValue> elements;
	elements.push_back(duckdb_api::TypedScalarValue::Varchar("first"));
	elements.push_back(duckdb_api::TypedScalarValue::Null(duckdb_api::ValueKind::VARCHAR));
	elements.push_back(duckdb_api::TypedScalarValue::Varchar("first"));
	batch.rows.push_back({{duckdb_api::TypedValue::Array(duckdb_api::ValueKind::VARCHAR, true, std::move(elements))}});
	Require(batch.IsSchemaAligned() && batch.rows[0].values[0].elements.size() == 3,
	        "flat ARRAY value did not preserve ordered nullable scalar children");

	auto wrong_kind = batch;
	wrong_kind.rows[0].values[0].elements[0] = duckdb_api::TypedScalarValue::BigInt(1);
	Require(!wrong_kind.IsSchemaAligned(), "ARRAY schema accepted a child-kind mismatch");
	auto forbidden_null = batch;
	forbidden_null.column_types[0] = duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::VARCHAR, false);
	forbidden_null.rows[0].values[0].element_nullable = false;
	Require(!forbidden_null.IsSchemaAligned(), "ARRAY schema accepted a forbidden child NULL");
	auto inactive_payload = batch;
	inactive_payload.rows[0].values[0].varchar_value = "not-flat";
	Require(!inactive_payload.IsSchemaAligned(), "ARRAY schema accepted an active parent scalar payload");
}

void TestDoublePayloadAndAlignmentCancellation() {
	duckdb_api::TypedBatch scalar;
	scalar.column_types = {duckdb_api::ValueKind::DOUBLE};
	scalar.rows.push_back({{duckdb_api::TypedValue::Double(1.5)}});
	Require(scalar.IsSchemaAligned(), "finite canonical DOUBLE was rejected");
	for (const auto invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
	                           std::numeric_limits<double>::quiet_NaN(), -0.0}) {
		auto malformed = scalar;
		malformed.rows[0].values[0].double_value = invalid;
		Require(!malformed.IsSchemaAligned(), "non-finite or noncanonical scalar DOUBLE payload was accepted");
	}

	duckdb_api::TypedBatch array;
	array.column_types = {duckdb_api::OutputValueType::Array(duckdb_api::ValueKind::DOUBLE, false)};
	std::vector<duckdb_api::TypedScalarValue> elements(64, duckdb_api::TypedScalarValue::Double(1.0));
	array.rows.push_back({{duckdb_api::TypedValue::Array(duckdb_api::ValueKind::DOUBLE, false, std::move(elements))}});
	auto malformed_child = array;
	malformed_child.rows[0].values[0].elements[32].double_value = std::numeric_limits<double>::infinity();
	Require(!malformed_child.IsSchemaAligned(), "ARRAY schema accepted a non-finite DOUBLE child");
	malformed_child = array;
	malformed_child.rows[0].values[0].elements[32].double_value = -0.0;
	Require(!malformed_child.IsSchemaAligned(), "ARRAY schema accepted a noncanonical negative-zero child");

	CancelDuringAlignment control(12);
	bool cancelled = false;
	try {
		(void)array.IsSchemaAligned(control);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancelled = true;
	}
	Require(cancelled, "ARRAY schema alignment did not observe cancellation during child traversal");
}

void TestStableErrorContract() {
	const duckdb_api::ExecutionError error(duckdb_api::ErrorStage::HTTP_STATUS, "", "HTTP endpoint rejected request");
	Require(error.Stage() == duckdb_api::ErrorStage::HTTP_STATUS, "error stage drifted");
	Require(error.Field().empty(), "status error unexpectedly exposed a field");
	Require(error.SafeMessage() == "HTTP endpoint rejected request", "safe error message drifted");
	Require(std::string(error.what()) == error.SafeMessage(), "what() exposed a different diagnostic");

	const duckdb_api::ExecutionCancelled cancellation;
	Require(std::string(cancellation.what()) == "execution cancelled", "cancellation marker drifted");

	const duckdb_api::ExecutionError authentication(duckdb_api::ErrorStage::AUTHENTICATION, "authorization",
	                                                "authentication failed");
	const duckdb_api::ExecutionError authorization(duckdb_api::ErrorStage::AUTHORIZATION, "authorization",
	                                               "authorization failed");
	Require(authentication.Stage() == duckdb_api::ErrorStage::AUTHENTICATION, "authentication error stage drifted");
	Require(authorization.Stage() == duckdb_api::ErrorStage::AUTHORIZATION, "authorization error stage drifted");
	const duckdb_api::ExecutionError remote_protocol(duckdb_api::ErrorStage::REMOTE_PROTOCOL, "errors",
	                                                 "remote GraphQL response reported an error");
	Require(remote_protocol.Stage() == duckdb_api::ErrorStage::REMOTE_PROTOCOL, "remote protocol error stage drifted");
	Require(remote_protocol.Field() == "errors" &&
	            remote_protocol.SafeMessage() == "remote GraphQL response reported an error",
	        "remote protocol diagnostic lost its safe structural contract");
}

void TestFailureClassification() {
	// ClassifyFailureClass: the coarse ErrorStage -> FailureClass fallback used at
	// the adapter translation boundary when no explicit properties were attached.
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::TRANSPORT) == duckdb_api::FailureClass::TRANSPORT,
	        "TRANSPORT stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::HTTP_STATUS) ==
	            duckdb_api::FailureClass::REMOTE_STATUS,
	        "HTTP_STATUS stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::DECODE) == duckdb_api::FailureClass::DECODE,
	        "DECODE stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::SCHEMA) == duckdb_api::FailureClass::SCHEMA,
	        "SCHEMA stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::POLICY) ==
	            duckdb_api::FailureClass::DESTINATION_POLICY,
	        "POLICY stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::RESOURCE) ==
	            duckdb_api::FailureClass::RESOURCE_BUDGET,
	        "RESOURCE stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::INTERNAL) == duckdb_api::FailureClass::INTERNAL,
	        "INTERNAL stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::AUTHENTICATION) ==
	            duckdb_api::FailureClass::CREDENTIAL_PROVIDER,
	        "AUTHENTICATION stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::AUTHORIZATION) ==
	            duckdb_api::FailureClass::AUTHORIZATION,
	        "AUTHORIZATION stage classified incorrectly");
	Require(duckdb_api::ClassifyFailureClass(duckdb_api::ErrorStage::REMOTE_PROTOCOL) ==
	            duckdb_api::FailureClass::PROTOCOL,
	        "REMOTE_PROTOCOL stage classified incorrectly");

	// HttpStatusFailureProperties: the four-way status distinction (RFC 0021 §2).
	const auto rate_limited = duckdb_api::HttpStatusFailureProperties(429, true);
	Require(rate_limited.failure_class == duckdb_api::FailureClass::RATE_LIMIT &&
	            rate_limited.remote_status_class == duckdb_api::RemoteStatusClass::RATE_LIMITED &&
	            rate_limited.replay_classification == duckdb_api::ReplayClassification::SERVER_DIRECTED_DELAY,
	        "HTTP 429 was not classified as a server-directed rate limit");
	const auto unavailable = duckdb_api::HttpStatusFailureProperties(503, false);
	Require(unavailable.failure_class == duckdb_api::FailureClass::REMOTE_STATUS &&
	            unavailable.remote_status_class == duckdb_api::RemoteStatusClass::SERVER_ERROR,
	        "HTTP 503 without Retry-After was not classified as a server error");
	const auto directed_unavailable = duckdb_api::HttpStatusFailureProperties(503, false, true);
	Require(directed_unavailable.failure_class == duckdb_api::FailureClass::RATE_LIMIT &&
	            directed_unavailable.replay_classification == duckdb_api::ReplayClassification::SERVER_DIRECTED_DELAY,
	        "HTTP 503 with Retry-After was not classified as a server-directed rate limit");
	const auto auth_rejected = duckdb_api::HttpStatusFailureProperties(401, true);
	Require(auth_rejected.failure_class == duckdb_api::FailureClass::AUTHORIZATION &&
	            auth_rejected.remote_status_class == duckdb_api::RemoteStatusClass::CLIENT_ERROR,
	        "HTTP 401 (auth attempted) was not classified as an authorization rejection");
	const auto anon_rejected = duckdb_api::HttpStatusFailureProperties(401, false);
	Require(anon_rejected.failure_class == duckdb_api::FailureClass::REMOTE_STATUS,
	        "HTTP 401 (anonymous) was not classified as a remote-status rejection");
	const auto server_error = duckdb_api::HttpStatusFailureProperties(500, false);
	Require(server_error.failure_class == duckdb_api::FailureClass::REMOTE_STATUS &&
	            server_error.remote_status_class == duckdb_api::RemoteStatusClass::SERVER_ERROR,
	        "HTTP 5xx was not classified as a server error");
	const auto client_error = duckdb_api::HttpStatusFailureProperties(404, false);
	Require(client_error.failure_class == duckdb_api::FailureClass::REMOTE_STATUS &&
	            client_error.remote_status_class == duckdb_api::RemoteStatusClass::CLIENT_ERROR,
	        "HTTP 4xx was not classified as a client error");
	// A status is observed before decode: no rows exposed, v1 attempt is 1.
	Require(rate_limited.rows_exposed == 0 && rate_limited.attempt == 1,
	        "status failure did not default rows_exposed=0 / attempt=1");

	// Closed-set name functions bind the C++ vocabulary to the freeze strings.
	Require(std::string(duckdb_api::FailureClassName(duckdb_api::FailureClass::RATE_LIMIT)) == "rate_limit",
	        "FailureClassName drifted from the freeze string");
	Require(std::string(duckdb_api::ExposureStateName(duckdb_api::ExposureState::ACCEPTED_UNEXPOSED)) ==
	            "accepted_unexposed",
	        "ExposureStateName drifted from the retry contract");
	Require(std::string(duckdb_api::ReplayClassificationName(
	            duckdb_api::ReplayClassification::SERVER_DIRECTED_DELAY)) == "server_directed_delay",
	        "ReplayClassificationName drifted from the freeze string");
	Require(std::string(duckdb_api::FailurePhaseName(duckdb_api::FailurePhase::REQUEST)) == "request",
	        "FailurePhaseName drifted");
	Require(std::string(duckdb_api::BudgetDimensionName(duckdb_api::BudgetDimension::TIME)) == "time",
	        "BudgetDimensionName drifted");
	Require(std::string(duckdb_api::RemoteStatusClassName(duckdb_api::RemoteStatusClass::RATE_LIMITED)) ==
	            "rate_limited",
	        "RemoteStatusClassName drifted");
	Require(std::string(duckdb_api::RateLimitReasonName(duckdb_api::RateLimitReason::POLICY_FAIL)) == "policy_fail" &&
	            std::string(duckdb_api::RateLimitReasonName(duckdb_api::RateLimitReason::BUCKET_CHANGED)) ==
	                "bucket_changed" &&
	            std::string(duckdb_api::RateLimitReasonName(duckdb_api::RateLimitReason::TICKET_EXHAUSTED)) ==
	                "ticket_exhausted",
	        "RateLimitReasonName drifted from the freeze strings");
	Require(std::string(duckdb_api::AdmissionReasonName(duckdb_api::AdmissionReason::RUNTIME_CLOSED)) ==
	                "runtime_closed" &&
	            std::string(duckdb_api::AdmissionScopeName(duckdb_api::AdmissionScope::PRINCIPAL)) == "principal",
	        "admission reason or scope name drifted from the freeze strings");
	const auto local = duckdb_api::LocalAdmissionFailureProperties(duckdb_api::AdmissionReason::REQUEST_QUEUE_SATURATED,
	                                                               duckdb_api::AdmissionScope::CONNECTOR, 64, 64, 1, 17,
	                                                               true, duckdb_api::FailurePhase::REQUEST);
	Require(local.failure_class == duckdb_api::FailureClass::LOCAL_ADMISSION &&
	            local.phase == duckdb_api::FailurePhase::REQUEST &&
	            local.replay_classification == duckdb_api::ReplayClassification::NEVER_REPLAYABLE &&
	            local.terminating_budget == duckdb_api::BudgetDimension::NONE &&
	            local.admission_reason == duckdb_api::AdmissionReason::REQUEST_QUEUE_SATURATED &&
	            local.admission_scope == duckdb_api::AdmissionScope::CONNECTOR && local.admission_limit == 64 &&
	            local.admission_observed == 64 && local.admission_requested == 1 &&
	            local.cumulative_admission_waiting_milliseconds == 17 && local.admission_waiting,
	        "local admission failure properties lost their closed failure contract");
	LegacyExecutor legacy;
	legacy.Close();
	legacy.Close();
	LegacyStream legacy_stream;
	const auto legacy_snapshot = legacy_stream.Diagnostics();
	Require(legacy_snapshot.admission_reason == duckdb_api::AdmissionReason::NONE &&
	            legacy_snapshot.admission_scope == duckdb_api::AdmissionScope::NONE &&
	            legacy_snapshot.admission_limit == 0 && legacy_snapshot.admission_observed == 0 &&
	            legacy_snapshot.admission_requested == 0 &&
	            legacy_snapshot.cumulative_admission_waiting_milliseconds == 0 && !legacy_snapshot.admission_waiting,
	        "legacy stream diagnostics did not default every appended admission field closed");
	Require(rate_limited.admission_reason == duckdb_api::AdmissionReason::NONE &&
	            rate_limited.admission_scope == duckdb_api::AdmissionScope::NONE && rate_limited.admission_limit == 0 &&
	            rate_limited.admission_observed == 0 && rate_limited.admission_requested == 0 &&
	            rate_limited.cumulative_admission_waiting_milliseconds == 0 && !rate_limited.admission_waiting,
	        "pre-admission failure factories did not default appended admission properties closed");
	bool rejected_unknown_reason = false;
	try {
		(void)duckdb_api::AdmissionReasonName(static_cast<duckdb_api::AdmissionReason>(255));
	} catch (const std::logic_error &) {
		rejected_unknown_reason = true;
	}
	bool rejected_unknown_scope = false;
	try {
		(void)duckdb_api::AdmissionScopeName(static_cast<duckdb_api::AdmissionScope>(255));
	} catch (const std::logic_error &) {
		rejected_unknown_scope = true;
	}
	Require(rejected_unknown_reason && rejected_unknown_scope,
	        "admission diagnostic vocabulary did not fail closed on unknown enum values");

	// ClassifyReplay: the AGENTS.md retry invariant (declared safety AND uncommitted
	// replay unit) as a truth table.
	Require(duckdb_api::ClassifyReplay(true, false) == duckdb_api::ReplayClassification::REPLAYABLE_BEFORE_EXPOSURE,
	        "safe failure before exposure must be replayable");
	Require(duckdb_api::ClassifyReplay(true, true) == duckdb_api::ReplayClassification::NEVER_REPLAYABLE,
	        "safe failure after exposure must not be replayable (commitment crossed)");
	Require(duckdb_api::ClassifyReplay(false, false) == duckdb_api::ReplayClassification::NEVER_REPLAYABLE,
	        "unsafe operation must not be replayable even before exposure");
	Require(duckdb_api::ClassifyReplay(false, true) == duckdb_api::ReplayClassification::NEVER_REPLAYABLE,
	        "unsafe operation after exposure must not be replayable");

	// BudgetDimensionFromField: the four-way termination distinguishability signal.
	Require(duckdb_api::BudgetDimensionFromField("wall_milliseconds") == duckdb_api::BudgetDimension::TIME,
	        "wall-time field did not map to TIME");
	Require(duckdb_api::BudgetDimensionFromField("pages") == duckdb_api::BudgetDimension::PAGES,
	        "pages field did not map to PAGES");
	Require(duckdb_api::BudgetDimensionFromField("decoded_memory_bytes") == duckdb_api::BudgetDimension::MEMORY,
	        "memory field did not map to MEMORY");
	Require(duckdb_api::BudgetDimensionFromField("request_attempts") == duckdb_api::BudgetDimension::ATTEMPTS,
	        "attempts field did not map to ATTEMPTS");
	Require(duckdb_api::BudgetDimensionFromField("cumulative_waiting_milliseconds") ==
	            duckdb_api::BudgetDimension::WAITING,
	        "waiting field did not map to WAITING");
	Require(duckdb_api::BudgetDimensionFromField("unknown") == duckdb_api::BudgetDimension::NONE,
	        "unknown field did not map to NONE");
	const auto deadline_budget = duckdb_api::ResourceBudgetFailureProperties("wall_milliseconds");
	Require(deadline_budget.failure_class == duckdb_api::FailureClass::RESOURCE_BUDGET &&
	            deadline_budget.terminating_budget == duckdb_api::BudgetDimension::TIME,
	        "resource-budget termination did not carry its terminating dimension");
	const auto page_budget = duckdb_api::ResourceBudgetFailureProperties("pages");
	Require(page_budget.terminating_budget == duckdb_api::BudgetDimension::PAGES,
	        "page-budget termination did not carry PAGES");
	const auto accepted =
	    duckdb_api::EnrichRetryFailureProperties(duckdb_api::ResourceBudgetFailureProperties("decoded_memory_bytes"), 2,
	                                             1, 0, 0, duckdb_api::ExposureState::ACCEPTED_UNEXPOSED);
	const auto exposed = duckdb_api::EnrichRetryFailureProperties(duckdb_api::HttpStatusFailureProperties(429, false),
	                                                              2, 1, 4, 0, duckdb_api::ExposureState::EXPOSED);
	Require(accepted.replay_classification == duckdb_api::ReplayClassification::NEVER_REPLAYABLE &&
	            exposed.replay_classification == duckdb_api::ReplayClassification::NEVER_REPLAYABLE,
	        "accepted or exposed retry diagnostics retained replay authority");

	// Classified ExecutionError carries properties; unclassified does not.
	duckdb_api::FailureProperties properties {};
	properties.failure_class = duckdb_api::FailureClass::RESOURCE_BUDGET;
	properties.phase = duckdb_api::FailurePhase::DECODE;
	properties.replay_classification = duckdb_api::ReplayClassification::ATOMIC_TRAVERSAL_STEP;
	properties.step = 3;
	properties.attempt = 1;
	properties.remote_status_class = duckdb_api::RemoteStatusClass::NONE;
	properties.terminating_budget = duckdb_api::BudgetDimension::PAGES;
	properties.exposure_state = duckdb_api::ExposureState::UNACCEPTED;
	properties.rate_limit_reason = duckdb_api::RateLimitReason::NONE;
	const duckdb_api::ExecutionError classified(duckdb_api::ErrorStage::RESOURCE, "pages",
	                                            "scan exhausted its page budget", properties);
	Require(classified.Classified() &&
	            classified.Properties().failure_class == duckdb_api::FailureClass::RESOURCE_BUDGET &&
	            classified.Properties().step == 3 &&
	            classified.Properties().terminating_budget == duckdb_api::BudgetDimension::PAGES,
	        "classified ExecutionError did not carry its failure properties");
	const duckdb_api::ExecutionError unclassified(duckdb_api::ErrorStage::RESOURCE, "pages",
	                                              "scan exhausted its page budget");
	Require(!unclassified.Classified(), "unclassified ExecutionError reported Classified() true");
}

void TestFourWayTerminationDistinguishability() {
	// RFC 0021: deadline expiry and local exhaustion are both RESOURCE_BUDGET
	// but carry distinct terminating_budget dimensions, so they stay
	// distinguishable; cancellation is a distinct marker type; remote timeout
	// is a reserved distinct class (no v1 emitter).
	const auto deadline = duckdb_api::ResourceBudgetFailureProperties("wall_milliseconds");
	const auto memory = duckdb_api::ResourceBudgetFailureProperties("decoded_memory_bytes");
	const auto pages = duckdb_api::ResourceBudgetFailureProperties("pages");
	Require(deadline.failure_class == memory.failure_class && memory.failure_class == pages.failure_class,
	        "resource terminations were not all RESOURCE_BUDGET");
	Require(deadline.terminating_budget == duckdb_api::BudgetDimension::TIME &&
	            memory.terminating_budget == duckdb_api::BudgetDimension::MEMORY &&
	            pages.terminating_budget == duckdb_api::BudgetDimension::PAGES,
	        "resource terminations did not carry distinct terminating budgets");
	Require(deadline.terminating_budget != memory.terminating_budget &&
	            deadline.terminating_budget != pages.terminating_budget,
	        "deadline expiry was not distinguishable from local exhaustion");
	Require(std::string(duckdb_api::FailureClassName(duckdb_api::FailureClass::TIMEOUT)) == "timeout",
	        "timeout was not the reserved remote-timeout class");
	const duckdb_api::ExecutionCancelled cancellation;
	Require(std::string(cancellation.what()) == "execution cancelled",
	        "cancellation was not the distinct interruption marker type");
}

void TestStructuredFieldRedaction() {
	// RFC 0021: every structured FailureProperties field is a closed code or
	// count; its name renders only the freeze vocabulary and must never echo
	// body/document/cursor/row/credential content from the failure context.
	const std::string canary = "secret-canary-body-cursor-row-credential";
	const duckdb_api::ExecutionError error(duckdb_api::ErrorStage::HTTP_STATUS, "http_status",
	                                       std::string("response body contained ") + canary,
	                                       duckdb_api::HttpStatusFailureProperties(429, true));
	Require(error.Classified(), "canary error was not classified");
	const auto &properties = error.Properties();
	for (const char *name :
	     {duckdb_api::FailureClassName(properties.failure_class), duckdb_api::FailurePhaseName(properties.phase),
	      duckdb_api::ReplayClassificationName(properties.replay_classification),
	      duckdb_api::RemoteStatusClassName(properties.remote_status_class),
	      duckdb_api::BudgetDimensionName(properties.terminating_budget),
	      duckdb_api::RateLimitReasonName(properties.rate_limit_reason)}) {
		Require(std::string(name).find(canary) == std::string::npos,
		        "a structured failure field echoed redacted content");
	}
	Require(properties.rows_exposed == 0 && properties.attempt == 1 && properties.step == 0,
	        "identity/exposure fields were not closed ordinals/counts");
}

} // namespace

int main() {
	try {
		TestTypedValuesAndSchemaAlignment();
		TestNullableTypedValuesRetainKind();
		TestFlatArraySchemaAlignment();
		TestDoublePayloadAndAlignmentCancellation();
		TestStableErrorContract();
		TestFailureClassification();
		TestFourWayTerminationDistinguishability();
		TestStructuredFieldRedaction();
		std::cout << "execution contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "execution contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
