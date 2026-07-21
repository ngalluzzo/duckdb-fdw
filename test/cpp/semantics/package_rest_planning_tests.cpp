#include "connector/support/package_compiler_test_fixtures.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/package_bound_scan_planner.hpp"
#include "duckdb_api/relational_predicate.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "query/support/live_scan_request.hpp"
#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <iostream>
#include <string>

namespace duckdb_api_test {
namespace rest_semantics {
namespace {

const duckdb_api::CompiledRelation &FindRelation(const duckdb_api::CompiledConnector &connector,
                                                 const std::string &name) {
	const auto *relation = connector.FindRelation(name);
	Require(relation != nullptr, "native GitHub connector lost relation " + name);
	return *relation;
}

void RequireSameRestOperation(const duckdb_api::PlannedRestOperation &package,
                              const duckdb_api::PlannedRestOperation &native, const std::string &relation) {
	Require(package.operation_name == native.operation_name && package.method == native.method &&
	            package.cardinality == native.cardinality && package.replay_safety == native.replay_safety &&
	            package.origin.scheme == native.origin.scheme && package.origin.host == native.origin.host &&
	            package.origin.port == native.origin.port && package.path == native.path &&
	            package.response_source == native.response_source &&
	            package.records_path.segments == native.records_path.segments,
	        "package/native REST operation differential drifted for " + relation);
	Require(package.headers.size() == native.headers.size(),
	        "package/native REST header count drifted for " + relation);
	for (std::size_t index = 0; index < package.headers.size(); index++) {
		Require(package.headers[index].name == native.headers[index].name &&
		            package.headers[index].value == native.headers[index].value,
		        "package/native REST header differential drifted for " + relation);
	}
	Require(package.result_columns.size() == native.result_columns.size(),
	        "package/native REST result-column count drifted for " + relation);
	for (std::size_t index = 0; index < package.result_columns.size(); index++) {
		const auto &left = package.result_columns[index];
		const auto &right = native.result_columns[index];
		Require(left.name == right.name && left.scalar_kind == right.scalar_kind && left.nullable == right.nullable &&
		            left.response_path.segments == right.response_path.segments,
		        "package/native REST result-column differential drifted for " + relation);
	}
	// query_parameters is the native-0.7-only compatibility mirror (empty for
	// package plans); query_bindings is the executable authority both origins
	// populate, so the differential compares bindings, not parameters.
	Require(package.query_bindings.size() == native.query_bindings.size(),
	        "package/native REST query-binding count drifted for " + relation);
	for (std::size_t index = 0; index < package.query_bindings.size(); index++) {
		const auto &left = package.query_bindings[index];
		const auto &right = native.query_bindings[index];
		Require(left.Name() == right.Name() && left.Kind() == right.Kind() && left.Encoding() == right.Encoding() &&
		            left.EncodedValue() == right.EncodedValue(),
		        "package/native REST query-binding differential drifted for " + relation);
	}
}

void RequireSamePlanEnvelope(const duckdb_api::ScanPlan &package, const duckdb_api::ScanPlan &native,
                             const std::string &relation) {
	Require(package.ConnectorName() == "github" && native.ConnectorName() == "github" &&
	            package.RelationName() == relation && native.RelationName() == relation &&
	            package.Operation().Protocol() == duckdb_api::PlannedProtocol::REST &&
	            native.Operation().Protocol() == duckdb_api::PlannedProtocol::REST,
	        "package/native REST plan lost its shared connector/relation identity for " + relation);
	// The native catalog retains its bounded 0.7.0 compatibility profile and
	// distinct plan identity; the package catalog carries the accepted v1
	// package version. Both build the same executable REST operation below.
	Require(native.ConnectorVersion() == "0.7.0" && package.ConnectorVersion() == "1.0.0",
	        "native/package connector version compatibility profile changed for " + relation);
	RequireSameRestOperation(package.Operation().Rest(), native.Operation().Rest(), relation);
	Require(package.Pagination().Strategy() == native.Pagination().Strategy(),
	        "package/native REST pagination strategy drifted for " + relation);
	Require(package.OutputColumns().size() == native.OutputColumns().size(),
	        "package/native REST output-column count drifted for " + relation);
	for (std::size_t index = 0; index < package.OutputColumns().size(); index++) {
		Require(package.OutputColumns()[index].name == native.OutputColumns()[index].name &&
		            package.OutputColumns()[index].logical_type == native.OutputColumns()[index].logical_type &&
		            package.OutputColumns()[index].nullable == native.OutputColumns()[index].nullable,
		        "package/native REST output schema drifted for " + relation);
	}
	Require(package.Network().allowed_schemes == native.Network().allowed_schemes &&
	            package.Network().allowed_hosts == native.Network().allowed_hosts &&
	            package.Network().port == native.Network().port,
	        "package/native REST network capability drifted for " + relation);
	Require(package.AuthenticationObligation().Requirement() == native.AuthenticationObligation().Requirement() &&
	            package.AuthenticationObligation().Authenticator() ==
	                native.AuthenticationObligation().Authenticator() &&
	            package.AuthenticationObligation().Placement() == native.AuthenticationObligation().Placement(),
	        "package/native REST authentication obligation drifted for " + relation);
	Require(package.Budgets().response_bytes == native.Budgets().response_bytes &&
	            package.Budgets().decoded_records == native.Budgets().decoded_records &&
	            package.Budgets().extracted_string_bytes == native.Budgets().extracted_string_bytes,
	        "package/native REST resource ceilings drifted for " + relation);
}

void TestAuthenticatedUserRestDifferential(const std::string &absolute_repository_root) {
	const auto native_connector = duckdb_api::BuildNativeGithubConnector();
	const auto &native_relation = FindRelation(native_connector, "authenticated_user");
	const auto native_plan = duckdb_api::BuildConservativeScanPlan(
	    native_connector, BuildAuthenticatedScanRequest(native_connector, native_relation.Name(), "github_default"));
	const auto package_plan = BuildRepositoryGithubPackageRestPlan(absolute_repository_root, "authenticated_user",
	                                                               "package_rest_planning_secret");
	RequireSamePlanEnvelope(package_plan, native_plan, "authenticated_user");
	Require(package_plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            native_plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            package_plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
	            native_plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "unrestricted authenticated_user plan gained an unexpected predicate authority");
}

void TestAnonymousSearchRestDifferential(const std::string &absolute_repository_root) {
	const auto native_connector = duckdb_api::BuildNativeGithubConnector();
	const auto &native_relation = FindRelation(native_connector, "duckdb_login_search_page");
	const auto native_plan = duckdb_api::BuildConservativeScanPlan(
	    native_connector, BuildAnonymousScanRequest(native_connector, native_relation.Name()));
	const auto package_plan = BuildRepositoryGithubPackageAnonymousSearchPlan(absolute_repository_root);
	RequireSamePlanEnvelope(package_plan, native_plan, "duckdb_login_search_page");
	Require(package_plan.AuthenticationObligation().Requirement() == duckdb_api::PlannedCredentialRequirement::NONE,
	        "anonymous duckdb_login_search_page package plan required a credential");
}

void TestAuthenticatedRepositoriesRestDifferential(const std::string &absolute_repository_root) {
	const auto native_connector = duckdb_api::BuildNativeGithubConnector();
	const auto &native_relation = FindRelation(native_connector, "authenticated_repositories");
	auto native_request = BuildAuthenticatedScanRequest(native_connector, native_relation.Name(), "github_default");
	const auto native_plan = duckdb_api::BuildConservativeScanPlan(native_connector, native_request);
	const auto package_plan = BuildRepositoryGithubPackageRestPlan(
	    absolute_repository_root, "authenticated_repositories", "package_rest_planning_secret");
	RequireSamePlanEnvelope(package_plan, native_plan, "authenticated_repositories");
	Require(package_plan.Pagination().Strategy() == duckdb_api::PlannedPaginationStrategy::LINK_HEADER &&
	            package_plan.Pagination().Target().page_size == native_plan.Pagination().Target().page_size &&
	            package_plan.Pagination().Target().page_size_parameter ==
	                native_plan.Pagination().Target().page_size_parameter &&
	            package_plan.Pagination().Target().page_number_parameter ==
	                native_plan.Pagination().Target().page_number_parameter &&
	            package_plan.Pagination().Target().first_page == native_plan.Pagination().Target().first_page &&
	            package_plan.Pagination().Target().page_increment == native_plan.Pagination().Target().page_increment,
	        "package/native authenticated_repositories pagination target drifted");
	Require(package_plan.Pagination().ScanBudgets().decoded_records ==
	            native_plan.Pagination().ScanBudgets().decoded_records,
	        "package/native authenticated_repositories scan budget drifted");

	// Applying the same `visibility = 'private'` predicate through both
	// origins must reach the same superset-accuracy restriction, but the two
	// origins encode it differently by design: native materializes it through
	// the 0.7 VISIBILITY_EQUALS_PRIVATE/VISIBILITY_PRIVATE compatibility
	// bridge outside query_bindings (native's compiled operation never
	// declares a "visibility" REST query field at all), while the package
	// plan reports the generic TYPED_EQUALITY/REST_QUERY_BINDING
	// classification and adds an explicit typed query binding for it. Only
	// the origin/path/method/headers/result-column envelope is required to
	// stay identical; query_bindings is not compared here.
	const auto visibility_column_index = 5;
	auto native_predicate_request = native_request;
	native_predicate_request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    visibility_column_index, duckdb_api::RequestedPredicateValueKind::VARCHAR,
	    duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::Varchar("private"));
	native_predicate_request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	native_predicate_request.capabilities.selective_predicate = true;
	native_predicate_request.capabilities.retains_predicate = true;
	const auto native_predicate_plan =
	    duckdb_api::BuildConservativeScanPlan(native_connector, native_predicate_request);

