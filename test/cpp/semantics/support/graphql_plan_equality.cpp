#include "semantics/support/graphql_plan_equality.hpp"

#include <stdexcept>

namespace duckdb_api_test {
namespace {

template <class VALUE>
void CountValue(std::size_t &count, const VALUE &left, const VALUE &right) {
	count += left != right ? 1 : 0;
}

void CountOrigin(std::size_t &count, const duckdb_api::PlannedHttpOrigin &left,
                 const duckdb_api::PlannedHttpOrigin &right) {
	CountValue(count, left.scheme, right.scheme);
	CountValue(count, left.host, right.host);
	CountValue(count, left.port, right.port);
}

void CountPath(std::size_t &count, const duckdb_api::PlannedGraphqlResponsePath &left,
               const duckdb_api::PlannedGraphqlResponsePath &right) {
	CountValue(count, left.segments, right.segments);
}

bool HeadersEqual(const std::vector<duckdb_api::PlannedHttpHeader> &left,
                  const std::vector<duckdb_api::PlannedHttpHeader> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].value != right[index].value) {
			return false;
		}
	}
	return true;
}

bool VariablesEqual(const std::vector<duckdb_api::PlannedGraphqlVariable> &left,
                    const std::vector<duckdb_api::PlannedGraphqlVariable> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].type != right[index].type ||
		    left[index].source != right[index].source || left[index].integer_value != right[index].integer_value) {
			return false;
		}
	}
	return true;
}

bool ResultColumnsEqual(const std::vector<duckdb_api::PlannedGraphqlResultColumn> &left,
                        const std::vector<duckdb_api::PlannedGraphqlResultColumn> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].scalar_kind != right[index].scalar_kind ||
		    left[index].nullable != right[index].nullable ||
		    left[index].response_path.segments != right[index].response_path.segments) {
			return false;
		}
	}
	return true;
}

bool LiteralsEqual(const duckdb_api::PlannedGraphqlLiteral &left, const duckdb_api::PlannedGraphqlLiteral &right) {
	if (left.Kind() != right.Kind() || left.Scalar() != right.Scalar() || left.Items().size() != right.Items().size() ||
	    left.Fields().size() != right.Fields().size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.Items().size(); index++) {
		if (!left.Items()[index] || !right.Items()[index] ||
		    !LiteralsEqual(*left.Items()[index], *right.Items()[index])) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left.Fields().size(); index++) {
		if (left.Fields()[index].Name() != right.Fields()[index].Name() ||
		    !LiteralsEqual(left.Fields()[index].Value(), right.Fields()[index].Value())) {
			return false;
		}
	}
	return true;
}

bool GeneratorRecipesEqual(const std::shared_ptr<const duckdb_api::PlannedGraphqlGeneratorRecipe> &left,
                           const std::shared_ptr<const duckdb_api::PlannedGraphqlGeneratorRecipe> &right) {
	if (!left || !right) {
		return !left && !right;
	}
	if (left->Identity() != right->Identity() || left->OperationName() != right->OperationName() ||
	    left->RootPath() != right->RootPath() || left->NodesField() != right->NodesField() ||
	    left->PageInfoField() != right->PageInfoField() || left->HasNextPageField() != right->HasNextPageField() ||
	    left->EndCursorField() != right->EndCursorField() || left->Variables().size() != right->Variables().size() ||
	    left->FixedArguments().size() != right->FixedArguments().size() ||
	    left->Selections().size() != right->Selections().size()) {
		return false;
	}
	for (std::size_t index = 0; index < left->Variables().size(); index++) {
		const auto &left_variable = left->Variables()[index];
		const auto &right_variable = right->Variables()[index];
		if (left_variable.Name() != right_variable.Name() || left_variable.Type() != right_variable.Type() ||
		    left_variable.Role() != right_variable.Role() ||
		    left_variable.ArgumentName() != right_variable.ArgumentName()) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left->FixedArguments().size(); index++) {
		if (left->FixedArguments()[index].Name() != right->FixedArguments()[index].Name() ||
		    !LiteralsEqual(left->FixedArguments()[index].Value(), right->FixedArguments()[index].Value())) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left->Selections().size(); index++) {
		if (left->Selections()[index].ColumnName() != right->Selections()[index].ColumnName() ||
		    left->Selections()[index].FieldPath() != right->Selections()[index].FieldPath()) {
			return false;
		}
	}
	return true;
}

bool OutputColumnsEqual(const std::vector<duckdb_api::PlannedColumn> &left,
                        const std::vector<duckdb_api::PlannedColumn> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].logical_type != right[index].logical_type ||
		    left[index].nullable != right[index].nullable || left[index].extractor != right[index].extractor) {
			return false;
		}
	}
	return true;
}

