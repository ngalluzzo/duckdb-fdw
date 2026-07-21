#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"

#include "connector/support/package_compiler_test_fixtures.hpp"
#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/package_bound_scan_planner.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "semantics/support/scan_plan_test_access.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace duckdb_api_test {

namespace {

const char NON_GITHUB_LOGICAL_SECRET[] = "non_github_package_fixture_secret";

} // namespace

duckdb_api::ScanPlan BuildRepositoryGithubPackageGraphqlPlan(const std::string &absolute_repository_root,
                                                             const std::string &logical_secret_name) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	const auto registration = generation.QueryRegistration();
	const duckdb_api::PackageBoundScanPlanningService planning(generation);
	auto request =
	    duckdb_api::BuildConservativeScanRequest(generation.Connector(), "viewer_repository_metrics",
	                                             duckdb_api::LogicalSecretReference::Named(logical_secret_name));
	return planning.Plan(registration.GenerationHandle(), request);
}

duckdb_api::ScanPlan BuildNonGithubPackageGraphqlPlan(const std::string &absolute_repository_root) {
	const auto generation = CompileNonGithubGraphqlGenerationFixture(absolute_repository_root);
	const duckdb_api::PackageBoundScanPlanningService planning(generation);
	auto request =
	    duckdb_api::BuildConservativeScanRequest(generation.Connector(), "regional_events",
	                                             duckdb_api::LogicalSecretReference::Named(NON_GITHUB_LOGICAL_SECRET));
	request.explicit_inputs = duckdb_api::ExplicitInputs({duckdb_api::ExplicitInput::Varchar("region", "north"),
	                                                      duckdb_api::ExplicitInput::Boolean("graph_view", true)});
	return planning.Plan(generation.QueryRegistration().GenerationHandle(), request);
}

duckdb_api::ScanPlan BuildNonGithubPackageRestPlan(const std::string &absolute_repository_root) {
	const auto generation = CompileNonGithubGraphqlGenerationFixture(absolute_repository_root);
	const duckdb_api::PackageBoundScanPlanningService planning(generation);
	auto request =
	    duckdb_api::BuildConservativeScanRequest(generation.Connector(), "regional_events",
	                                             duckdb_api::LogicalSecretReference::Named(NON_GITHUB_LOGICAL_SECRET));
	return planning.Plan(generation.QueryRegistration().GenerationHandle(), request);
}

duckdb_api::ScanPlan
BuildNonGithubPackageGraphqlUnreachableBodyAuthorityCounterexample(const std::string &absolute_repository_root) {
	return ScanPlanTestAccess::PackageGraphqlUnreachableBodyAuthority(
	    BuildNonGithubPackageGraphqlPlan(absolute_repository_root));
}

duckdb_api::ScanPlan BuildRepositoryGithubPackageRestPlan(const std::string &absolute_repository_root,
                                                          const std::string &relation_name,
                                                          const std::string &logical_secret_name) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	const duckdb_api::PackageBoundScanPlanningService planning(generation);
	auto request = duckdb_api::BuildConservativeScanRequest(
	    generation.Connector(), relation_name, duckdb_api::LogicalSecretReference::Named(logical_secret_name));
	return planning.Plan(generation.QueryRegistration().GenerationHandle(), request);
}

duckdb_api::ScanPlan BuildRepositoryGithubPackageAnonymousSearchPlan(const std::string &absolute_repository_root) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	const duckdb_api::PackageBoundScanPlanningService planning(generation);
	auto request = duckdb_api::BuildConservativeScanRequest(generation.Connector(), "duckdb_login_search_page",
	                                                        duckdb_api::LogicalSecretReference());
	return planning.Plan(generation.QueryRegistration().GenerationHandle(), request);
}

