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

QueryFieldDeclaration DecodeQueryField(const SchemaReader &reader) {
	QueryFieldDeclaration field;
	reader.RequireMapping(
	    {"name", "literal", "input", "conditional_input", "encoding", "omit_when_unbound", "omit_when_null"},
	    {"name", "encoding"});
	field.name = reader.Text("name");
	field.encoding = reader.Text("encoding");
	field.mark = reader.Mark();
	const bool literal = reader.Field("literal") != nullptr;
	const bool input = reader.Field("input") != nullptr;
	const bool conditional = reader.Field("conditional_input") != nullptr;
	if (static_cast<int>(literal) + static_cast<int>(input) + static_cast<int>(conditional) != 1) {
		reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_TYPE, PackageDiagnosticPhase::SCHEMA, field.mark);
	}
	if (literal) {
		field.kind = QueryFieldKind::LITERAL;
		field.source = reader.Text("literal");
		if (reader.Field("omit_when_unbound") != nullptr || reader.Field("omit_when_null") != nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::UNKNOWN_FIELD, PackageDiagnosticPhase::SCHEMA, field.mark);
		}
	} else if (input) {
		field.kind = QueryFieldKind::INPUT;
		field.source = reader.Text("input");
		RequireIdentifier(field.source, reader.Diagnostics());
		RequireValue(reader.Text("omit_when_unbound"), "true", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
		             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
		RequireValue(reader.Text("omit_when_null"), "true", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
		             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	} else {
		field.kind = QueryFieldKind::CONDITIONAL;
		field.source = reader.Text("conditional_input");
		RequireIdentifier(field.source, reader.Diagnostics());
		RequireValue(reader.Text("omit_when_unbound"), "true", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
		             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
		if (reader.Field("omit_when_null") != nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::UNKNOWN_FIELD, PackageDiagnosticPhase::SCHEMA, field.mark);
		}
	}
	if (!IsQueryName(field.name.value)) {
		reader.Diagnostics().Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                         field.name.mark);
	}
	RequireValue(field.encoding, "form_urlencoded", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
	             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	return field;
}

} // namespace

RestRequestDeclaration DecodeRestRequestSchema(const SchemaReader &reader) {
	RestRequestDeclaration request;
	reader.RequireMapping({"protocol", "method", "origin", "path", "query", "headers"},
	                      {"protocol", "method", "origin", "path", "query", "headers"});
	request.protocol = reader.Text("protocol");
	request.method = reader.Text("method");
	request.origin = DecodeHttpOrigin(reader.Child("origin"));
	request.path = reader.Text("path");
	request.headers = DecodeHttpHeaders(reader);
	request.mark = reader.Mark();
	RequireValue(request.protocol, "rest", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
	             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	RequireValue(request.method, "GET", PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
	             reader.Diagnostics());
	if (!IsFixedPath(request.path.value)) {
		reader.Diagnostics().Add(PackageDiagnosticCode::POLICY_WIDENING, PackageDiagnosticPhase::COMPILE,
		                         request.path.mark);
	}
	const auto *query = reader.Sequence("query", 0, 64);
	if (query != nullptr) {
		request.query.reserve(query->Size());
		for (std::size_t index = 0; index < query->Size(); index++) {
			request.query.push_back(
			    DecodeQueryField(reader.Child(query->SequenceValue(index), ".query[" + std::to_string(index) + "]")));
		}
	}
	return request;
}

RestResponseDeclaration DecodeRestResponseSchema(const SchemaReader &reader) {
	RestResponseDeclaration response;
	reader.RequireMapping({"source", "records"}, {"source"});
	response.source = reader.Text("source");
	response.records = reader.Text("records", false);
	response.mark = reader.Mark();
	if (response.source.value == "terminal_collection") {
		if (reader.Field("records") == nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::MISSING_FIELD, PackageDiagnosticPhase::SCHEMA,
			                         response.records.mark);
		} else if (!IsExtractor(response.records.value, true)) {
			reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_EXTRACTOR, PackageDiagnosticPhase::SCHEMA,
			                         response.records.mark);
		}
	} else if (response.source.value == "root_object" || response.source.value == "root_array") {
		if (reader.Field("records") != nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::UNKNOWN_FIELD, PackageDiagnosticPhase::SCHEMA,
			                         response.records.mark);
		}
	} else {
		reader.Diagnostics().Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                         response.source.mark);
	}
	return response;
}

RestPaginationDeclaration DecodeRestPaginationSchema(const SchemaReader &reader) {
	RestPaginationDeclaration pagination;
	reader.RequireMapping({"strategy", "dependency", "consistency", "target_scope", "page_size_parameter", "page_size",
	                       "page_number_parameter", "first_page", "page_increment", "max_pages_per_scan"},
	                      {"strategy"});
	pagination.strategy = reader.Text("strategy");
	pagination.dependency = reader.Text("dependency", false);
	pagination.consistency = reader.Text("consistency", false);
	pagination.target_scope = reader.Text("target_scope", false);
	pagination.page_size_parameter = reader.Text("page_size_parameter", false);
	pagination.page_size = reader.Text("page_size", false);
	pagination.page_number_parameter = reader.Text("page_number_parameter", false);
	pagination.first_page = reader.Text("first_page", false);
	pagination.page_increment = reader.Text("page_increment", false);
	pagination.max_pages_per_scan = reader.Text("max_pages_per_scan", false);
	pagination.mark = reader.Mark();
	if (pagination.strategy.value == "disabled") {
		for (const auto *name : {"dependency", "consistency", "target_scope", "page_size_parameter", "page_size",
		                         "page_number_parameter", "first_page", "page_increment", "max_pages_per_scan"}) {
			if (reader.Field(name) != nullptr) {
				reader.Diagnostics().Add(PackageDiagnosticCode::UNKNOWN_FIELD, PackageDiagnosticPhase::SCHEMA,
				                         reader.FieldMark(name));
			}
		}
		return pagination;
	}
	RequireValue(pagination.strategy, "link_next", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
	             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	for (const auto *name : {"dependency", "consistency", "target_scope", "page_size_parameter", "page_size",
	                         "page_number_parameter", "first_page", "page_increment", "max_pages_per_scan"}) {
		if (reader.Field(name) == nullptr) {
			reader.Diagnostics().Add(PackageDiagnosticCode::MISSING_FIELD, PackageDiagnosticPhase::SCHEMA,
			                         reader.FieldMark(name));
		}
	}
	RequireValue(pagination.dependency, "sequential", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
	             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	RequireValue(pagination.consistency, "mutable", PackageDiagnosticCode::UNSUPPORTED_DECLARATION,
	             PackageDiagnosticPhase::SCHEMA, reader.Diagnostics());
	RequireValue(pagination.target_scope, "exact_operation_origin_and_path", PackageDiagnosticCode::POLICY_WIDENING,
	             PackageDiagnosticPhase::COMPILE, reader.Diagnostics());
	if (!IsQueryName(pagination.page_size_parameter.value) || !IsQueryName(pagination.page_number_parameter.value)) {
		reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_TYPE, PackageDiagnosticPhase::SCHEMA, pagination.mark);
	}
	std::uint64_t ignored = 0;
	for (const auto *value :
	     {&pagination.page_size, &pagination.first_page, &pagination.page_increment, &pagination.max_pages_per_scan}) {
		if (!IsCanonicalUnsigned(*value, ignored)) {
			reader.Diagnostics().Add(PackageDiagnosticCode::INVALID_TYPE, PackageDiagnosticPhase::SCHEMA, value->mark);
		}
	}
	return pagination;
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
