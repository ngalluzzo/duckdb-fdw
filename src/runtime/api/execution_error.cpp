#include "duckdb_api/execution.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api {

ExecutionError::ExecutionError(ErrorStage stage_p, std::string field_p, std::string safe_message_p)
    : stage(stage_p), field(std::move(field_p)), safe_message(std::move(safe_message_p)), properties {},
      classified(false) {
}

ExecutionError::ExecutionError(ErrorStage stage_p, std::string field_p, std::string safe_message_p,
                               FailureProperties properties_p)
    : stage(stage_p), field(std::move(field_p)), safe_message(std::move(safe_message_p)), properties(properties_p),
      classified(true) {
}

const char *ExecutionError::what() const noexcept {
	return safe_message.c_str();
}

ErrorStage ExecutionError::Stage() const noexcept {
	return stage;
}

const std::string &ExecutionError::Field() const {
	return field;
}

const std::string &ExecutionError::SafeMessage() const {
	return safe_message;
}

bool ExecutionError::Classified() const noexcept {
	return classified;
}

const FailureProperties &ExecutionError::Properties() const noexcept {
	return properties;
}

FailureClass ClassifyFailureClass(ErrorStage stage) {
	switch (stage) {
	case ErrorStage::TRANSPORT:
		return FailureClass::TRANSPORT;
	case ErrorStage::HTTP_STATUS:
		return FailureClass::REMOTE_STATUS;
	case ErrorStage::DECODE:
		return FailureClass::DECODE;
	case ErrorStage::SCHEMA:
		return FailureClass::SCHEMA;
	case ErrorStage::POLICY:
		return FailureClass::DESTINATION_POLICY;
	case ErrorStage::RESOURCE:
		return FailureClass::RESOURCE_BUDGET;
	case ErrorStage::INTERNAL:
		return FailureClass::INTERNAL;
	case ErrorStage::AUTHENTICATION:
		return FailureClass::CREDENTIAL_PROVIDER;
	case ErrorStage::AUTHORIZATION:
		return FailureClass::AUTHORIZATION;
	case ErrorStage::REMOTE_PROTOCOL:
		return FailureClass::PROTOCOL;
	}
	throw std::logic_error("unknown ErrorStage");
}

const char *FailureClassName(FailureClass failure_class) {
	switch (failure_class) {
	case FailureClass::CONFIGURATION:
		return "configuration";
	case FailureClass::AUTHORIZATION:
		return "authorization";
	case FailureClass::CREDENTIAL_PROVIDER:
		return "credential_provider";
	case FailureClass::DESTINATION_POLICY:
		return "destination_policy";
	case FailureClass::TRANSPORT:
		return "transport";
	case FailureClass::TIMEOUT:
		return "timeout";
	case FailureClass::REMOTE_STATUS:
		return "remote_status";
	case FailureClass::RATE_LIMIT:
		return "rate_limit";
	case FailureClass::PROTOCOL:
		return "protocol";
	case FailureClass::DECODE:
		return "decode";
	case FailureClass::SCHEMA:
		return "schema";
	case FailureClass::RESOURCE_BUDGET:
		return "resource_budget";
	case FailureClass::CANCELLATION:
		return "cancellation";
	case FailureClass::INTERNAL:
		return "internal";
	}
	throw std::logic_error("unknown FailureClass");
}

const char *ReplayClassificationName(ReplayClassification classification) {
	switch (classification) {
	case ReplayClassification::NEVER_REPLAYABLE:
		return "never_replayable";
	case ReplayClassification::REPLAYABLE_BEFORE_EXPOSURE:
		return "replayable_before_exposure";
	case ReplayClassification::ATOMIC_TRAVERSAL_STEP:
		return "atomic_traversal_step";
	case ReplayClassification::SERVER_DIRECTED_DELAY:
		return "server_directed_delay";
	case ReplayClassification::INDETERMINATE:
		return "indeterminate";
	}
	throw std::logic_error("unknown ReplayClassification");
}

const char *FailurePhaseName(FailurePhase phase) {
	switch (phase) {
	case FailurePhase::BIND:
		return "bind";
	case FailurePhase::PLAN:
		return "plan";
	case FailurePhase::ADMIT:
		return "admit";
	case FailurePhase::REQUEST:
		return "request";
	case FailurePhase::TRANSPORT:
		return "transport";
	case FailurePhase::DECODE:
		return "decode";
	case FailurePhase::PAGINATE:
		return "paginate";
	case FailurePhase::EMIT:
		return "emit";
	case FailurePhase::CLOSE:
		return "close";
	}
	throw std::logic_error("unknown FailurePhase");
}

