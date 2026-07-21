#include "package_fixture_execution.hpp"

#include "runtime/execution/package_fixture_variant_test_support.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;
using duckdb_api_test::RuntimeFixtureAuthorizationState;
using duckdb_api_test::RuntimeFixtureBoundaryVariant;
using duckdb_api_test::RuntimeFixtureRelationResourceField;
using duckdb_api_test::RuntimeFixtureVariantOutcome;
using duckdb_api_test::RuntimePackageFixtureExecutionService;
using duckdb_api_test::variant_test::AuthenticatedRootObject;
using duckdb_api_test::variant_test::GenericRestTranscript;
using duckdb_api_test::variant_test::ManualControl;
using duckdb_api_test::variant_test::Response;

void RequireBoundary(const duckdb_api_test::RuntimeFixtureVariantObservation &result) {
	Require(result.outcome == RuntimeFixtureVariantOutcome::BOUNDARY_SUCCEEDED && result.execution.succeeded &&
	            result.observed_units == result.admitted_limit,
	        "relation resource boundary did not debit the exact admitted limit");
}

void RequireOneOver(const duckdb_api_test::RuntimeFixtureVariantObservation &result) {
	Require(result.outcome == RuntimeFixtureVariantOutcome::ONE_OVER_REJECTED && !result.execution.succeeded &&
	            result.execution.has_runtime_error && result.observed_units == result.admitted_limit + 1,
	        "relation resource one-over did not preserve the exact rejected attempt");
}

void TestEveryRelationResourceField() {
	const auto plan = duckdb_api_test::BuildValidPaginatedPlanFixture("runtime_variant_secret");
	const auto transcript = GenericRestTranscript();
	const RuntimeFixtureRelationResourceField fields[] = {RuntimeFixtureRelationResourceField::RESPONSE_BYTES_PER_PAGE,
	                                                      RuntimeFixtureRelationResourceField::RESPONSE_BYTES_PER_SCAN,
	                                                      RuntimeFixtureRelationResourceField::RECORDS_PER_PAGE,
	                                                      RuntimeFixtureRelationResourceField::RECORDS_PER_SCAN,
	                                                      RuntimeFixtureRelationResourceField::EXTRACTED_STRING_BYTES};
	RuntimePackageFixtureExecutionService service;
	for (const auto field : fields) {
		ManualControl boundary_control;
		RequireBoundary(service.ExecuteRelationResourceVariant(
		    plan, transcript, field, RuntimeFixtureBoundaryVariant::BOUNDARY, boundary_control));
		ManualControl rejected_control;
		RequireOneOver(service.ExecuteRelationResourceVariant(
		    plan, transcript, field, RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED, rejected_control));
	}
}

void TestRootObjectRecordAccountingUsesCardinalityOnePrimitive() {
	const auto plan = duckdb_api_test::BuildValidAuthenticatedPlanFixture("runtime_variant_secret");
	const duckdb_api_test::RuntimeFixtureTranscript transcript {RuntimeFixtureAuthorizationState::BEARER_PRESENT,
	                                                            {Response(AuthenticatedRootObject())}};
	RuntimePackageFixtureExecutionService service;
	ManualControl boundary_control;
	RequireBoundary(service.ExecuteRelationResourceVariant(plan, transcript,
	                                                       RuntimeFixtureRelationResourceField::RECORDS_PER_PAGE,
	                                                       RuntimeFixtureBoundaryVariant::BOUNDARY, boundary_control));
	ManualControl rejected_control;
	RequireOneOver(
	    service.ExecuteRelationResourceVariant(plan, transcript, RuntimeFixtureRelationResourceField::RECORDS_PER_SCAN,
	                                           RuntimeFixtureBoundaryVariant::ONE_OVER_REJECTED, rejected_control));
}

void TestWireAccountingOverrideIsCallScopedAndOrdinaryBodiesUseTheirSize() {
	const auto plan = duckdb_api_test::BuildValidPaginatedPlanFixture("runtime_variant_secret");
	RuntimePackageFixtureExecutionService service;
	ManualControl boundary_control;
	RequireBoundary(service.ExecuteRelationResourceVariant(plan, GenericRestTranscript(),
	                                                       RuntimeFixtureRelationResourceField::RESPONSE_BYTES_PER_PAGE,
	                                                       RuntimeFixtureBoundaryVariant::BOUNDARY, boundary_control));

	ManualControl ordinary_control;
	const duckdb_api_test::RuntimeFixtureTranscript oversized {
	    RuntimeFixtureAuthorizationState::BEARER_PRESENT,
	    {Response(std::string(static_cast<std::size_t>(plan.Budgets().response_bytes + 1), 'x'))}};
	const auto ordinary = service.Execute(plan, oversized, ordinary_control);
	Require(!ordinary.succeeded && ordinary.has_runtime_error &&
	            ordinary.runtime_error_stage == duckdb_api::ErrorStage::RESOURCE &&
	            ordinary.runtime_error_field == "response_bytes" && ordinary.request_count == 1,
	        "ordinary controlled response stopped accounting its actual body size after a boundary override");
}

} // namespace

int main() {
	try {
		TestEveryRelationResourceField();
		TestRootObjectRecordAccountingUsesCardinalityOnePrimitive();
		TestWireAccountingOverrideIsCallScopedAndOrdinaryBodiesUseTheirSize();
		std::cout << "package fixture Runtime relation resource variant tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package fixture Runtime relation resource variant tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