	const auto package_generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	const duckdb_api::PackageBoundScanPlanningService package_planning(package_generation);
	auto package_predicate_request = duckdb_api::BuildConservativeScanRequest(
	    package_generation.Connector(), "authenticated_repositories",
	    duckdb_api::LogicalSecretReference::Named("package_rest_planning_secret"));
	package_predicate_request.requested_predicate = native_predicate_request.requested_predicate;
	package_predicate_request.retained_predicate_scope = native_predicate_request.retained_predicate_scope;
	package_predicate_request.capabilities = native_predicate_request.capabilities;
	const auto package_predicate_plan =
	    package_planning.Plan(package_generation.QueryRegistration().GenerationHandle(), package_predicate_request);

	Require(native_predicate_plan.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            native_predicate_plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "native authenticated_repositories predicate lost its 0.7 compatibility classification");
	Require(package_predicate_plan.RemotePredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            package_predicate_plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::REST_QUERY_BINDING,
	        "package authenticated_repositories predicate did not classify as a package typed equality");
	Require(native_predicate_plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            package_predicate_plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            native_predicate_plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            package_predicate_plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        "package/native authenticated_repositories predicate accuracy/residual ownership drifted");

	const auto &native_predicate_rest = native_predicate_plan.Operation().Rest();
	const auto &package_predicate_rest = package_predicate_plan.Operation().Rest();
	Require(native_predicate_rest.origin.host == package_predicate_rest.origin.host &&
	            native_predicate_rest.path == package_predicate_rest.path &&
	            native_predicate_rest.method == package_predicate_rest.method &&
	            native_predicate_rest.response_source == package_predicate_rest.response_source,
	        "package/native authenticated_repositories predicate plan lost its shared request envelope");
	Require(native_predicate_rest.query_bindings.size() == 2,
	        "native authenticated_repositories predicate plan gained a query binding outside its compatibility bridge");
	Require(package_predicate_rest.query_bindings.size() == 3, "package authenticated_repositories predicate plan did "
	                                                           "not add its typed visibility query binding");
	const auto &visibility_binding = package_predicate_rest.query_bindings.back();
	Require(visibility_binding.Name() == "visibility" &&
	            visibility_binding.Source() == duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT &&
	            visibility_binding.Kind() == duckdb_api::PlannedRestScalarKind::VARCHAR &&
	            visibility_binding.VarcharValue() == "private",
	        "package authenticated_repositories typed visibility query binding lost its predicate-sourced value");
}

} // namespace
} // namespace rest_semantics
} // namespace duckdb_api_test

int main(int argc, char **argv) {
	if (argc != 2) {
		std::cerr << "usage: package_rest_planning_tests ABSOLUTE_REPOSITORY_ROOT" << std::endl;
		return 1;
	}
	duckdb_api_test::rest_semantics::TestAuthenticatedUserRestDifferential(argv[1]);
	duckdb_api_test::rest_semantics::TestAnonymousSearchRestDifferential(argv[1]);
	duckdb_api_test::rest_semantics::TestAuthenticatedRepositoriesRestDifferential(argv[1]);
	std::cout << "package/native REST plan differential tests passed" << std::endl;
	return 0;
}
