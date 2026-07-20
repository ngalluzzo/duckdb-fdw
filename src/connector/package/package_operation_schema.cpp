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

} // namespace

OperationDeclaration DecodeOperationSchema(const SchemaReader &reader) {
	OperationDeclaration operation;
	reader.RequireMapping(
	    {"id", "fallback", "when", "cardinality", "replay_safety", "request", "response", "pagination"},
	    {"id", "cardinality", "replay_safety", "request"});
	operation.id = reader.Text("id");
	operation.cardinality = reader.Text("cardinality");
	operation.replay_safety = reader.Text("replay_safety");
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