namespace {

const char *NumericOrigin(PackageHttpNumericOriginCounterexample counterexample) {
	switch (counterexample) {
	case PackageHttpNumericOriginCounterexample::LOOPBACK_TWO_COMPONENT_DECIMAL:
		return "127.1";
	case PackageHttpNumericOriginCounterexample::LOOPBACK_SINGLE_DECIMAL:
		return "2130706433";
	case PackageHttpNumericOriginCounterexample::LOOPBACK_SINGLE_HEX:
		return "0x7f000001";
	case PackageHttpNumericOriginCounterexample::LOOPBACK_SINGLE_HEX_UPPERCASE:
		return "0X7F000001";
	case PackageHttpNumericOriginCounterexample::LOOPBACK_TWO_COMPONENT_HEX_UPPERCASE:
		return "0X7f.1";
	case PackageHttpNumericOriginCounterexample::PUBLIC_SINGLE_DECIMAL:
		return "134744072";
	case PackageHttpNumericOriginCounterexample::PUBLIC_SINGLE_HEX:
		return "0x08080808";
	case PackageHttpNumericOriginCounterexample::PUBLIC_MAX_HEX_UPPERCASE:
		return "0XFFFFFFFF";
	case PackageHttpNumericOriginCounterexample::PUBLIC_FOUR_COMPONENT_OCTAL:
		return "010.010.010.010";
	case PackageHttpNumericOriginCounterexample::COUNT:
		break;
	}
	throw std::invalid_argument("unknown package numeric-origin counterexample");
}

} // namespace

duckdb_api::ScanPlan ScanPlanTestAccess::PackageGraphqlUnreachableBodyAuthority(duckdb_api::ScanPlan plan) {
	if (plan.Operation().Protocol() != duckdb_api::PlannedProtocol::GRAPHQL ||
	    plan.Pagination().Strategy() != duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR) {
		throw std::invalid_argument("unreachable body authority counterexample requires a cursor GraphQL plan");
	}
	auto &scan = plan.pagination.scan_budgets;
	const auto page_body = plan.pagination.page_budgets.serialized_request_body_bytes;
	const auto max_pages = plan.pagination.graphql_cursor.max_pages_per_scan;
	if (page_body == 0 || max_pages == 0 || page_body > std::numeric_limits<std::uint64_t>::max() / max_pages ||
	    scan.serialized_request_body_bytes != page_body * max_pages ||
	    scan.serialized_request_body_bytes == std::numeric_limits<std::uint64_t>::max()) {
		throw std::invalid_argument("package GraphQL plan lacks an exact reachable body-authority baseline");
	}
	scan.serialized_request_body_bytes++;
	return plan;
}

duckdb_api::ScanPlan
ScanPlanTestAccess::PackageHttpNumericOrigin(duckdb_api::ScanPlan plan,
                                             PackageHttpNumericOriginCounterexample counterexample) {
	const std::string host = NumericOrigin(counterexample);
	if (plan.Operation().Protocol() == duckdb_api::PlannedProtocol::REST) {
		auto operation = plan.Operation().Rest();
		operation.origin.host = host;
		ReplaceRest(plan, std::move(operation));
	} else if (plan.Operation().Protocol() == duckdb_api::PlannedProtocol::GRAPHQL) {
		auto operation = plan.Operation().Graphql();
		operation.origin.host = host;
		ReplaceGraphql(plan, std::move(operation));
	} else {
		throw std::invalid_argument("package numeric-origin counterexample requires HTTP protocol");
	}
	if (plan.network.allowed_hosts.size() != 1 || !plan.authentication_obligation.has_destination) {
		throw std::invalid_argument("package numeric-origin counterexample lost its authority baseline");
	}
	plan.network.allowed_hosts[0] = host;
	plan.authentication_obligation.destination.host = host;
	return plan;
}

duckdb_api::ScanPlan
BuildRepositoryPackageGraphqlNumericOriginCounterexample(const std::string &absolute_repository_root,
                                                         const std::string &logical_secret_name,
                                                         PackageHttpNumericOriginCounterexample counterexample) {
	return ScanPlanTestAccess::PackageHttpNumericOrigin(
	    BuildRepositoryGithubPackageGraphqlPlan(absolute_repository_root, logical_secret_name), counterexample);
}