void CountCursor(std::size_t &count, const duckdb_api::PlannedGraphqlCursor &left,
                 const duckdb_api::PlannedGraphqlCursor &right) {
	CountValue(count, left.direction, right.direction);
	CountValue(count, left.dependency, right.dependency);
	CountValue(count, left.consistency, right.consistency);
	CountValue(count, left.supports_total, right.supports_total);
	CountValue(count, left.supports_resume, right.supports_resume);
	CountValue(count, left.max_concurrent_pages, right.max_concurrent_pages);
	CountValue(count, left.page_size_variable, right.page_size_variable);
	CountValue(count, left.page_size, right.page_size);
	CountValue(count, left.cursor_variable, right.cursor_variable);
	CountPath(count, left.has_next_page, right.has_next_page);
	CountPath(count, left.end_cursor, right.end_cursor);
	CountValue(count, left.max_pages_per_scan, right.max_pages_per_scan);
}

void CountOperation(std::size_t &count, const duckdb_api::PlannedGraphqlOperation &left,
                    const duckdb_api::PlannedGraphqlOperation &right) {
	CountValue(count, left.operation_name, right.operation_name);
	CountValue(count, left.cardinality, right.cardinality);
	CountValue(count, left.replay_safety, right.replay_safety);
	CountValue(count, left.kind, right.kind);
	CountValue(count, left.document_identity, right.document_identity);
	CountValue(count, left.document, right.document);
	CountValue(count, left.digest_algorithm, right.digest_algorithm);
	CountValue(count, left.document_digest, right.document_digest);
	CountOrigin(count, left.origin, right.origin);
	CountValue(count, left.path, right.path);
	CountValue(count, HeadersEqual(left.headers, right.headers), true);
	CountValue(count, VariablesEqual(left.variables, right.variables), true);
	CountValue(count, ResultColumnsEqual(left.result_columns, right.result_columns), true);
	CountPath(count, left.response.nodes, right.response.nodes);
	CountPath(count, left.response.errors, right.response.errors);
	CountPath(count, left.response.page_info, right.response.page_info);
	CountValue(count, left.response.partial_data, right.response.partial_data);
	CountCursor(count, left.cursor, right.cursor);
	CountValue(count, left.max_document_bytes, right.max_document_bytes);
	CountValue(count, left.max_serialized_request_body_bytes_per_request,
	           right.max_serialized_request_body_bytes_per_request);
	CountValue(count, left.max_serialized_request_body_bytes_per_scan,
	           right.max_serialized_request_body_bytes_per_scan);
	CountValue(count, GeneratorRecipesEqual(left.generator_recipe, right.generator_recipe), true);
}

void CountPageBudgets(std::size_t &count, const duckdb_api::ResourceBudgets &left,
                      const duckdb_api::ResourceBudgets &right) {
	CountValue(count, left.request_attempts, right.request_attempts);
	CountValue(count, left.response_bytes, right.response_bytes);
	CountValue(count, left.header_bytes, right.header_bytes);
	CountValue(count, left.decompressed_bytes, right.decompressed_bytes);
	CountValue(count, left.decoded_records, right.decoded_records);
	CountValue(count, left.extracted_string_bytes, right.extracted_string_bytes);
	CountValue(count, left.json_nesting, right.json_nesting);
	CountValue(count, left.decoded_memory_bytes, right.decoded_memory_bytes);
	CountValue(count, left.batch_rows, right.batch_rows);
	CountValue(count, left.wall_milliseconds, right.wall_milliseconds);
	CountValue(count, left.concurrency, right.concurrency);
	CountValue(count, left.serialized_request_body_bytes, right.serialized_request_body_bytes);
}

void CountScanBudgets(std::size_t &count, const duckdb_api::ScanResourceBudgets &left,
                      const duckdb_api::ScanResourceBudgets &right) {
	CountValue(count, left.request_attempts, right.request_attempts);
	CountValue(count, left.pages, right.pages);
	CountValue(count, left.response_bytes, right.response_bytes);
	CountValue(count, left.header_bytes, right.header_bytes);
	CountValue(count, left.decompressed_bytes, right.decompressed_bytes);
	CountValue(count, left.decoded_records, right.decoded_records);
	CountValue(count, left.extracted_string_bytes, right.extracted_string_bytes);
	CountValue(count, left.json_nesting, right.json_nesting);
	CountValue(count, left.decoded_memory_bytes, right.decoded_memory_bytes);
	CountValue(count, left.batch_rows, right.batch_rows);
	CountValue(count, left.wall_milliseconds, right.wall_milliseconds);
	CountValue(count, left.concurrency, right.concurrency);
	CountValue(count, left.serialized_request_body_bytes, right.serialized_request_body_bytes);
}

} // namespace

