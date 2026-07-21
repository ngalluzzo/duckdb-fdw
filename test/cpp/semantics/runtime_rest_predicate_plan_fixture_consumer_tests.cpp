#include "semantics/support/runtime_rest_predicate_plan_test_fixtures.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
	try {
		const auto exact = duckdb_api_test::BuildRuntimeExactRestPredicatePlanFixture();
		const auto residual = duckdb_api_test::BuildRuntimeResidualOnlyRestPredicatePlanFixture();
		const auto malformed = duckdb_api_test::BuildRuntimeRestPredicatePlanCounterexample(
		    duckdb_api_test::RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SOURCE_ID);
		const auto native = duckdb_api_test::BuildRuntimeNativePredicateIsolationPlanFixture();
		if (exact.ConditionalInput() != duckdb_api::PlannedConditionalInput::REST_QUERY_BINDING ||
		    residual.ConditionalInput() != duckdb_api::PlannedConditionalInput::NONE ||
		    malformed.Operation().Protocol() != duckdb_api::PlannedProtocol::REST ||
		    native.ConditionalInput() != duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE) {
			std::cerr << "materialized fixture consumer observed an invalid bounded API" << std::endl;
			return EXIT_FAILURE;
		}
		std::cout << "materialized REST predicate fixture consumer passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "materialized REST predicate fixture consumer failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
