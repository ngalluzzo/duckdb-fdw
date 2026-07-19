#include "duckdb_api/authorization.hpp"
#include "duckdb_api/internal/runtime/authentication/fixed_github_user_bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "runtime/support/controlled_http_transport.hpp"
#include "runtime/support/http_scan_executor_test_support.hpp"
#include "support/require.hpp"
#include "semantics/support/scan_plan_test_fixtures.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <type_traits>

namespace {

using duckdb_api_test::BuildAnonymousHttpPlan;
using duckdb_api_test::BuildAuthenticatedHttpPlan;
using duckdb_api_test::GeneratedHttpBearerToken;
using duckdb_api_test::ManualHttpExecutionControl;
using duckdb_api_test::Require;
using duckdb_api_test::RequireHttpExecutionError;

static_assert(std::is_copy_constructible<duckdb_api::internal::AdmittedRepositoryRequestProfile>::value,
              "admitted repository profiles must support stream ownership copies");
static_assert(!std::is_copy_assignable<duckdb_api::internal::AdmittedRepositoryRequestProfile>::value,
              "admitted repository profiles must remain immutable after admission");

void RequirePlanDeniedBeforeTransport(const std::shared_ptr<duckdb_api_test::ControlledHttpRuntime> &runtime,
                                      const duckdb_api::ScanPlan &plan, bool authenticated, uint64_t suffix,
                                      const std::string &context) {
	ManualHttpExecutionControl control;
	bool rejected = false;
	if (authenticated) {
		auto token = GeneratedHttpBearerToken(suffix);
		try {
			(void)runtime->Executor()->OpenWithAuthorization(
			    plan, duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token)), control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = true;
			Require(error.Stage() == duckdb_api::ErrorStage::POLICY, context + " used the wrong error stage");
		}
	} else {
		try {
			(void)runtime->Executor()->OpenWithAuthorization(plan, duckdb_api::ScanAuthorization::Anonymous(), control);
		} catch (const duckdb_api::ExecutionError &error) {
			rejected = true;
			Require(error.Stage() == duckdb_api::ErrorStage::POLICY, context + " used the wrong error stage");
		}
	}
	Require(rejected, context + " did not produce a structured policy error");
	Require(runtime->Observation().request_count == 0, "invalid provider-owned plan reached transport");
}