std::size_t CountGraphqlPlanDifferences(const duckdb_api::ScanPlan &left, const duckdb_api::ScanPlan &right) {
	std::size_t count = 0;
	CountValue(count, left.ConnectorName(), right.ConnectorName());
	CountValue(count, left.ConnectorVersion(), right.ConnectorVersion());
	CountValue(count, left.RelationName(), right.RelationName());
	CountValue(count, left.Domain(), right.Domain());
	CountValue(count, left.Operation().Protocol(), right.Operation().Protocol());
	if (left.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL &&
	    right.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL) {
		const duckdb_api::PlannedGraphqlOperation *left_graphql = nullptr;
		const duckdb_api::PlannedGraphqlOperation *right_graphql = nullptr;
		try {
			left_graphql = &left.Operation().Graphql();
		} catch (const std::logic_error &) {
		}
		try {
			right_graphql = &right.Operation().Graphql();
		} catch (const std::logic_error &) {
		}
		CountValue(count, left_graphql != nullptr, right_graphql != nullptr);
		if (left_graphql != nullptr && right_graphql != nullptr) {
			CountOperation(count, *left_graphql, *right_graphql);
		}
	}
	CountValue(count, OutputColumnsEqual(left.OutputColumns(), right.OutputColumns()), true);
	CountValue(count, left.RemotePredicate(), right.RemotePredicate());
	CountValue(count, left.RemoteAccuracy(), right.RemoteAccuracy());
	CountValue(count, left.ResidualPredicate(), right.ResidualPredicate());
	CountValue(count, left.ResidualOwner(), right.ResidualOwner());
	CountValue(count, left.ConditionalInput(), right.ConditionalInput());
	CountValue(count, left.PredicateCategory(), right.PredicateCategory());
	CountValue(count, left.PredicateReason(), right.PredicateReason());
	CountValue(count, left.Ownership().filter, right.Ownership().filter);
	CountValue(count, left.Ownership().projection, right.Ownership().projection);
	CountValue(count, left.Ownership().ordering, right.Ownership().ordering);
	CountValue(count, left.Ownership().limit, right.Ownership().limit);
	CountValue(count, left.Ownership().offset, right.Ownership().offset);
	CountValue(count, left.RemoteOrdering(), right.RemoteOrdering());
	CountValue(count, left.RuntimeOrdering(), right.RuntimeOrdering());
	CountValue(count, left.RemoteLimit(), right.RemoteLimit());
	CountValue(count, left.RemoteOffset(), right.RemoteOffset());
	CountValue(count, left.RuntimeLimit(), right.RuntimeLimit());
	CountValue(count, left.RuntimeOffset(), right.RuntimeOffset());
	CountValue(count, left.Pagination().Strategy(), right.Pagination().Strategy());
	if (left.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR &&
	    right.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR) {
		CountCursor(count, left.Pagination().GraphqlCursor(), right.Pagination().GraphqlCursor());
		CountPageBudgets(count, left.Pagination().PageBudgets(), right.Pagination().PageBudgets());
		CountScanBudgets(count, left.Pagination().ScanBudgets(), right.Pagination().ScanBudgets());
	}
	CountValue(count, left.Providers(), right.Providers());
	CountValue(count, left.Retry(), right.Retry());
	CountValue(count, left.Cache(), right.Cache());
	CountValue(count, left.Authentication(), right.Authentication());
	CountValue(count, left.SecretReference().IsPresent(), right.SecretReference().IsPresent());
	if (left.SecretReference().IsPresent() && right.SecretReference().IsPresent()) {
		CountValue(count, left.SecretReference().Name(), right.SecretReference().Name());
	}
	const auto &left_auth = left.AuthenticationObligation();
	const auto &right_auth = right.AuthenticationObligation();
	CountValue(count, left_auth.Requirement(), right_auth.Requirement());
	CountValue(count, left_auth.LogicalCredential(), right_auth.LogicalCredential());
	CountValue(count, left_auth.Authenticator(), right_auth.Authenticator());
	CountValue(count, left_auth.Placement(), right_auth.Placement());
	CountValue(count, left_auth.Destination() != nullptr, right_auth.Destination() != nullptr);
	if (left_auth.Destination() != nullptr && right_auth.Destination() != nullptr) {
		CountOrigin(count, *left_auth.Destination(), *right_auth.Destination());
	}
	CountValue(count, left.Network().allowed_schemes, right.Network().allowed_schemes);
	CountValue(count, left.Network().allowed_hosts, right.Network().allowed_hosts);
	CountValue(count, left.Network().port, right.Network().port);
	CountValue(count, left.Network().redirects_enabled, right.Network().redirects_enabled);
	CountValue(count, left.Network().private_addresses_enabled, right.Network().private_addresses_enabled);
	CountValue(count, left.Network().link_local_addresses_enabled, right.Network().link_local_addresses_enabled);
	CountValue(count, left.Network().loopback_addresses_enabled, right.Network().loopback_addresses_enabled);
	// Budgets() is the public alias of PageBudgets() for an enabled GraphQL
	// pagination plan, so counting it again would count one leaf twice.
	CountValue(count, left.ClassificationReason(), right.ClassificationReason());
	return count;
}

} // namespace duckdb_api_test
