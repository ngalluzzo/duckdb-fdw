#include "package_model_compiler_internal.hpp"

#include "duckdb_api/content_digest.hpp"

#include <limits>
#include <set>

namespace duckdb_api {
namespace connector {
namespace internal {

namespace {

const InputDeclaration *FindInput(const RelationDeclaration &relation, const std::string &id) {
	for (const auto &input : relation.inputs) {
		if (input.id.value == id) {
			return &input;
		}
	}
	return nullptr;
}

bool PredicateTargets(const RelationDeclaration &relation, const std::string &operation,
                      const std::string &conditional) {
	for (const auto &predicate : relation.predicates) {
		if (predicate.conditional_input.value != conditional) {
			continue;
		}
		for (const auto &target : predicate.operations) {
			if (target.value == operation) {
				return true;
			}
		}
	}
	return false;
}

bool FitsBigintPageSequence(const RestPaginationDeclaration &pagination) {
	if (pagination.strategy.value != "link_next" && pagination.strategy.value != "response_next") {
		return true;
	}
	const auto first = ParseUnsigned(pagination.first_page);
	const auto increment = ParseUnsigned(pagination.page_increment);
	const auto pages = ParseUnsigned(pagination.max_pages_per_scan);
	const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	return pagination.page_size_parameter.value != pagination.page_number_parameter.value &&
	       ParseUnsigned(pagination.page_size) <= maximum && first <= maximum && pages > 0 && increment > 0 &&
	       pages - 1 <= (maximum - first) / increment;
}

bool CompileSelector(const RelationDeclaration &relation, const OperationDeclaration &operation,
                     PackageDiagnosticSink &diagnostics, std::unique_ptr<CompiledOperationSelector> &selector) {
	const auto revision = diagnostics.Revision();
	std::vector<CompiledRequiredInputReference> references;
	std::set<std::string> identities;
	for (const auto &source : operation.selector.required_inputs) {
		const auto dot = source.value.find('.');
		const auto kind = dot == std::string::npos ? std::string() : source.value.substr(0, dot);
		const auto id = dot == std::string::npos ? std::string() : source.value.substr(dot + 1);
		if (!identities.insert(source.value).second || !IsIdentifier(id)) {
			diagnostics.Add(PackageDiagnosticCode::INVALID_SELECTOR, PackageDiagnosticPhase::COMPILE, source.mark, "",
			                relation.id.value, operation.id.value);
			continue;
		}
		if (kind == "input") {
			if (FindInput(relation, id) == nullptr) {
				diagnostics.Add(PackageDiagnosticCode::INVALID_REFERENCE, PackageDiagnosticPhase::REFERENCE,
				                source.mark, "", relation.id.value, operation.id.value);
				continue;
			}
			references.push_back(duckdb_api::internal::CompiledModelBuilder::RelationInputReference(id));
		} else if (kind == "conditional") {
			if (!PredicateTargets(relation, operation.id.value, id)) {
				diagnostics.Add(PackageDiagnosticCode::INVALID_REFERENCE, PackageDiagnosticPhase::REFERENCE,
				                source.mark, "", relation.id.value, operation.id.value);
				continue;
			}
			references.push_back(duckdb_api::internal::CompiledModelBuilder::ConditionalInputReference(id));
		} else {
			diagnostics.Add(PackageDiagnosticCode::INVALID_SELECTOR, PackageDiagnosticPhase::COMPILE, source.mark, "",
			                relation.id.value, operation.id.value);
		}
	}
	selector.reset(new CompiledOperationSelector(
	    duckdb_api::internal::CompiledModelBuilder::V1OperationSelector(std::move(references))));
	return diagnostics.Revision() == revision;
}

CompiledPagination CompilePagination(const RestPaginationDeclaration &source) {
	const bool has_page_size = !source.page_size_parameter.value.empty();
	const auto page_size = has_page_size ? ParseUnsigned(source.page_size) : 0;
	if (source.strategy.value == "link_next") {
		return duckdb_api::internal::CompiledModelBuilder::LinkPagination(
		    source.page_size_parameter.value, page_size, source.page_number_parameter.value,
		    ParseUnsigned(source.first_page), ParseUnsigned(source.page_increment),
		    ParseUnsigned(source.max_pages_per_scan));
	}
	if (source.strategy.value == "response_next") {
		return duckdb_api::internal::CompiledModelBuilder::ResponseNextPagination(
		    source.next_url_path.value, source.page_size_parameter.value, page_size, source.page_number_parameter.value,
		    ParseUnsigned(source.first_page), ParseUnsigned(source.page_increment),
		    ParseUnsigned(source.max_pages_per_scan));
	}
	return duckdb_api::internal::CompiledModelBuilder::DisabledPagination();
}

std::vector<CompiledQueryParameter> CompileQuery(const RelationDeclaration &relation,
                                                 const OperationDeclaration &operation,
                                                 const RestPaginationDeclaration &pagination,
                                                 PackageDiagnosticSink &diagnostics) {
	std::vector<CompiledQueryParameter> result;
	std::set<std::string> names;
	std::set<std::string> conditional_ids;
	bool found_page_number = false;
	// link_next and response_next share the same structural page-size and
	// page-number query bindings; only the continuation signal's source
	// (header vs body) differs.
	const bool paginated = pagination.strategy.value == "link_next" || pagination.strategy.value == "response_next";
	const auto page_size = ParseUnsigned(pagination.page_size);
	const auto first_page = ParseUnsigned(pagination.first_page);
	for (const auto &field : operation.rest.query) {
		if (!names.insert(field.name.value).second) {
			diagnostics.Add(PackageDiagnosticCode::DUPLICATE_ID, PackageDiagnosticPhase::SCHEMA, field.name.mark, "",
			                relation.id.value, operation.id.value);
		}
		switch (field.kind) {
		case QueryFieldKind::LITERAL:
			if (paginated && field.name.value == pagination.page_size_parameter.value) {
				if (field.source.value != pagination.page_size.value ||
				    page_size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
					diagnostics.Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::COMPILE,
					                field.mark, "", relation.id.value, operation.id.value);
					break;
				}
				result.push_back(
				    duckdb_api::internal::CompiledModelBuilder::PageSizeQueryParameter(field.name.value, page_size));
			} else if (paginated && field.name.value == pagination.page_number_parameter.value) {
				found_page_number = true;
				if (field.source.value != pagination.first_page.value ||
				    first_page > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
					diagnostics.Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::COMPILE,
					                field.mark, "", relation.id.value, operation.id.value);
					break;
				}
				result.push_back(
				    duckdb_api::internal::CompiledModelBuilder::PageNumberQueryParameter(field.name.value, first_page));
			} else {
				auto value = duckdb_api::internal::CompiledModelBuilder::Varchar(field.source.value);
				try {
					(void)EncodeCompiledQueryScalar(value);
					result.push_back(duckdb_api::internal::CompiledModelBuilder::FixedQueryParameter(field.name.value,
					                                                                                 std::move(value)));
				} catch (const std::invalid_argument &) {
					diagnostics.Add(PackageDiagnosticCode::INVALID_TYPE, PackageDiagnosticPhase::SCHEMA,
					                field.source.mark, "", relation.id.value, operation.id.value);
				}
			}
			break;
		case QueryFieldKind::INPUT:
			if (FindInput(relation, field.source.value) == nullptr) {
				diagnostics.Add(PackageDiagnosticCode::INVALID_REFERENCE, PackageDiagnosticPhase::REFERENCE,
				                field.source.mark, "", relation.id.value, operation.id.value);
			}
			result.push_back(duckdb_api::internal::CompiledModelBuilder::RelationInputQueryParameter(
			    field.name.value, field.source.value));
			break;
		case QueryFieldKind::CONDITIONAL:
			if (!conditional_ids.insert(field.source.value).second || conditional_ids.size() > 1 ||
			    !PredicateTargets(relation, operation.id.value, field.source.value)) {
				diagnostics.Add(PackageDiagnosticCode::INVALID_PREDICATE, PackageDiagnosticPhase::COMPILE,
				                field.source.mark, "", relation.id.value, operation.id.value);
			}
			result.push_back(duckdb_api::internal::CompiledModelBuilder::ConditionalInputQueryParameter(
			    field.name.value, field.source.value));
			break;
		}
	}
	if (paginated &&
	    (pagination.page_size_parameter.value == pagination.page_number_parameter.value || !found_page_number)) {
		diagnostics.Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::COMPILE,
		                pagination.mark, "", relation.id.value, operation.id.value);
	}
	return result;
}

bool CompileRestOperation(const RelationDeclaration &relation, const OperationDeclaration &source,
                          CompiledOperationSelector selector, PackageDiagnosticSink &diagnostics,
                          std::unique_ptr<CompiledOperation> &result) {
	const auto revision = diagnostics.Revision();
	const auto cardinality = source.cardinality.value == "one" ? CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS
	                                                           : CompiledOperationCardinality::ZERO_TO_MANY;
	if (!FitsBigintPageSequence(source.rest_pagination)) {
		diagnostics.Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::COMPILE,
		                source.rest_pagination.mark, "", relation.id.value, source.id.value);
		return false;
	}
	auto pagination = CompilePagination(source.rest_pagination);
	CompiledResponseSource response_source = CompiledResponseSource::ROOT_ARRAY;
	std::string extractor = "$";
	std::vector<std::string> extractor_segments;
	if (source.response.source.value == "root_object") {
		response_source = CompiledResponseSource::ROOT_OBJECT;
	} else if (source.response.source.value == "terminal_collection") {
		response_source = CompiledResponseSource::JSON_PATH_MANY;
		extractor = source.response.records.value;
		(void)IsExtractor(extractor, true, &extractor_segments);
	}
	if ((cardinality == CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS) !=
	    (response_source == CompiledResponseSource::ROOT_OBJECT)) {
		diagnostics.Add(PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                source.response.mark, "", relation.id.value, source.id.value);
		return false;
	}
	CompiledRestRequest request = {CompileOrigin(source.rest.origin), source.rest.path.value,
	                               CompileQuery(relation, source, source.rest_pagination, diagnostics),
	                               CompileHeaders(source.rest.headers, false, relation, source, diagnostics)};
	if (diagnostics.Revision() != revision) {
		return false;
	}
	result.reset(new CompiledOperation(duckdb_api::internal::CompiledModelBuilder::RestOperation(
	    source.id.value, source.selector.fallback, cardinality, std::move(pagination), std::move(request),
	    response_source, std::move(extractor), std::move(extractor_segments), std::move(selector))));
	return true;
}

