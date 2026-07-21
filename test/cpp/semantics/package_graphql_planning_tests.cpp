#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "connector/support/catalog_test_access.hpp"
#include "connector/support/package_compiler_test_fixtures.hpp"
#include "duckdb_api/package_bound_scan_planner.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "query/support/live_scan_request.hpp"
#include "graphql_generator_recipe_planner.hpp"
#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace duckdb_api_test {

class GraphqlGeneratorRecipePlannerTestAccess {
public:
	static std::size_t CopyLiteralNodeCount(const duckdb_api::CompiledGraphqlLiteral &source) {
		std::size_t nodes = 0;
		(void)duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::CopyLiteral(source, 1, nodes);
		return nodes;
	}
};

namespace graphql_semantics {
namespace {

void RequireSamePath(const duckdb_api::PlannedGraphqlResponsePath &left,
                     const duckdb_api::PlannedGraphqlResponsePath &right, const std::string &fact) {
	Require(left.segments == right.segments, "package/native GraphQL " + fact + " path drifted");
}

void RequireRejected(const std::string &absolute_repository_root,
                     RepositoryGithubGraphqlCounterexample counterexample) {
	const auto connector = CompileRepositoryGithubGraphqlCounterexample(absolute_repository_root, counterexample);
	const auto request =
	    BuildAuthenticatedScanRequest(connector, "viewer_repository_metrics", "package_graphql_counterexample_secret");
	bool rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(connector, request);
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == duckdb_api::PlanningErrorCode::INVALID_CONTRACT;
	}
	Require(rejected, "package GraphQL counterexample produced a partial ScanPlan");
}

void RequireBoundaryAccepted(const std::string &absolute_repository_root, RepositoryGithubGraphqlBoundary boundary) {
	const auto connector = CompileRepositoryGithubGraphqlBoundary(absolute_repository_root, boundary);
	const auto request =
	    BuildAuthenticatedScanRequest(connector, "viewer_repository_metrics", "package_graphql_boundary_secret");
	(void)duckdb_api::BuildConservativeScanPlan(connector, request);
}

void RequireRecipeRejected(const duckdb_api::CompiledGraphqlQueryRecipe &recipe, std::uint64_t rendered_bytes,
                           const std::string &fact) {
	bool rejected = false;
	try {
		(void)duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::Plan(recipe, rendered_bytes);
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "package GraphQL recipe planner accepted " + fact);
}

void TestRecipeCopyAndRenderBudgets(const std::string &absolute_repository_root) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	const auto *relation = generation.Connector().FindRelation("viewer_repository_metrics");
	Require(relation != nullptr && relation->Operations().size() == 1,
	        "recipe budget fixture lost the repository GraphQL operation");
	const auto &recipe = relation->Operations()[0].Graphql().QueryRecipe();
	const auto baseline = duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::Plan(recipe, 65536);
	Require(!baseline.rendered_document.empty(), "recipe budget fixture did not render its baseline document");
	const auto exact_render = duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::Plan(
	    recipe, baseline.rendered_document.size());
	Require(exact_render.rendered_document == baseline.rendered_document,
	        "recipe renderer rejected its exact byte budget");
	RequireRecipeRejected(recipe, baseline.rendered_document.size() - 1, "a one-byte-short rendered budget");

	const auto exact_depth = ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
	    recipe, ConnectorCatalogTestAccess::NestedGraphqlList(31));
	(void)duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::Plan(exact_depth, 65536);
	const auto excessive_depth = ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
	    recipe, ConnectorCatalogTestAccess::NestedGraphqlList(32));
	RequireRecipeRejected(excessive_depth, 65536, "a literal one level beyond the depth budget");

	const auto exact_list = ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
	    recipe, ConnectorCatalogTestAccess::FlatGraphqlNullList(4096));
	(void)duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::Plan(exact_list, 65536);
	const auto excessive_list = ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
	    recipe, ConnectorCatalogTestAccess::FlatGraphqlNullList(4097));
	RequireRecipeRejected(excessive_list, 65536, "a literal one node beyond the list allocation budget");

	const auto exact_nodes = ConnectorCatalogTestAccess::GraphqlLiteralNodeTree(100000);
	Require(GraphqlGeneratorRecipePlannerTestAccess::CopyLiteralNodeCount(exact_nodes) == 100000,
	        "recipe literal copier rejected its exact node budget");
	bool excessive_nodes_rejected = false;
	try {
		const auto excessive_nodes = ConnectorCatalogTestAccess::GraphqlLiteralNodeTree(100001);
		(void)GraphqlGeneratorRecipePlannerTestAccess::CopyLiteralNodeCount(excessive_nodes);
	} catch (const std::logic_error &) {
		excessive_nodes_rejected = true;
	}
	Require(excessive_nodes_rejected, "recipe literal copier accepted one node beyond its budget");

	const char *accepted_integers[] = {"9223372036854775807", "-9223372036854775808"};
	for (const auto *integer : accepted_integers) {
		const auto accepted = ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::RawGraphqlInteger(integer));
		(void)duckdb_api::scan_planner_internal::GraphqlGeneratorRecipePlanner::Plan(accepted, 65536);
	}
	const char *rejected_integers[] = {"9223372036854775808", "-9223372036854775809"};
	for (const auto *integer : rejected_integers) {
		const auto rejected = ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::RawGraphqlInteger(integer));
		RequireRecipeRejected(rejected, 65536, std::string("signed 64-bit overflow ") + integer);
	}
}

} // namespace