void TestProviderOwnedPlanDenialMatrix() {
	using namespace duckdb_api_test;
	uint64_t suffix = 100;
	const OperationPlanCounterexample operations[] = {OperationPlanCounterexample::OTHER_CONNECTOR_IDENTITY,
	                                                  OperationPlanCounterexample::OTHER_CONNECTOR_VERSION,
	                                                  OperationPlanCounterexample::OTHER_RELATION_IDENTITY,
	                                                  OperationPlanCounterexample::EMPTY_IDENTITY,
	                                                  OperationPlanCounterexample::OTHER_OPERATION_IDENTITY,
	                                                  OperationPlanCounterexample::UNKNOWN_METHOD,
	                                                  OperationPlanCounterexample::EMPTY_PATH,
	                                                  OperationPlanCounterexample::OTHER_PATH,
	                                                  OperationPlanCounterexample::INVALID_QUERY,
	                                                  OperationPlanCounterexample::EMPTY_FIXED_HEADER_VALUE,
	                                                  OperationPlanCounterexample::CASE_VARIANT_AUTHORIZATION_HEADER,
	                                                  OperationPlanCounterexample::DUPLICATE_AUTHORIZATION_HEADERS,
	                                                  OperationPlanCounterexample::HTTP_ORIGIN_SCHEME,
	                                                  OperationPlanCounterexample::OTHER_ORIGIN_HOST,
	                                                  OperationPlanCounterexample::OTHER_ORIGIN_PORT};
	for (std::size_t index = 0; index < sizeof(operations) / sizeof(operations[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildOperationPlanCounterexample("fixture_secret", operations[index]),
		                                 true, suffix++, "operation " + std::to_string(index));
	}

	const AuthenticatedPlanCounterexample authentication[] = {
	    AuthenticatedPlanCounterexample::FEATURE_DISABLED,
	    AuthenticatedPlanCounterexample::REQUIREMENT_NONE,
	    AuthenticatedPlanCounterexample::EMPTY_LOGICAL_BINDING,
	    AuthenticatedPlanCounterexample::AUTHENTICATOR_NONE,
	    AuthenticatedPlanCounterexample::PLACEMENT_NONE,
	    AuthenticatedPlanCounterexample::DESTINATION_ABSENT,
	    AuthenticatedPlanCounterexample::HTTP_DESTINATION_SCHEME,
	    AuthenticatedPlanCounterexample::OTHER_DESTINATION_HOST,
	    AuthenticatedPlanCounterexample::OTHER_DESTINATION_PORT,
	    AuthenticatedPlanCounterexample::MISSING_SECRET_REFERENCE};
	for (std::size_t index = 0; index < sizeof(authentication) / sizeof(authentication[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime,
		                                 BuildAuthenticatedPlanCounterexample("fixture_secret", authentication[index]),
		                                 true, suffix++, "authentication " + std::to_string(index));
	}

	const AnonymousAuthPlanCounterexample anonymous_auth[] = {
	    AnonymousAuthPlanCounterexample::FEATURE_ENABLED,         AnonymousAuthPlanCounterexample::REQUIREMENT_REQUIRED,
	    AnonymousAuthPlanCounterexample::LOGICAL_BINDING_PRESENT, AnonymousAuthPlanCounterexample::AUTHENTICATOR_BEARER,
	    AnonymousAuthPlanCounterexample::AUTHORIZATION_PLACEMENT, AnonymousAuthPlanCounterexample::DESTINATION_PRESENT};
	for (std::size_t index = 0; index < sizeof(anonymous_auth) / sizeof(anonymous_auth[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildAnonymousAuthPlanCounterexample(anonymous_auth[index]), false,
		                                 suffix++, "anonymous auth " + std::to_string(index));
	}
	{
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildAnonymousSecretReferenceCounterexample("fixture_secret"), false,
		                                 suffix++, "anonymous secret");
	}

	const ResponsePlanCounterexample responses[] = {
	    ResponsePlanCounterexample::JSON_PATH_RESPONSE_SOURCE,  ResponsePlanCounterexample::ZERO_TO_MANY_CARDINALITY,
	    ResponsePlanCounterexample::JSON_PATH_BASE_DOMAIN,      ResponsePlanCounterexample::EMPTY_RECORDS_EXTRACTOR,
	    ResponsePlanCounterexample::EMPTY_SCHEMA_NAME,          ResponsePlanCounterexample::UNSUPPORTED_SCHEMA_TYPE,
	    ResponsePlanCounterexample::FLIPPED_SCHEMA_NULLABILITY, ResponsePlanCounterexample::EMPTY_SCHEMA_EXTRACTOR};
	for (std::size_t index = 0; index < sizeof(responses) / sizeof(responses[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildResponsePlanCounterexample("fixture_secret", responses[index]),
		                                 true, suffix++, "response " + std::to_string(index));
	}

	const NetworkPlanCounterexample networks[] = {NetworkPlanCounterexample::EMPTY_SCHEMES,
	                                              NetworkPlanCounterexample::WIDENED_SCHEMES,
	                                              NetworkPlanCounterexample::EMPTY_HOSTS,
	                                              NetworkPlanCounterexample::WIDENED_HOSTS,
	                                              NetworkPlanCounterexample::REDIRECTS_ENABLED,
	                                              NetworkPlanCounterexample::PRIVATE_ADDRESSES_ENABLED,
	                                              NetworkPlanCounterexample::LINK_LOCAL_ADDRESSES_ENABLED,
	                                              NetworkPlanCounterexample::LOOPBACK_ADDRESSES_ENABLED};
	for (std::size_t index = 0; index < sizeof(networks) / sizeof(networks[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildNetworkPlanCounterexample("fixture_secret", networks[index]),
		                                 true, suffix++, "network " + std::to_string(index));
	}

	const FeaturePlanCounterexample features[] = {FeaturePlanCounterexample::PROVIDERS_ENABLED,
	                                              FeaturePlanCounterexample::RETRY_ENABLED,
	                                              FeaturePlanCounterexample::CACHE_ENABLED};
	for (std::size_t index = 0; index < sizeof(features) / sizeof(features[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildFeaturePlanCounterexample("fixture_secret", features[index]),
		                                 true, suffix++, "feature " + std::to_string(index));
	}

	const PaginationPlanCounterexample pagination[] = {PaginationPlanCounterexample::STRATEGY_DISABLED,
	                                                   PaginationPlanCounterexample::UNKNOWN_DEPENDENCY,
	                                                   PaginationPlanCounterexample::UNKNOWN_CONSISTENCY,
	                                                   PaginationPlanCounterexample::UNKNOWN_LINK_RELATION,
	                                                   PaginationPlanCounterexample::UNKNOWN_TARGET_SCOPE,
	                                                   PaginationPlanCounterexample::SUPPORTS_TOTAL,
	                                                   PaginationPlanCounterexample::SUPPORTS_RESUME,
	                                                   PaginationPlanCounterexample::EMPTY_TARGET_PATH,
	                                                   PaginationPlanCounterexample::PAGE_REQUEST_ATTEMPTS_WIDENED,
	                                                   PaginationPlanCounterexample::SCAN_REQUEST_ATTEMPTS_MISMATCH,
	                                                   PaginationPlanCounterexample::SCAN_RESPONSE_BYTES_BELOW_PAGE,
	                                                   PaginationPlanCounterexample::SCAN_DECODED_RECORDS_BELOW_PAGE};
	for (std::size_t index = 0; index < sizeof(pagination) / sizeof(pagination[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		try {
			RequirePlanDeniedBeforeTransport(runtime,
			                                 BuildPaginationPlanCounterexample("fixture_secret", pagination[index]),
			                                 true, suffix++, "pagination " + std::to_string(index));
		} catch (const std::exception &error) {
			throw std::runtime_error("pagination denial index " + std::to_string(index) + ": " + error.what());
		}
	}

	const ResourcePlanCounterexample resources[] = {ResourcePlanCounterexample::REQUEST_ATTEMPTS_ZERO,
	                                                ResourcePlanCounterexample::REQUEST_ATTEMPTS_WIDENED,
	                                                ResourcePlanCounterexample::RESPONSE_BYTES_ZERO,
	                                                ResourcePlanCounterexample::RESPONSE_BYTES_WIDENED,
	                                                ResourcePlanCounterexample::HEADER_BYTES_ZERO,
	                                                ResourcePlanCounterexample::HEADER_BYTES_WIDENED,
	                                                ResourcePlanCounterexample::DECOMPRESSED_BYTES_ZERO,
	                                                ResourcePlanCounterexample::DECOMPRESSED_BYTES_WIDENED,
	                                                ResourcePlanCounterexample::DECODED_RECORDS_ZERO,
	                                                ResourcePlanCounterexample::DECODED_RECORDS_WIDENED,
	                                                ResourcePlanCounterexample::EXTRACTED_STRING_BYTES_ZERO,
	                                                ResourcePlanCounterexample::EXTRACTED_STRING_BYTES_WIDENED,
	                                                ResourcePlanCounterexample::JSON_NESTING_ZERO,
	                                                ResourcePlanCounterexample::JSON_NESTING_WIDENED,
	                                                ResourcePlanCounterexample::DECODED_MEMORY_BYTES_ZERO,
	                                                ResourcePlanCounterexample::DECODED_MEMORY_BYTES_WIDENED,
	                                                ResourcePlanCounterexample::BATCH_ROWS_ZERO,
	                                                ResourcePlanCounterexample::BATCH_ROWS_WIDENED,
	                                                ResourcePlanCounterexample::WALL_MILLISECONDS_ZERO,
	                                                ResourcePlanCounterexample::WALL_MILLISECONDS_WIDENED,
	                                                ResourcePlanCounterexample::CONCURRENCY_ZERO,
	                                                ResourcePlanCounterexample::CONCURRENCY_WIDENED};
	for (std::size_t index = 0; index < sizeof(resources) / sizeof(resources[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime, BuildResourcePlanCounterexample("fixture_secret", resources[index]),
		                                 true, suffix++, "resource " + std::to_string(index));
	}

	const RepositoryPlanCounterexample repositories[] = {
	    RepositoryPlanCounterexample::MISSING_VISIBILITY_COLUMN,
	    RepositoryPlanCounterexample::VISIBILITY_NOT_TRAILING,
	    RepositoryPlanCounterexample::VISIBILITY_NULLABLE,
	    RepositoryPlanCounterexample::VISIBILITY_WRONG_TYPE,
	    RepositoryPlanCounterexample::VISIBILITY_WRONG_EXTRACTOR,
	    RepositoryPlanCounterexample::SELECTIVE_REMOTE_TRUE,
	    RepositoryPlanCounterexample::SELECTIVE_ACCURACY_UNSUPPORTED,
	    RepositoryPlanCounterexample::SELECTIVE_RESIDUAL_TRUE,
	    RepositoryPlanCounterexample::SELECTIVE_RESIDUAL_OWNER_UNKNOWN,
	    RepositoryPlanCounterexample::SELECTIVE_FILTER_OWNER_UNKNOWN,
	    RepositoryPlanCounterexample::SELECTIVE_REMOTE_ORDERING_UNKNOWN,
	    RepositoryPlanCounterexample::UNKNOWN_CONDITIONAL_INPUT,
	    RepositoryPlanCounterexample::BASELINE_REMOTE_VISIBILITY};
	for (std::size_t index = 0; index < sizeof(repositories) / sizeof(repositories[0]); index++) {
		const auto runtime = BuildControlledHttpRuntime();
		RequirePlanDeniedBeforeTransport(runtime,
		                                 BuildRepositoryPlanCounterexample("fixture_secret", repositories[index]), true,
		                                 suffix++, "repository " + std::to_string(index));
	}
}

void TestAuthorizationAlternativeMismatches() {
	ManualHttpExecutionControl control;
	const auto runtime = duckdb_api_test::BuildControlledHttpRuntime();
	RequireHttpExecutionError(
	    [&]() {
		    (void)runtime->Executor()->OpenWithAuthorization(BuildAuthenticatedHttpPlan(),
		                                                     duckdb_api::ScanAuthorization::Anonymous(), control);
	    },
	    duckdb_api::ErrorStage::AUTHENTICATION);
	auto surplus = GeneratedHttpBearerToken(2);
	RequireHttpExecutionError(
	    [&]() {
		    (void)runtime->Executor()->OpenWithAuthorization(
		        BuildAnonymousHttpPlan(), duckdb_api::ScanAuthorization::GithubUserBearer(std::move(surplus)), control);
	    },
	    duckdb_api::ErrorStage::AUTHENTICATION);
	RequireHttpExecutionError([&]() { (void)runtime->Executor()->Open(BuildAuthenticatedHttpPlan(), control); },
	                          duckdb_api::ErrorStage::AUTHENTICATION);
	Require(runtime->Observation().request_count == 0, "authorization mismatch reached transport");
}

duckdb_api::internal::HttpRequest FixedAuthenticatedRequest() {
	duckdb_api::internal::HttpRequest request;
	request.method = "GET";
	request.scheme = "https";
	request.host = "api.github.com";
	request.port = 443;
	request.target = "/user";
	request.headers = {{"Accept", "application/vnd.github+json"},
	                   {"User-Agent", "duckdb-api/0.6.0"},
	                   {"X-GitHub-Api-Version", "2022-11-28"}};
	return request;
}

void RequireFinalRequestDenied(duckdb_api::internal::HttpRequest request, uint64_t suffix) {
	auto token = GeneratedHttpBearerToken(suffix);
	auto authorization = duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token));
	RequireHttpExecutionError(
	    [&]() {
		    (void)duckdb_api::internal::FixedGithubUserBearerAuthenticator::Authorize(
		        BuildAuthenticatedHttpPlan(), std::move(request), authorization);
	    },
	    duckdb_api::ErrorStage::POLICY);
}

void TestFixedAuthenticatorRevalidatesFinalRequest() {
	auto wrong_method = FixedAuthenticatedRequest();
	wrong_method.method = "POST";
	RequireFinalRequestDenied(std::move(wrong_method), 500);
	auto wrong_path = FixedAuthenticatedRequest();
	wrong_path.target = "/other";
	RequireFinalRequestDenied(std::move(wrong_path), 501);
	auto wrong_host = FixedAuthenticatedRequest();
	wrong_host.host = "other.example";
	RequireFinalRequestDenied(std::move(wrong_host), 502);
	auto case_variant = FixedAuthenticatedRequest();
	case_variant.headers.push_back({"authorization", "test-only-redacted"});
	RequireFinalRequestDenied(std::move(case_variant), 503);
	auto duplicate = FixedAuthenticatedRequest();
	duplicate.headers.push_back({"Authorization", "test-only-redacted"});
	duplicate.headers.push_back({"Authorization", "test-only-redacted"});
	RequireFinalRequestDenied(std::move(duplicate), 504);
}

void TestExecutionProfileNeverWidensRecordAuthority() {
	const uint64_t narrower_record_authority = 2;
	const auto runtime =
	    duckdb_api_test::BuildControlledHttpRuntime(duckdb_api::MAX_EXECUTION_MILLISECONDS, narrower_record_authority);
	runtime->Respond(200, duckdb_api_test::ThreeHttpRows());
	ManualHttpExecutionControl control;
	Require(BuildAnonymousHttpPlan().Budgets().decoded_records == 3,
	        "record-authority counterexample did not retain the valid product plan");
	RequireHttpExecutionError([&]() { (void)runtime->Executor()->Open(BuildAnonymousHttpPlan(), control); },
	                          duckdb_api::ErrorStage::POLICY);
	Require(runtime->Observation().request_count == 0, "plan wider than executor authority reached transport");

	RequireHttpExecutionError(
	    [&]() { (void)duckdb_api_test::BuildControlledHttpRuntime(duckdb_api::MAX_EXECUTION_MILLISECONDS, 0); },
	    duckdb_api::ErrorStage::INTERNAL);
	RequireHttpExecutionError(
	    [&]() {
		    (void)duckdb_api_test::BuildControlledHttpRuntime(duckdb_api::MAX_EXECUTION_MILLISECONDS,
		                                                      duckdb_api::PAGINATION_MAX_DECODED_RECORDS_PER_PAGE + 1);
	    },
	    duckdb_api::ErrorStage::INTERNAL);
}

void TestRepositoryAdmissionProducesOneClosedRequestProfile() {
	const duckdb_api::internal::HttpExecutionProfile execution_profile {duckdb_api::PlannedUrlScheme::HTTPS,
	                                                                    "api.github.com",
	                                                                    443,
	                                                                    false,
	                                                                    false,
	                                                                    false,
	                                                                    duckdb_api::MAX_EXECUTION_MILLISECONDS,
	                                                                    100};
	auto base = duckdb_api::internal::TryAdmitRepositoryHttpPlan(
	    duckdb_api_test::BuildValidAuthenticatedRepositoriesPlanFixture("fixture_secret"), execution_profile);
	auto selective = duckdb_api::internal::TryAdmitRepositoryHttpPlan(
	    duckdb_api_test::BuildVisibilityPrivatePlanFixture("fixture_secret"), execution_profile);
	auto selective_complete = duckdb_api::internal::TryAdmitRepositoryHttpPlan(
	    duckdb_api_test::BuildVisibilityPrivateCompleteResidualPlanFixture("fixture_secret"), execution_profile);
	auto fallback_complete = duckdb_api::internal::TryAdmitRepositoryHttpPlan(
	    duckdb_api_test::BuildCompleteResidualFallbackPlanFixture("fixture_secret"), execution_profile);
	Require(base && selective && selective_complete && fallback_complete && base->Columns().size() == 6 &&
	            base->Columns()[5].name == "visibility" && base->Columns()[5].kind == duckdb_api::ValueKind::VARCHAR &&
	            base->Method() == "GET" && base->Scheme() == "https" && base->Host() == "api.github.com" &&
	            base->Port() == 443 && base->Path() == "/user/repos" && base->Headers().size() == 3 &&
	            base->PageSizeParameter() == "per_page" && base->PageSize() == 100 &&
	            base->PageNumberParameter() == "page" && base->FirstPage() == 1 && base->PageIncrement() == 1 &&
	            base->MaxPages() == 32 &&
	            base->ConditionalInput() == duckdb_api::internal::AdmittedRepositoryConditionalInput::NONE &&
	            selective->ConditionalInput() ==
	                duckdb_api::internal::AdmittedRepositoryConditionalInput::VISIBILITY_PRIVATE &&
	            selective_complete->ConditionalInput() ==
	                duckdb_api::internal::AdmittedRepositoryConditionalInput::VISIBILITY_PRIVATE &&
	            fallback_complete->ConditionalInput() == duckdb_api::internal::AdmittedRepositoryConditionalInput::NONE,
	        "repository admission did not produce the complete closed immutable profile");
	Require(duckdb_api::internal::BuildAdmittedRepositoryPageRequest(*base, 2).target ==
	                "/user/repos?per_page=100&page=2" &&
	            duckdb_api::internal::BuildAdmittedRepositoryPageRequest(*fallback_complete, 2).target ==
	                "/user/repos?per_page=100&page=2" &&
	            duckdb_api::internal::BuildAdmittedRepositoryPageRequest(*selective, 2).target ==
	                "/user/repos?per_page=100&page=2&visibility=private" &&
	            duckdb_api::internal::BuildAdmittedRepositoryPageRequest(*selective_complete, 2).target ==
	                "/user/repos?per_page=100&page=2&visibility=private",
	        "admitted request builder did not distinguish absent and selected conditional input");
	RequireHttpExecutionError([&]() { (void)duckdb_api::internal::BuildAdmittedRepositoryPageRequest(*selective, 0); },
	                          duckdb_api::ErrorStage::POLICY);
}

void TestNullTransportRejected() {
	RequireHttpExecutionError(
	    [&]() { duckdb_api::internal::BuildHttpScanExecutor(std::unique_ptr<duckdb_api::internal::HttpTransport>()); },
	    duckdb_api::ErrorStage::INTERNAL);
}

} // namespace

int main() {
	try {
		TestProviderOwnedPlanDenialMatrix();
		TestAuthorizationAlternativeMismatches();
		TestFixedAuthenticatorRevalidatesFinalRequest();
		TestExecutionProfileNeverWidensRecordAuthority();
		TestRepositoryAdmissionProducesOneClosedRequestProfile();
		TestNullTransportRejected();
		std::cout << "HTTP scan executor policy tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "HTTP scan executor policy tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
