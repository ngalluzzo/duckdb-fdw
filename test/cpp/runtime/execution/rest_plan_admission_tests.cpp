#include "duckdb_api/authorization.hpp"
#include "duckdb_api/internal/runtime/authentication/api_key_authenticator.hpp"
#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "semantics/support/runtime_rest_predicate_plan_test_fixtures.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using duckdb_api_test::Require;

class NeverCancelledControl final : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

duckdb_api::internal::HttpExecutionProfile RepositoryExecutionProfile() {
	return {duckdb_api::PlannedUrlScheme::HTTPS,
	        "api.github.com",
	        443,
	        false,
	        false,
	        false,
	        duckdb_api::MAX_EXECUTION_MILLISECONDS,
	        duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE,
	        duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP,
	        duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN,
	        duckdb_api::RETRY_MAX_DELAY_MILLISECONDS,
	        duckdb_api::RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN};
}

duckdb_api::internal::HttpExecutionProfile PredicateProofExecutionProfile() {
	return {duckdb_api::PlannedUrlScheme::HTTPS,
	        "predicate-proof.invalid",
	        443,
	        false,
	        false,
	        false,
	        duckdb_api::MAX_EXECUTION_MILLISECONDS,
	        duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE,
	        duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_STEP,
	        duckdb_api::RETRY_MAX_REQUEST_ATTEMPTS_PER_SCAN,
	        duckdb_api::RETRY_MAX_DELAY_MILLISECONDS,
	        duckdb_api::RETRY_MAX_CUMULATIVE_WAITING_MILLISECONDS_PER_SCAN};
}

void TestNamesClassificationAndValidRequestFactsAreNotAuthority() {
	using namespace duckdb_api_test;
	const OperationPlanCounterexample operations[] = {
	    OperationPlanCounterexample::OTHER_CONNECTOR_IDENTITY, OperationPlanCounterexample::OTHER_CONNECTOR_VERSION,
	    OperationPlanCounterexample::OTHER_RELATION_IDENTITY,  OperationPlanCounterexample::EMPTY_IDENTITY,
	    OperationPlanCounterexample::OTHER_OPERATION_IDENTITY, OperationPlanCounterexample::OTHER_PATH,
	    OperationPlanCounterexample::EMPTY_FIXED_HEADER_VALUE};
	for (const auto variation : operations) {
		auto profile = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(
		    BuildOperationPlanCounterexample("fixture_secret", variation), RepositoryExecutionProfile());
		Require(static_cast<bool>(profile), "REST admission interpreted provenance or valid request data as identity");
	}

	auto nullable = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(
	    BuildResponsePlanCounterexample("fixture_secret", ResponsePlanCounterexample::FLIPPED_SCHEMA_NULLABILITY),
	    RepositoryExecutionProfile());
	Require(nullable && nullable->Columns()[0].nullable,
	        "REST admission rejected a structurally valid nullable output column");

	const RepositoryPlanCounterexample independent_facts[] = {
	    RepositoryPlanCounterexample::MISSING_VISIBILITY_COLUMN,
	    RepositoryPlanCounterexample::VISIBILITY_NOT_TRAILING,
	    RepositoryPlanCounterexample::VISIBILITY_NULLABLE,
	    RepositoryPlanCounterexample::VISIBILITY_WRONG_TYPE,
	    RepositoryPlanCounterexample::VISIBILITY_WRONG_EXTRACTOR,
	    RepositoryPlanCounterexample::UNKNOWN_PREDICATE_CATEGORY,
	    RepositoryPlanCounterexample::UNKNOWN_PREDICATE_REASON,
	    RepositoryPlanCounterexample::EXACT_CATEGORY_SUPERSET_ACCURACY,
	    RepositoryPlanCounterexample::SUPERSET_CATEGORY_EXACT_ACCURACY,
	    RepositoryPlanCounterexample::AMBIGUOUS_RESIDUAL_TRUE,
	    RepositoryPlanCounterexample::MAPPING_UNAVAILABLE_RESIDUAL_TRUE};
	for (std::size_t index = 0; index < sizeof(independent_facts) / sizeof(independent_facts[0]); index++) {
		Require(static_cast<bool>(duckdb_api::internal::TryAdmitPaginatedRestPlan(
		            BuildRepositoryPlanCounterexample("fixture_secret", independent_facts[index]),
		            RepositoryExecutionProfile())),
		        "REST admission interpreted independent schema/classification variation " + std::to_string(index) +
		            " as native identity");
	}
}