void TestPackageGraphqlPlanning(const std::string &absolute_repository_root) {
	TestRecipeCopyAndRenderBudgets(absolute_repository_root);
	const auto non_github_generation = CompileNonGithubGraphqlGenerationFixture(absolute_repository_root);
	const auto non_github_registration = non_github_generation.QueryRegistration();
	const duckdb_api::PackageBoundScanPlanningService non_github_planning(non_github_generation);
	auto non_github_request = duckdb_api::BuildConservativeScanRequest(
	    non_github_generation.Connector(), "regional_events", duckdb_api::LogicalSecretReference());
	non_github_request.explicit_inputs =
	    duckdb_api::ExplicitInputs({duckdb_api::ExplicitInput::Varchar("region", "north")});
	const auto non_github = non_github_planning.Plan(non_github_registration.GenerationHandle(), non_github_request);
	const auto &non_github_operation = non_github.Operation().Graphql();
	const auto &non_github_page = non_github.Pagination().PageBudgets();
	const auto &non_github_scan = non_github.Pagination().ScanBudgets();
	Require(non_github.ConnectorName() == "acme_events" && non_github.RelationName() == "regional_events" &&
	            non_github_operation.operation_name == "regional_event_graph" &&
	            non_github_operation.origin.host == "api.example.com" && non_github_operation.origin.port == 8443 &&
	            non_github.Network().allowed_schemes == std::vector<std::string>({"https"}) &&
	            non_github.Network().allowed_hosts == std::vector<std::string>({"api.example.com"}) &&
	            non_github.Network().port == 8443 && non_github_operation.path == "/v1/graphql-events" &&
	            non_github_operation.generator_recipe != nullptr &&
	            non_github_operation.generator_recipe->OperationName() == "AcmeRegionalEvents" &&
	            non_github_operation.generator_recipe->PageInfoField() == "pagination" &&
	            non_github_operation.generator_recipe->HasNextPageField() == "more" &&
	            non_github_operation.generator_recipe->EndCursorField() == "next",
	        "non-GitHub required-input GraphQL candidate did not retain its package-defined authority");
	Require(non_github.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            non_github.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "REST-owned predicate mappings leaked into the selected non-GitHub GraphQL operation");
	Require(non_github_page.response_bytes == duckdb_api::PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE &&
	            non_github_page.decoded_records == duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE &&
	            non_github_page.extracted_string_bytes == duckdb_api::PAGINATION_MAX_EXTRACTED_STRING_BYTES &&
	            non_github_page.serialized_request_body_bytes == duckdb_api::HOST_MAX_SERIALIZED_REQUEST_BODY_BYTES &&
	            non_github_scan.response_bytes == 48ULL * 1024ULL * 1024ULL && non_github_scan.decoded_records == 750 &&
	            non_github_scan.extracted_string_bytes == duckdb_api::PAGINATION_MAX_EXTRACTED_STRING_BYTES &&
	            non_github_scan.serialized_request_body_bytes == 98304,
	        "non-GitHub author resource declarations were not intersected with host policy");

	const auto package =
	    BuildRepositoryGithubPackageGraphqlPlan(absolute_repository_root, "repository_package_graphql_secret");
	const auto native = BuildProductionPlan();
	const auto &planned = package.Operation().Graphql();
	const auto &native_operation = native.Operation().Graphql();
	const auto &package_page = package.Pagination().PageBudgets();
	const auto &package_scan = package.Pagination().ScanBudgets();

	Require(package.ConnectorName() == "github" && package.ConnectorVersion() == "1.0.0" &&
	            package.RelationName() == "viewer_repository_metrics" &&
	            package.Domain() == duckdb_api::BaseDomain::GRAPHQL_RELAY_CONNECTION_NODE_OCCURRENCES &&
	            planned.document_identity == duckdb_api::PlannedGraphqlDocumentIdentity::PACKAGE_GENERATED_V1 &&
	            planned.generator_recipe != nullptr,
	        "real repository package did not produce a package-owned immutable GraphQL plan");
	Require(native.Domain() == duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
	            native_operation.document_identity ==
	                duckdb_api::PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 &&
	            native_operation.generator_recipe == nullptr,
	        "package planning changed the native GraphQL identity or attached package authority");

	Require(
	    planned.operation_name == native_operation.operation_name && planned.document == native_operation.document &&
	        planned.document_digest == native_operation.document_digest &&
	        planned.digest_algorithm == native_operation.digest_algorithm && planned.origin.host == "api.github.com" &&
	        planned.origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS && planned.origin.port == 443 &&
	        package.Network().port == 443 && native.Network().port == 443 && planned.path == "/graphql" &&
	        planned.variables.size() == native_operation.variables.size() &&
	        planned.result_columns.size() == native_operation.result_columns.size(),
	    "real package and native GraphQL plans lost their exact accepted protocol differential");
	for (std::size_t index = 0; index < planned.variables.size(); index++) {
		Require(planned.variables[index].name == native_operation.variables[index].name &&
		            planned.variables[index].type == native_operation.variables[index].type &&
		            planned.variables[index].source == native_operation.variables[index].source &&
		            planned.variables[index].integer_value == native_operation.variables[index].integer_value,
		        "package/native GraphQL variable differential drifted");
	}
	for (std::size_t index = 0; index < planned.result_columns.size(); index++) {
		const auto &left = planned.result_columns[index];
		const auto &right = native_operation.result_columns[index];
		Require(left.name == right.name && left.scalar_kind == right.scalar_kind && left.nullable == right.nullable &&
		            left.response_path.segments == right.response_path.segments,
		        "package/native GraphQL result-column differential drifted");
	}
	RequireSamePath(planned.response.nodes, native_operation.response.nodes, "nodes");
	RequireSamePath(planned.response.errors, native_operation.response.errors, "errors");
	RequireSamePath(planned.response.page_info, native_operation.response.page_info, "pageInfo");
	RequireSamePath(planned.cursor.has_next_page, native_operation.cursor.has_next_page, "hasNextPage");
	RequireSamePath(planned.cursor.end_cursor, native_operation.cursor.end_cursor, "endCursor");
	Require(planned.cursor.page_size_variable == native_operation.cursor.page_size_variable &&
	            planned.cursor.page_size == native_operation.cursor.page_size &&
	            planned.cursor.cursor_variable == native_operation.cursor.cursor_variable &&
	            planned.cursor.max_pages_per_scan == native_operation.cursor.max_pages_per_scan,
	        "package/native GraphQL cursor differential drifted");
	Require(package_scan.decoded_records == package_page.decoded_records * planned.cursor.max_pages_per_scan,
	        "package GraphQL record scan budget lost its exact accepted page product");

	const auto &recipe = *planned.generator_recipe;
	Require(recipe.Identity() == duckdb_api::PlannedGraphqlGeneratorIdentity::PACKAGE_QUERY_GENERATOR_V1 &&
	            recipe.OperationName() == "DuckdbApiViewerRepositoryMetrics" && recipe.RootPath().size() == 2 &&
	            recipe.RootPath()[0] == "viewer" && recipe.RootPath()[1] == "repositories" &&
	            recipe.Variables().size() == 2 && recipe.Variables()[0].Name() == "pageSize" &&
	            recipe.Variables()[0].ArgumentName() == "first" && recipe.Variables()[1].Name() == "cursor" &&
	            recipe.Variables()[1].ArgumentName() == "after" && recipe.FixedArguments().size() == 3 &&
	            recipe.Selections().size() == 8 && recipe.NodesField() == "nodes" &&
	            recipe.PageInfoField() == "pageInfo" && recipe.HasNextPageField() == "hasNextPage" &&
	            recipe.EndCursorField() == "endCursor",
	        "package GraphQL plan lost field-complete immutable generator authority");

	const auto count = static_cast<std::size_t>(RepositoryGithubGraphqlCounterexample::COUNT);
	Require(count == 23, "closed package GraphQL planning counterexample catalog changed without review");
	for (std::size_t value = 0; value < count; value++) {
		RequireRejected(absolute_repository_root, static_cast<RepositoryGithubGraphqlCounterexample>(value));
	}
	bool sentinel_rejected = false;
	try {
		(void)CompileRepositoryGithubGraphqlCounterexample(absolute_repository_root,
		                                                   RepositoryGithubGraphqlCounterexample::COUNT);
	} catch (const std::invalid_argument &) {
		sentinel_rejected = true;
	}
	Require(sentinel_rejected, "package GraphQL counterexample fixture accepted its sentinel");

	const auto boundary_count = static_cast<std::size_t>(RepositoryGithubGraphqlBoundary::COUNT);
	Require(boundary_count == 4, "closed package GraphQL boundary catalog changed without review");
	for (std::size_t value = 0; value < boundary_count; value++) {
		RequireBoundaryAccepted(absolute_repository_root, static_cast<RepositoryGithubGraphqlBoundary>(value));
	}
	bool boundary_sentinel_rejected = false;
	try {
		(void)CompileRepositoryGithubGraphqlBoundary(absolute_repository_root, RepositoryGithubGraphqlBoundary::COUNT);
	} catch (const std::invalid_argument &) {
		boundary_sentinel_rejected = true;
	}
	Require(boundary_sentinel_rejected, "package GraphQL boundary fixture accepted its sentinel");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