duckdb_api::ScanPlan
BuildRepositoryPackageRestNumericOriginCounterexample(const std::string &absolute_repository_root,
                                                      const std::string &logical_secret_name,
                                                      PackageHttpNumericOriginCounterexample counterexample) {
	return ScanPlanTestAccess::PackageHttpNumericOrigin(
	    BuildRepositoryGithubPackageRestPlan(absolute_repository_root, "authenticated_user", logical_secret_name),
	    counterexample);
}

duckdb_api::ScanPlan
ScanPlanTestAccess::PackageGraphqlRecipe(duckdb_api::ScanPlan plan,
                                         PackageGraphqlRuntimeRecipeCounterexample counterexample) {
	auto operation = plan.Operation().Graphql();
	if (!operation.generator_recipe) {
		throw std::invalid_argument("package GraphQL recipe counterexample lost its valid baseline");
	}
	auto recipe = std::shared_ptr<duckdb_api::PlannedGraphqlGeneratorRecipe>(
	    new duckdb_api::PlannedGraphqlGeneratorRecipe(*operation.generator_recipe));
	switch (counterexample) {
	case PackageGraphqlRuntimeRecipeCounterexample::MISSING_RECIPE:
		recipe.reset();
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::NATIVE_IDENTITY_WITH_RECIPE:
		operation.document_identity = duckdb_api::PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1;
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::UNKNOWN_RECIPE_IDENTITY:
		recipe->identity = static_cast<duckdb_api::PlannedGraphqlGeneratorIdentity>(255);
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_RECIPE_OPERATION_NAME:
		recipe->operation_name = "OtherOperation";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_PAGE_VARIABLE_NAME:
		recipe->variables[0].name = "otherPage";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::UNKNOWN_PAGE_VARIABLE_TYPE:
		recipe->variables[0].type = static_cast<duckdb_api::PlannedGraphqlRecipeVariableType>(255);
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::UNKNOWN_PAGE_VARIABLE_ROLE:
		recipe->variables[0].role = static_cast<duckdb_api::PlannedGraphqlRecipeVariableRole>(255);
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_PAGE_ARGUMENT_NAME:
		recipe->variables[0].argument_name = "otherFirst";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_CURSOR_VARIABLE_NAME:
		recipe->variables[1].name = "otherCursor";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::UNKNOWN_CURSOR_VARIABLE_TYPE:
		recipe->variables[1].type = static_cast<duckdb_api::PlannedGraphqlRecipeVariableType>(255);
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::UNKNOWN_CURSOR_VARIABLE_ROLE:
		recipe->variables[1].role = static_cast<duckdb_api::PlannedGraphqlRecipeVariableRole>(255);
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_CURSOR_ARGUMENT_NAME:
		recipe->variables[1].argument_name = "otherAfter";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_ROOT_PATH:
		recipe->root_path[0] = "otherViewer";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::MISSING_FIXED_ARGUMENT:
		recipe->fixed_arguments.pop_back();
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_FIXED_ARGUMENT_NAME:
		recipe->fixed_arguments[0].name = "otherAffiliations";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::UNKNOWN_FIXED_ARGUMENT_LITERAL_KIND: {
		auto literal = std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(
		    new duckdb_api::PlannedGraphqlLiteral(recipe->fixed_arguments[0].Value()));
		literal->kind = static_cast<duckdb_api::PlannedGraphqlLiteralKind>(255);
		recipe->fixed_arguments[0].value = std::move(literal);
		break;
	}
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_FIXED_ARGUMENT_LITERAL_VALUE: {
		auto literal = std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(
		    new duckdb_api::PlannedGraphqlLiteral(recipe->fixed_arguments[0].Value()));
		literal->scalar = "OTHER";
		recipe->fixed_arguments[0].value = std::move(literal);
		break;
	}
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_LIST_LITERAL_ITEM: {
		auto list = std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(
		    new duckdb_api::PlannedGraphqlLiteral(recipe->fixed_arguments[0].Value()));
		auto item =
		    std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(new duckdb_api::PlannedGraphqlLiteral(*list->items[0]));
		item->scalar = "OTHER";
		list->items[0] = std::move(item);
		recipe->fixed_arguments[0].value = std::move(list);
		break;
	}
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OBJECT_LITERAL_FIELD_NAME: {
		auto object = std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(
		    new duckdb_api::PlannedGraphqlLiteral(recipe->fixed_arguments[2].Value()));
		object->fields[0].name = "otherField";
		recipe->fixed_arguments[2].value = std::move(object);
		break;
	}
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OBJECT_LITERAL_FIELD_VALUE: {
		auto object = std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(
		    new duckdb_api::PlannedGraphqlLiteral(recipe->fixed_arguments[2].Value()));
		auto value = std::shared_ptr<duckdb_api::PlannedGraphqlLiteral>(
		    new duckdb_api::PlannedGraphqlLiteral(object->fields[0].Value()));
		value->scalar = "OTHER";
		object->fields[0].value = std::move(value);
		recipe->fixed_arguments[2].value = std::move(object);
		break;
	}
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_NODES_FIELD:
		recipe->nodes_field = "otherNodes";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::MISSING_SELECTION:
		recipe->selections.pop_back();
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_SELECTION_COLUMN:
		recipe->selections[0].column_name = "other_id";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_SELECTION_PATH:
		recipe->selections[0].field_path[0] = "otherId";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_PAGE_INFO_FIELD:
		recipe->page_info_field = "otherPageInfo";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_HAS_NEXT_PAGE_FIELD:
		recipe->has_next_page_field = "otherHasNext";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_END_CURSOR_FIELD:
		recipe->end_cursor_field = "otherEndCursor";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::COHERENT_OTHER_DOCUMENT:
		operation.document += " ";
		operation.document_digest = duckdb_api::ComputeSha256Hex(operation.document);
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_PAGE_VARIABLE:
		operation.variables[0].name = "otherPage";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_CURSOR_VARIABLE:
		operation.variables[1].name = "otherCursor";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_NODES_PATH:
		operation.response.nodes.segments.back() = "otherNodes";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_PAGE_INFO_PATH:
		operation.response.page_info.segments.back() = "otherPageInfo";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_HAS_NEXT_PATH:
		operation.cursor.has_next_page.segments.back() = "otherHasNext";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_END_CURSOR_PATH:
		operation.cursor.end_cursor.segments.back() = "otherEndCursor";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_RESULT_COLUMN_NAME:
		operation.result_columns[0].name = "other_id";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_OPERATION_RESULT_COLUMN_PATH:
		operation.result_columns[0].response_path.segments[0] = "otherId";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_CURSOR_PAGE_VARIABLE_CORRELATION:
		operation.cursor.page_size_variable = "otherPage";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::OTHER_CURSOR_VARIABLE_CORRELATION:
		operation.cursor.cursor_variable = "otherCursor";
		break;
	case PackageGraphqlRuntimeRecipeCounterexample::COUNT:
		throw std::invalid_argument("package GraphQL recipe counterexample received its sentinel");
	}
	operation.generator_recipe = std::move(recipe);
	plan.operation =
	    std::shared_ptr<const duckdb_api::PlannedProtocolOperation>(new duckdb_api::PlannedProtocolOperation(
	        duckdb_api::PlannedProtocolOperation::FromGraphql(std::move(operation))));
	return plan;
}

duckdb_api::ScanPlan
BuildPackageGraphqlRuntimeRecipeCounterexample(const std::string &absolute_repository_root,
                                               const std::string &logical_secret_name,
                                               PackageGraphqlRuntimeRecipeCounterexample counterexample) {
	return ScanPlanTestAccess::PackageGraphqlRecipe(
	    BuildRepositoryGithubPackageGraphqlPlan(absolute_repository_root, logical_secret_name), counterexample);
}

} // namespace duckdb_api_test