const char *BudgetDimensionName(BudgetDimension dimension) {
	switch (dimension) {
	case BudgetDimension::NONE:
		return "none";
	case BudgetDimension::TIME:
		return "time";
	case BudgetDimension::ATTEMPTS:
		return "attempts";
	case BudgetDimension::WAITING:
		return "waiting";
	case BudgetDimension::PAGES:
		return "pages";
	case BudgetDimension::RESPONSE_BYTES:
		return "response_bytes";
	case BudgetDimension::RECORDS:
		return "records";
	case BudgetDimension::MEMORY:
		return "memory";
	}
	throw std::logic_error("unknown BudgetDimension");
}

const char *RemoteStatusClassName(RemoteStatusClass status_class) {
	switch (status_class) {
	case RemoteStatusClass::NONE:
		return "none";
	case RemoteStatusClass::SUCCESS:
		return "success";
	case RemoteStatusClass::CLIENT_ERROR:
		return "client_error";
	case RemoteStatusClass::RATE_LIMITED:
		return "rate_limited";
	case RemoteStatusClass::SERVER_ERROR:
		return "server_error";
	case RemoteStatusClass::GRAPHQL_ERRORS:
		return "graphql_errors";
	}
	throw std::logic_error("unknown RemoteStatusClass");
}

TypedValue::TypedValue()
    : kind(ValueKind::VARCHAR), valid(false), bigint_value(0), varchar_value(), boolean_value(false),
      double_value(0.0) {
}

TypedValue::TypedValue(ValueKind kind_p, int64_t bigint_value_p, std::string varchar_value_p, bool boolean_value_p)
    : kind(kind_p), valid(true), bigint_value(bigint_value_p), varchar_value(std::move(varchar_value_p)),
      boolean_value(boolean_value_p), double_value(0.0) {
}

TypedValue TypedValue::BigInt(int64_t value) {
	TypedValue result;
	result.kind = ValueKind::BIGINT;
	result.valid = true;
	result.bigint_value = value;
	result.boolean_value = false;
	return result;
}

TypedValue TypedValue::Varchar(std::string value) {
	TypedValue result;
	result.kind = ValueKind::VARCHAR;
	result.valid = true;
	result.bigint_value = 0;
	result.varchar_value = std::move(value);
	result.boolean_value = false;
	return result;
}

TypedValue TypedValue::Boolean(bool value) {
	TypedValue result;
	result.kind = ValueKind::BOOLEAN;
	result.valid = true;
	result.bigint_value = 0;
	result.boolean_value = value;
	return result;
}

TypedValue TypedValue::Double(double value) {
	TypedValue result;
	result.kind = ValueKind::DOUBLE;
	result.valid = true;
	result.bigint_value = 0;
	result.boolean_value = false;
	// RFC 0020: -0.0 is normalized to 0.0 so every consumer sees one
	// canonical zero.
	result.double_value = value == 0.0 ? 0.0 : value;
	return result;
}

TypedValue TypedValue::Null(ValueKind kind) {
	TypedValue result;
	result.kind = kind;
	result.valid = false;
	result.bigint_value = 0;
	result.boolean_value = false;
	return result;
}

void TypedBatch::Clear() {
	column_kinds.clear();
	rows.clear();
}

bool TypedBatch::IsSchemaAligned() const noexcept {
	for (std::size_t row_index = 0; row_index < rows.size(); row_index++) {
		const auto &values = rows[row_index].values;
		if (values.size() != column_kinds.size()) {
			return false;
		}
		for (std::size_t column_index = 0; column_index < values.size(); column_index++) {
			if (values[column_index].kind != column_kinds[column_index]) {
				return false;
			}
		}
	}
	return true;
}

const char *ExecutionCancelled::what() const noexcept {
	return "execution cancelled";
}

ExecutionControl::~ExecutionControl() noexcept {
}

BatchStream::~BatchStream() noexcept {
}

ScanExecutor::~ScanExecutor() noexcept {
}

} // namespace duckdb_api
