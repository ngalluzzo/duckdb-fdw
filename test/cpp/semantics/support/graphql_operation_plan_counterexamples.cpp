#include "semantics/support/scan_plan_test_access.hpp"

#include <functional>
#include <utility>

namespace duckdb_api_test {

bool ScanPlanTestAccess::MutateGraphqlOperationOrSchema(duckdb_api::ScanPlan &plan,
                                                        GraphqlPlanCounterexample counterexample) {
	auto replace_operation = [&plan](const std::function<void(duckdb_api::PlannedGraphqlOperation &)> &mutate) {
		auto operation = plan.Operation().Graphql();
		mutate(operation);
		ReplaceGraphql(plan, std::move(operation));
	};
	switch (counterexample) {
	case GraphqlPlanCounterexample::OTHER_CONNECTOR_NAME:
		plan.connector_name = "other_connector";
		return true;
	case GraphqlPlanCounterexample::OTHER_CONNECTOR_VERSION:
		plan.connector_version = "other-version";
		return true;
	case GraphqlPlanCounterexample::OTHER_RELATION_NAME:
		plan.relation_name = "other_relation";
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_NAME:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.operation_name = "other_operation"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_CARDINALITY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.cardinality = duckdb_api::PlannedCardinality::EXACTLY_ONE_ON_SUCCESS;
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_REPLAY_SAFETY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.replay_safety = static_cast<duckdb_api::PlannedReplaySafety>(127);
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_OPERATION_KIND:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.kind = static_cast<duckdb_api::PlannedGraphqlOperationKind>(127);
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_DOCUMENT_IDENTITY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.document_identity = static_cast<duckdb_api::PlannedGraphqlDocumentIdentity>(127);
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_DOCUMENT:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.document.append("\n# other"); });
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_DIGEST_ALGORITHM:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.digest_algorithm = static_cast<duckdb_api::PlannedGraphqlDigestAlgorithm>(127);
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_DOCUMENT_DIGEST:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.document_digest = "other-digest"; });
		return true;
	case GraphqlPlanCounterexample::HTTP_OPERATION_ORIGIN:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.origin.scheme = duckdb_api::PlannedUrlScheme::HTTP;
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_HOST:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.origin.host = "other.example"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_PORT:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) { operation.origin.port++; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_PATH:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) { operation.path = "/other"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_HEADER_NAME:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.headers.at(0).name = "X-Other"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_HEADER_VALUE:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.headers.at(0).value = "other"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_VARIABLE_NAME:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.variables.at(0).name = "other"; });
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_VARIABLE_TYPE:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.variables.at(0).type = static_cast<duckdb_api::PlannedGraphqlVariableType>(127);
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_VARIABLE_SOURCE:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.variables.at(0).source = static_cast<duckdb_api::PlannedGraphqlVariableSource>(127);
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_VARIABLE_INTEGER_VALUE:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.variables.at(0).integer_value++; });
		return true;
	case GraphqlPlanCounterexample::OTHER_RESULT_NAME:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.result_columns.at(0).name = "other"; });
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_RESULT_SCALAR:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.result_columns.at(0).scalar_kind = static_cast<duckdb_api::PlannedGraphqlScalarKind>(127);
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_RESULT_NULLABILITY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.result_columns.at(0).nullable = !operation.result_columns.at(0).nullable;
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_RESULT_PATH:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.result_columns.at(0).response_path.segments.at(0) = "other";
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_RESPONSE_NODES_PATH:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.response.nodes.segments.back() = "other"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_RESPONSE_ERRORS_PATH:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.response.errors.segments.back() = "other";
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_RESPONSE_PAGE_INFO_PATH:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.response.page_info.segments.back() = "other";
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_PARTIAL_DATA_POLICY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.response.partial_data = static_cast<duckdb_api::PlannedGraphqlPartialDataPolicy>(127);
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_OPERATION_CURSOR_DIRECTION:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.cursor.direction = static_cast<duckdb_api::PlannedGraphqlCursorDirection>(127);
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_OPERATION_CURSOR_DEPENDENCY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.cursor.dependency = static_cast<duckdb_api::PlannedGraphqlCursorDependency>(127);
		});
		return true;
	case GraphqlPlanCounterexample::UNKNOWN_OPERATION_CURSOR_CONSISTENCY:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.cursor.consistency = static_cast<duckdb_api::PlannedGraphqlCursorConsistency>(127);
		});
		return true;
	case GraphqlPlanCounterexample::OPERATION_CURSOR_SUPPORTS_TOTAL:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.supports_total = true; });
		return true;
	case GraphqlPlanCounterexample::OPERATION_CURSOR_SUPPORTS_RESUME:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.supports_resume = true; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_CURSOR_CONCURRENCY:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.max_concurrent_pages++; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_PAGE_SIZE_VARIABLE:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.page_size_variable = "other"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_CURSOR_PAGE_SIZE:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.page_size++; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_CURSOR_VARIABLE:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.cursor_variable = "other"; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_HAS_NEXT_PAGE_PATH:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.cursor.has_next_page.segments.back() = "other";
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_END_CURSOR_PATH:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.cursor.end_cursor.segments.back() = "other";
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_MAX_PAGES:
		replace_operation(
		    [](duckdb_api::PlannedGraphqlOperation &operation) { operation.cursor.max_pages_per_scan++; });
		return true;
	case GraphqlPlanCounterexample::OTHER_MAX_DOCUMENT_BYTES:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) { operation.max_document_bytes++; });
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_PAGE_BODY_BUDGET:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.max_serialized_request_body_bytes_per_request++;
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_OPERATION_SCAN_BODY_BUDGET:
		replace_operation([](duckdb_api::PlannedGraphqlOperation &operation) {
			operation.max_serialized_request_body_bytes_per_scan++;
		});
		return true;
	case GraphqlPlanCounterexample::OTHER_OUTPUT_NAME:
		plan.output_columns.at(0).name = "other";
		return true;
	case GraphqlPlanCounterexample::OTHER_OUTPUT_LOGICAL_TYPE:
		plan.output_columns.at(0).logical_type = "OTHER";
		return true;
	case GraphqlPlanCounterexample::OTHER_OUTPUT_NULLABILITY:
		plan.output_columns.at(0).nullable = !plan.output_columns.at(0).nullable;
		return true;
	case GraphqlPlanCounterexample::OTHER_OUTPUT_EXTRACTOR:
		plan.output_columns.at(0).extractor = "$.other";
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_api_test