bool CompileGraphqlOperation(const RelationDeclaration &relation, const OperationDeclaration &source,
                             CompiledOperationSelector selector, PackageDiagnosticSink &diagnostics,
                             std::unique_ptr<CompiledOperation> &result) {
	const auto revision = diagnostics.Revision();
	RenderedGraphqlOperation rendered;
	if (!RenderGraphqlOperation(relation, source, diagnostics, rendered)) {
		return false;
	}
	const auto &request = source.graphql_request;
	const auto &pagination = request.pagination;
	CompiledGraphqlOperation operation = {
	    CompiledGraphqlDocumentIdentity::PACKAGE_QUERY_GENERATOR_V1,
	    rendered.document,
	    CompiledGraphqlDigestAlgorithm::SHA256,
	    ComputeSha256Hex(rendered.document),
	    CompileOrigin(request.origin),
	    request.path.value,
	    CompileHeaders(request.headers, true, relation, source, diagnostics),
	    {{pagination.page_size_variable.value, CompiledGraphqlVariableType::INT_NON_NULL,
	      CompiledGraphqlVariableSource::FIXED_PAGE_SIZE, ParseUnsigned(pagination.page_size)},
	     {pagination.cursor_variable.value, CompiledGraphqlVariableType::STRING_NULLABLE,
	      CompiledGraphqlVariableSource::RUNTIME_CURSOR, 0}},
	    std::move(rendered.result_columns),
	    std::move(rendered.response),
	    std::move(rendered.cursor),
	    ParseUnsigned(request.max_document_bytes),
	    ParseUnsigned(request.max_serialized_body_bytes_per_request),
	    ParseUnsigned(request.max_serialized_body_bytes_per_scan),
	    false,
	    false,
	    false,
	    std::move(rendered.query_recipe)};
	if (diagnostics.Revision() != revision) {
		return false;
	}
	result.reset(new CompiledOperation(duckdb_api::internal::CompiledModelBuilder::GraphqlOperation(
	    source.id.value, source.selector.fallback, std::move(operation), std::move(selector))));
	return true;
}

} // namespace

bool CompileOperations(const RelationDeclaration &relation, PackageDiagnosticSink &diagnostics,
                       std::vector<CompiledOperation> &operations) {
	std::size_t fallback_count = 0;
	for (const auto &source : relation.operations) {
		const auto revision = diagnostics.Revision();
		fallback_count += source.selector.fallback ? 1 : 0;
		std::unique_ptr<CompiledOperationSelector> selector;
		const auto selector_valid = CompileSelector(relation, source, diagnostics, selector);
		std::unique_ptr<CompiledOperation> operation;
		if (selector_valid && selector && source.graphql) {
			CompileGraphqlOperation(relation, source, std::move(*selector), diagnostics, operation);
		} else if (selector_valid && selector) {
			CompileRestOperation(relation, source, std::move(*selector), diagnostics, operation);
		}
		if (operation && diagnostics.Revision() == revision) {
			operations.push_back(std::move(*operation));
		}
	}
	if (fallback_count > 1) {
		diagnostics.Add(PackageDiagnosticCode::INVALID_SELECTOR, PackageDiagnosticPhase::COMPILE, relation.mark, "",
		                relation.id.value);
	}
	return diagnostics.Empty();
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