void TestPermanentConditionalBindingUsesTypedAuthority() {
	const auto plan = duckdb_api_test::BuildDistinctRestQueryPathScanPlanFixture("permanent_rest_secret");
	const auto &operation = plan.Operation().Rest();
	bool has_conditional = false;
	for (const auto &binding : operation.query_bindings) {
		has_conditional =
		    has_conditional || binding.Source() == duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT;
	}
	Require(!operation.result_columns.empty() && !operation.records_path.segments.empty() && has_conditional &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::REST_QUERY_BINDING &&
	            plan.TypedEquality() != nullptr,
	        "permanent REST fixture lost its typed conditional binding");
	auto admitted = duckdb_api::internal::TryAdmitPaginatedRestPlan(plan, RepositoryExecutionProfile());
	Require(static_cast<bool>(admitted), "matching permanent REST conditional authority was not admitted");
	std::size_t access_count = 0;
	for (const auto &parameter : admitted->QueryParameters()) {
		if (parameter.name == "access") {
			access_count++;
			Require(parameter.encoded_value == "private", "typed conditional value changed during materialization");
		}
	}
	Require(access_count == 1 &&
	            admitted->ConditionalInput() == duckdb_api::internal::AdmittedPaginatedRestConditionalInput::NONE,
	        "generic conditional binding borrowed the native visibility compatibility discriminant");
}

void TestPlannerProducedExactAndResidualOnlyPlansRemainDistinct() {
	const auto exact_plan = duckdb_api_test::BuildRuntimeExactRestPredicatePlanFixture();
	Require(exact_plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
	            exact_plan.TypedEquality() != nullptr &&
	            exact_plan.TypedEquality()->OccurrencePreservation() ==
	                duckdb_api::PlannedOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
	        "planner-produced exact fixture lost its exact occurrence authority");
	auto exact = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(exact_plan, PredicateProofExecutionProfile());
	Require(static_cast<bool>(exact), "planner-produced exact REST predicate plan was not admitted");
	std::size_t rank_filter_count = 0;
	for (const auto &parameter : exact->QueryParameters()) {
		if (parameter.name == "rank_filter") {
			rank_filter_count++;
			Require(parameter.encoded_value == "42", "exact BIGINT predicate changed during materialization");
		}
	}
	Require(rank_filter_count == 1, "exact conditional authority did not emit exactly one request binding");

	const auto residual_plan = duckdb_api_test::BuildRuntimeResidualOnlyRestPredicatePlanFixture();
	Require(residual_plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            residual_plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
	            residual_plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TYPED_EQUALITY &&
	            residual_plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
	        "planner-produced residual fixture lost its DuckDB-only predicate ownership");
	auto residual =
	    duckdb_api::internal::TryAdmitSingleResponseHttpPlan(residual_plan, PredicateProofExecutionProfile());
	Require(static_cast<bool>(residual), "planner-produced residual-only REST predicate plan was not admitted");
	for (const auto &parameter : residual->QueryParameters()) {
		Require(parameter.name != "rank_filter", "residual-only predicate leaked into the remote request");
	}
}

void TestDoubleTypedEqualityReachesRealRequest() {
	// RFC 0020: proves a DOUBLE predicate reaches the actual constructed REST
	// request (not merely a correct result via DuckDB's residual fallback).
	const auto plan = duckdb_api_test::BuildRuntimeExactDoubleRestPredicatePlanFixture();
	Require(plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT && plan.TypedEquality() != nullptr &&
	            plan.TypedEquality()->Kind() == duckdb_api::PlannedRestScalarKind::DOUBLE &&
	            plan.TypedEquality()->DoubleValue() == 3.5,
	        "planner-produced DOUBLE exact fixture lost its typed conditional authority");
	auto admitted = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(plan, PredicateProofExecutionProfile());
	Require(static_cast<bool>(admitted), "planner-produced DOUBLE REST predicate plan was not admitted");
	std::size_t score_filter_count = 0;
	for (const auto &parameter : admitted->QueryParameters()) {
		if (parameter.name == "score_filter") {
			score_filter_count++;
			Require(parameter.encoded_value == "3.5", "exact DOUBLE predicate changed during materialization");
		}
	}
	Require(score_filter_count == 1, "DOUBLE conditional authority did not emit exactly one request binding");
}

