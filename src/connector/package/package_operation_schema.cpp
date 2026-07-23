#include "package_relation_schema_parts.hpp"

namespace duckdb_api {
namespace connector {
namespace internal {

namespace {

void RequireValue(const LocatedText &value, const char *expected, PackageDiagnosticCode code,
                  PackageDiagnosticPhase phase, PackageDiagnosticSink &diagnostics) {
	if (value.value != expected) {
		diagnostics.Add(code, phase, value.mark);
	}
}

void RequireIdentifier(const LocatedText &value, PackageDiagnosticSink &diagnostics) {
	if (!IsIdentifier(value.value)) {
		diagnostics.Add(PackageDiagnosticCode::INVALID_IDENTIFIER, PackageDiagnosticPhase::SCHEMA, value.mark);
	}
}

SelectorDeclaration DecodeSelector(const SchemaReader &reader) {
	SelectorDeclaration selector;
	selector.fallback = reader.Field("fallback") != nullptr;
	selector.mark = reader.Mark();
	if (selector.fallback) {
		RequireValue(reader.Text("fallback"), "true", PackageDiagnosticCode::INVALID_SELECTOR,
		             PackageDiagnosticPhase::COMPILE, reader.Diagnostics());
		if (reader.Field("when") != nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_SELECTOR, PackageDiagnosticPhase::COMPILE,
			                         reader.FieldMark("when"));
		}
		return selector;
	}
	if (reader.Field("when") == nullptr) {
		reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_SELECTOR, PackageDiagnosticPhase::COMPILE,
		                         reader.Mark());
		return selector;
	}
	auto when = reader.Child("when");
	when.RequireMapping({"required_inputs"}, {"required_inputs"});
	selector.required_inputs = when.TextSequence("required_inputs", 1, 128);
	return selector;
}

RetryDeclaration DecodeRetry(const SchemaReader &reader) {
	RetryDeclaration retry;
	retry.present = true;
	retry.mark = reader.Mark();
	reader.RequireMapping(
	    {"max_attempts_per_step", "max_delay_milliseconds", "max_cumulative_waiting_milliseconds_per_scan"},
	    {"max_attempts_per_step", "max_delay_milliseconds", "max_cumulative_waiting_milliseconds_per_scan"});
	retry.max_attempts_per_step = reader.Text("max_attempts_per_step");
	retry.max_delay_milliseconds = reader.Text("max_delay_milliseconds");
	retry.max_cumulative_waiting_milliseconds_per_scan = reader.Text("max_cumulative_waiting_milliseconds_per_scan");
	std::uint64_t attempts = 0;
	std::uint64_t delay = 0;
	std::uint64_t waiting = 0;
	if (!IsCanonicalUnsigned(retry.max_attempts_per_step, attempts) || attempts < 2 || attempts > 3) {
		reader.Diagnostics().Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                         retry.max_attempts_per_step.mark);
	}
	if (!IsCanonicalUnsigned(retry.max_delay_milliseconds, delay) || delay == 0 || delay > 100) {
		reader.Diagnostics().Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                         retry.max_delay_milliseconds.mark);
	}
	if (!IsCanonicalUnsigned(retry.max_cumulative_waiting_milliseconds_per_scan, waiting) || waiting == 0 ||
	    waiting > 250) {
		reader.Diagnostics().Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                         retry.max_cumulative_waiting_milliseconds_per_scan.mark);
	}
	return retry;
}

} // namespace

OperationDeclaration DecodeOperationSchema(const SchemaReader &reader, bool retry_supported) {
	OperationDeclaration operation;
	if (retry_supported) {
		reader.RequireMapping(
		    {"id", "fallback", "when", "cardinality", "replay_safety", "retry", "request", "response", "pagination"},
		    {"id", "cardinality", "replay_safety", "request"});
	} else {
		reader.RequireMapping(
		    {"id", "fallback", "when", "cardinality", "replay_safety", "request", "response", "pagination"},
		    {"id", "cardinality", "replay_safety", "request"});
	}
	operation.id = reader.Text("id");
	operation.cardinality = reader.Text("cardinality");
	operation.replay_safety = reader.Text("replay_safety");
	operation.retry.present = false;
	if (retry_supported && reader.Field("retry") != nullptr) {
		operation.retry = DecodeRetry(reader.Child("retry"));
	}
	operation.selector = DecodeSelector(reader);
	operation.mark = reader.Mark();
	RequireIdentifier(operation.id, reader.Diagnostics());
	RequireValue(operation.replay_safety, "safe", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
	             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	const auto *request_node = reader.Field("request");
	if (request_node == nullptr || request_node->Type() != FailsafeYamlNode::Kind::MAPPING) {
		reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_TYPE, PackageDiagnosticPhase::SCHEMA,
		                         reader.FieldMark("request"));
		return operation;
	}
	auto request = reader.Child("request");
	const auto protocol = request.Text("protocol");
	operation.graphql = protocol.value == "graphql";
	if (operation.graphql) {
		if (reader.Field("response") != nullptr || reader.Field("pagination") != nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::UNKNOWN_FIELD, PackageDiagnosticPhase::SCHEMA,
			                         operation.mark);
		}
		RequireValue(operation.cardinality, "many", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
		             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
		operation.graphql_request = DecodeGraphqlRequestSchema(request);
	} else {
		if (reader.Field("response") == nullptr || reader.Field("pagination") == nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::MISSING_FIELD, PackageDiagnosticPhase::SCHEMA,
			                         operation.mark);
		}
		if (operation.cardinality.value != "one" && operation.cardinality.value != "many") {
			reader.Diagnostics().Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
			                         operation.cardinality.mark);
		}
		operation.rest = DecodeRestRequestSchema(request);
		operation.response = DecodeRestResponseSchema(reader.Child("response"));
		operation.rest_pagination = DecodeRestPaginationSchema(reader.Child("pagination"));
	}
	return operation;
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
