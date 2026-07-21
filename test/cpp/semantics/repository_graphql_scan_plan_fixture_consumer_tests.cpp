#include "semantics/support/repository_graphql_scan_plan_test_fixtures.hpp"

#include "support/require.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
	try {
		duckdb_api_test::Require(argc == 2,
		                         "usage: repository_graphql_fixture_consumer_tests ABSOLUTE_REPOSITORY_ROOT");
		const auto plan = duckdb_api_test::BuildRepositoryGithubPackageGraphqlPlan(
		    argv[1], "isolated_runtime_package_graphql_secret");
		const auto &operation = plan.Operation().Graphql();
		duckdb_api_test::Require(
		    plan.Domain() == duckdb_api::BaseDomain::GRAPHQL_RELAY_CONNECTION_NODE_OCCURRENCES &&
		        operation.document_identity == duckdb_api::PlannedGraphqlDocumentIdentity::PACKAGE_GENERATED_V1 &&
		        operation.generator_recipe != nullptr &&
		        operation.generator_recipe->Identity() ==
		            duckdb_api::PlannedGraphqlGeneratorIdentity::PACKAGE_QUERY_GENERATOR_V1 &&
		        operation.generator_recipe->Selections().size() == operation.result_columns.size() &&
		        operation.generator_recipe->Variables().size() == operation.variables.size(),
		    "isolated Runtime consumer did not receive the complete Semantics-owned package GraphQL plan");
		using Counterexample = duckdb_api_test::PackageGraphqlRuntimeRecipeCounterexample;
		const auto count = static_cast<std::size_t>(Counterexample::COUNT);
		duckdb_api_test::Require(count == 38,
		                         "closed Runtime-facing package recipe counterexample catalog changed without review");
		for (std::size_t value = 0; value < count; value++) {
			const auto counterexample = static_cast<Counterexample>(value);
			const auto invalid = duckdb_api_test::BuildPackageGraphqlRuntimeRecipeCounterexample(
			    argv[1], "isolated_runtime_package_graphql_secret", counterexample);
			const auto &invalid_operation = invalid.Operation().Graphql();
			duckdb_api_test::Require(counterexample == Counterexample::MISSING_RECIPE
			                             ? invalid_operation.generator_recipe == nullptr
			                             : invalid_operation.generator_recipe != nullptr,
			                         "Semantics provider did not produce the named package recipe counterexample");
		}
		bool sentinel_rejected = false;
		try {
			(void)duckdb_api_test::BuildPackageGraphqlRuntimeRecipeCounterexample(
			    argv[1], "isolated_runtime_package_graphql_secret", Counterexample::COUNT);
		} catch (const std::invalid_argument &) {
			sentinel_rejected = true;
		}
		duckdb_api_test::Require(sentinel_rejected, "package recipe counterexample provider accepted its sentinel");
		std::cout << "repository package GraphQL plan fixture consumer tests passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