void TestConditionalBindingCounterexamplesFailBeforeTransport() {
	using duckdb_api_test::RuntimeRestPredicatePlanCounterexample;
	const RuntimeRestPredicatePlanCounterexample counterexamples[] = {
	    RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SOURCE_ID,
	    RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SCALAR_KIND,
	    RuntimeRestPredicatePlanCounterexample::CONDITIONAL_TYPED_VALUE,
	    RuntimeRestPredicatePlanCounterexample::NONCANONICAL_ENCODED_VALUE,
	    RuntimeRestPredicatePlanCounterexample::DUPLICATE_CONDITIONAL_BINDING};
	for (std::size_t index = 0; index < sizeof(counterexamples) / sizeof(counterexamples[0]); index++) {
		const auto plan = duckdb_api_test::BuildRuntimeRestPredicatePlanCounterexample(counterexamples[index]);
		const auto runtime = duckdb_api_test::BuildControlledHttpRuntimeForHost("predicate-proof.invalid");
		NeverCancelledControl control;
		bool rejected = false;
		try {
			(void)runtime->Executor()->Open(plan, control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = true;
			Require(error.Stage() == duckdb_api::ErrorStage::POLICY,
			        "conditional binding mismatch used the wrong error stage");
		}
		const auto observation = runtime->Observation();
		Require(rejected && observation.request_count == 0 && observation.target.empty() && observation.headers.empty(),
		        "conditional binding mismatch reached request construction or transport at index " +
		            std::to_string(index));
	}
}

void TestPermanentResponseSchemaCounterexamplesFailBeforeTransport() {
	using duckdb_api_test::RuntimeRestSchemaCounterexample;
	const RuntimeRestSchemaCounterexample counterexamples[] = {
	    RuntimeRestSchemaCounterexample::RESULT_NAME,
	    RuntimeRestSchemaCounterexample::RESULT_SHAPE,
	    RuntimeRestSchemaCounterexample::RESULT_ELEMENT_KIND,
	    RuntimeRestSchemaCounterexample::RESULT_ELEMENT_NULLABILITY,
	    RuntimeRestSchemaCounterexample::RESULT_OUTER_NULLABILITY,
	    RuntimeRestSchemaCounterexample::RESULT_PATH,
	    RuntimeRestSchemaCounterexample::RESULT_ARITY,
	    RuntimeRestSchemaCounterexample::RESULT_ORDER,
	    RuntimeRestSchemaCounterexample::OUTPUT_NAME,
	    RuntimeRestSchemaCounterexample::OUTPUT_NAME_ORDER,
	    RuntimeRestSchemaCounterexample::OUTPUT_ARITY,
	    RuntimeRestSchemaCounterexample::OUTPUT_SHAPE};
	for (std::size_t index = 0; index < sizeof(counterexamples) / sizeof(counterexamples[0]); index++) {
		const auto plan = duckdb_api_test::BuildRuntimeRestSchemaCounterexample(counterexamples[index]);
		const auto runtime = duckdb_api_test::BuildControlledHttpRuntimeForHost("api.github.com");
		NeverCancelledControl control;
		bool rejected = false;
		try {
			(void)runtime->Executor()->Open(plan, control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = true;
			Require(error.Stage() == duckdb_api::ErrorStage::POLICY,
			        "REST response-schema mismatch used the wrong error stage");
		}
		const auto observation = runtime->Observation();
		Require(rejected && observation.request_count == 0 && observation.target.empty() && observation.headers.empty(),
		        "REST response-schema mismatch reached request construction or transport at index " +
		            std::to_string(index));
	}
}

void TestNativeConditionalCompatibilityRemainsIsolated() {
	auto selected = duckdb_api::internal::TryAdmitPaginatedRestPlan(
	    duckdb_api_test::BuildRuntimeNativePredicateIsolationPlanFixture(), RepositoryExecutionProfile());
	auto fallback = duckdb_api::internal::TryAdmitPaginatedRestPlan(
	    duckdb_api_test::BuildCompleteResidualFallbackPlanFixture("native_fallback_secret"),
	    RepositoryExecutionProfile());
	Require(selected && fallback &&
	            selected->ConditionalInput() ==
	                duckdb_api::internal::AdmittedPaginatedRestConditionalInput::LEGACY_VISIBILITY_PRIVATE &&
	            fallback->ConditionalInput() == duckdb_api::internal::AdmittedPaginatedRestConditionalInput::NONE,
	        "native selected and fallback profiles lost their distinct compatibility authority");
	const auto selected_request =
	    duckdb_api::internal::BuildAdmittedPaginatedRestPageRequest(*selected, selected->FirstPage());
	const auto fallback_request =
	    duckdb_api::internal::BuildAdmittedPaginatedRestPageRequest(*fallback, fallback->FirstPage());
	Require(selected_request.target.find("visibility=private") != std::string::npos &&
	            selected_request.target.find("rank_filter=") == std::string::npos &&
	            fallback_request.target.find("visibility=") == std::string::npos,
	        "generic admission widened or erased the native visibility compatibility bridge");
}

// RFC 0018: proves ApiKeyAuthenticator actually places the declared header or
// query value correctly through the real admission pipeline, and that the
// query-placement value never enters QueryParameters()/EXPLAIN-visible facts
// (addressing an adversarial-review gap: the compiler-level and
// authorization-construction tests alone never exercised real admission or
// authenticator placement for either api_key shape).
void TestApiKeyAuthenticatorPlacesDeclaredHeaderAndQueryValues() {
	using duckdb_api::PlannedCredentialPlacement;
	using duckdb_api::ScanAuthorization;
	using duckdb_api::internal::ApiKeyAuthenticator;

	const auto profile = RepositoryExecutionProfile();

	{
		const auto plan = duckdb_api_test::BuildValidApiKeyPlanFixture(
		    "api_key_secret", PlannedCredentialPlacement::HEADER_NAMED, "X-Api-Key");
		const auto admitted = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(plan, profile);
		Require(admitted != nullptr, "valid api_key header plan failed to admit");
		Require(admitted->RequiresApiKey() && admitted->ApiKeyHeaderPlacement() &&
		            admitted->ApiKeyPlacementName() == "X-Api-Key" && !admitted->RequiresBearer(),
		        "admitted profile lost its api_key header placement facts");
		auto request = duckdb_api::internal::BuildAdmittedRestRequest(*admitted);
		auto authorization = ScanAuthorization::Credential(std::string("header-secret-value"));
		const auto authorized = ApiKeyAuthenticator::AuthorizeRest(*admitted, std::move(request), authorization);
		bool found_header = false;
		for (const auto &header : authorized.headers) {
			if (header.name == "X-Api-Key") {
				Require(header.value == "header-secret-value", "api_key header carried the wrong value");
				found_header = true;
			}
			Require(header.name != "Authorization", "api_key header placement leaked an Authorization header");
		}
		Require(found_header, "api_key header placement did not add the declared header");
		Require(authorized.target.find("header-secret-value") == std::string::npos &&
		            authorized.target.find("X-Api-Key") == std::string::npos,
		        "api_key header placement leaked into the request target");
	}
	{
		const auto plan = duckdb_api_test::BuildValidApiKeyPlanFixture(
		    "api_key_secret", PlannedCredentialPlacement::QUERY_NAMED, "api_key");
		const auto admitted = duckdb_api::internal::TryAdmitSingleResponseHttpPlan(plan, profile);
		Require(admitted != nullptr, "valid api_key query plan failed to admit");
		Require(admitted->RequiresApiKey() && !admitted->ApiKeyHeaderPlacement() &&
		            admitted->ApiKeyPlacementName() == "api_key" && !admitted->RequiresBearer(),
		        "admitted profile lost its api_key query placement facts");
		Require(admitted->QueryParameters().empty(),
		        "api_key query credential leaked into the profile's EXPLAIN-visible query parameters before "
		        "authorization");
		auto request = duckdb_api::internal::BuildAdmittedRestRequest(*admitted);
		Require(request.target.find("api_key=") == std::string::npos,
		        "unauthorized admitted request already carries the api_key query parameter");
		auto authorization = ScanAuthorization::Credential(std::string("query-secret-value"));
		const auto authorized = ApiKeyAuthenticator::AuthorizeRest(*admitted, std::move(request), authorization);
		Require(authorized.target.find("?api_key=query-secret-value") != std::string::npos ||
		            authorized.target.find("&api_key=query-secret-value") != std::string::npos,
		        "api_key query placement did not append the declared parameter and value");
		for (const auto &header : authorized.headers) {
			Require(header.value.find("query-secret-value") == std::string::npos,
			        "api_key query placement leaked its value into a header");
		}
	}
}

} // namespace

int main() {
	try {
		TestNamesClassificationAndValidRequestFactsAreNotAuthority();
		TestPermanentConditionalBindingUsesTypedAuthority();
		TestPlannerProducedExactAndResidualOnlyPlansRemainDistinct();
		TestDoubleTypedEqualityReachesRealRequest();
		TestConditionalBindingCounterexamplesFailBeforeTransport();
		TestPermanentResponseSchemaCounterexamplesFailBeforeTransport();
		TestNativeConditionalCompatibilityRemainsIsolated();
		TestApiKeyAuthenticatorPlacesDeclaredHeaderAndQueryValues();
		std::cout << "REST plan admission tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "REST plan admission tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
