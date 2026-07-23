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
	Require(unavailable.failure_class == duckdb_api::FailureClass::RATE_LIMIT &&
	            unavailable.replay_classification == duckdb_api::ReplayClassification::SERVER_DIRECTED_DELAY,
	        "HTTP 503 was not classified as a server-directed rate limit");
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

	// Classified ExecutionError carries properties; unclassified does not.
	const duckdb_api::FailureProperties properties {duckdb_api::FailureClass::RESOURCE_BUDGET,
	                                                duckdb_api::FailurePhase::DECODE,
	                                                duckdb_api::ReplayClassification::ATOMIC_TRAVERSAL_STEP,
	                                                3,
	                                                1,
	                                                0,
	                                                duckdb_api::RemoteStatusClass::NONE,
	                                                duckdb_api::BudgetDimension::PAGES};
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

} // namespace

int main() {
	try {
		TestTypedValuesAndSchemaAlignment();
		TestNullableTypedValuesRetainKind();
		TestFlatArraySchemaAlignment();
		TestDoublePayloadAndAlignmentCancellation();
		TestStableErrorContract();
		TestFailureClassification();
		std::cout << "execution contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "execution contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
