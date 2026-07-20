#include "duckdb_api/authorization.hpp"
#include "duckdb_api/internal/runtime/authentication/fixed_github_user_bearer_authenticator.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/policy/scan_resource_accounting.hpp"
#include "duckdb_api/internal/runtime/transport/graphql_request_body.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

duckdb_api::internal::HttpExecutionProfile PublicProfile() {
	return {duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443, false, false, false, 30000, 100};
}

void TestValidPlanProducesClosedProfileAndCanonicalBodies() {
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("any_exact_secret_name");
	auto admitted = duckdb_api::internal::TryAdmitGraphqlPlan(plan, PublicProfile());
	Require(static_cast<bool>(admitted), "valid GraphQL fixture must be admitted");
	Require(admitted->Method() == "POST" && admitted->Scheme() == "https" && admitted->Host() == "api.github.com" &&
	            admitted->Port() == 443 && admitted->Path() == "/graphql" && admitted->Columns().size() == 8 &&
	            admitted->PageSize() == 100 && admitted->MaxPages() == 32,
	        "admitted GraphQL profile must own exact execution facts");

	const auto first = duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, nullptr);
	Require(first.method == "POST" && first.target == "/graphql" && first.content_type == "application/json" &&
	            first.headers.size() == 3 &&
	            first.body.find("{\"query\":\"query DuckdbApiViewerRepositoryMetrics") == 0 &&
	            first.body.find("\",\"variables\":{\"pageSize\":100,\"cursor\":null}}") != std::string::npos,
	        "first GraphQL body must be compact and use a null cursor");
	const std::string cursor = "quote\"slash\\line\n";
	const auto next = duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, &cursor);
	Require(next.body.find("\"cursor\":\"quote\\\"slash\\\\line\\n\"}}") != std::string::npos,
	        "cursor must use deterministic JSON escaping");
	Require(duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(first.body) &&
	            duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(next.body),
	        "installed transport body classifier must accept both canonical cursor alternatives");
	auto drifted = next.body;
	const auto cursor_escape = drifted.find("\\n", drifted.find("\"cursor\""));
	Require(cursor_escape != std::string::npos, "escaped cursor fixture lost its newline");
	drifted.replace(cursor_escape, 2, "\\/");
	Require(!duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(drifted),
	        "installed transport body classifier must reject noncanonical cursor escaping");
	auto empty_body = first.body;
	const auto null_cursor = empty_body.rfind("null}}");
	Require(null_cursor != std::string::npos, "null cursor fixture drifted");
	empty_body.replace(null_cursor, 4, "\"\"");
	Require(!duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(empty_body),
	        "installed request classifier must reject an empty non-null continuation cursor");
	Require(first.body != next.body, "only cursor state may change between canonical bodies");
	const std::string exact_first =
	    "{\"query\":\"query DuckdbApiViewerRepositoryMetrics($pageSize: Int!, $cursor: String) {\\n"
	    "  viewer {\\n"
	    "    repositories(\\n"
	    "      first: $pageSize\\n"
	    "      after: $cursor\\n"
	    "      affiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]\\n"
	    "      ownerAffiliations: [OWNER, COLLABORATOR, ORGANIZATION_MEMBER]\\n"
	    "      orderBy: {field: UPDATED_AT, direction: DESC}\\n"
	    "    ) {\\n"
	    "      nodes {\\n"
	    "        id\\n"
	    "        nameWithOwner\\n"
	    "        owner { login }\\n"
	    "        stargazerCount\\n"
	    "        primaryLanguage { name }\\n"
	    "        isPrivate\\n"
	    "        isArchived\\n"
	    "        updatedAt\\n"
	    "      }\\n"
	    "      pageInfo { hasNextPage endCursor }\\n"
	    "    }\\n"
	    "  }\\n"
	    "}\",\"variables\":{\"pageSize\":100,\"cursor\":null}}";
	Require(first.body == exact_first, "first GraphQL request bytes drifted from the external compact oracle");
}

void TestRequestBodyExactOneOverAggregateAndPreBearerOrdering() {
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("body_accounting_secret");
	auto admitted = duckdb_api::internal::TryAdmitGraphqlPlan(plan, PublicProfile());
	Require(static_cast<bool>(admitted), "body-accounting plan must be admitted");
	auto request = duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, nullptr);
	const auto body_bytes = static_cast<uint64_t>(request.body.size());
	using duckdb_api::internal::ScanResourceAccounting;
	using duckdb_api::internal::ScanResourceError;
	using duckdb_api::internal::ScanResourceProfile;
	const auto now = std::chrono::steady_clock::time_point();
	const ScanResourceProfile exact_profile {{1, 16384, 1024, 1024, 100, 1024, 1, body_bytes},
	                                         {2, 2, 32768, 2048, 2048, 200, 1024, 30000, 1, body_bytes * 2}};
	ScanResourceAccounting exact(exact_profile);
	for (std::size_t page = 0; page < 2; page++) {
		const auto allowance = exact.BeginPage(now);
		Require(allowance.serialized_request_body_bytes == body_bytes,
		        "exact GraphQL body allowance did not survive page/scan intersection");
		exact.CommitRequestBody(body_bytes);
		exact.CommitTransport({0, 0, 0});
		exact.CommitDecodedPage({0, 0});
		exact.CompletePage(page == 0, now);
	}
	Require(exact.Counters().serialized_request_body_bytes == body_bytes * 2,
	        "GraphQL aggregate body debit did not reach its exact scan boundary");

	auto one_under_profile = exact_profile;
	one_under_profile.page.serialized_request_body_bytes--;
	one_under_profile.scan.serialized_request_body_bytes = one_under_profile.page.serialized_request_body_bytes;
	one_under_profile.scan.request_attempts = 1;
	one_under_profile.scan.pages = 1;
	ScanResourceAccounting one_under(one_under_profile);
	one_under.BeginPage(now);
	bool rejected = false;
	try {
		one_under.CommitRequestBody(body_bytes);
	} catch (const ScanResourceError &error) {
		rejected = error.Field() == "request_body_bytes";
	}
	Require(rejected && one_under.Counters().serialized_request_body_bytes == 0,
	        "one-byte-over GraphQL body must fail without an aggregate debit");
	for (const auto &header : request.headers) {
		Require(header.name != "Authorization", "body debit failure occurred after bearer placement");
	}
	const auto authorization = duckdb_api::ScanAuthorization::GithubUserBearer("synthetic_body_order_token");
	auto arbitrary = request;
	const auto arbitrary_cursor = arbitrary.body.rfind("null}}");
	Require(arbitrary_cursor != std::string::npos, "arbitrary cursor fixture drifted");
	arbitrary.body.replace(arbitrary_cursor, 4, "\"\"");
	try {
		(void)duckdb_api::internal::FixedGithubUserBearerAuthenticator::AuthorizeGraphql(*admitted, 16384, arbitrary,
		                                                                                 authorization);
		throw std::runtime_error("arbitrary nonempty GraphQL body acquired bearer authority");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::POLICY && error.Field() == "authorization" &&
		            arbitrary.headers.size() == admitted->Headers().size(),
		        "arbitrary body was not rejected before bearer placement");
	}
	auto authorized = duckdb_api::internal::FixedGithubUserBearerAuthenticator::AuthorizeGraphql(
	    *admitted, 16384, std::move(request), authorization);
	Require(authorized.headers.size() == admitted->Headers().size() + 1 &&
	            authorized.headers.back().name == "Authorization",
	        "valid exact body did not remain authorizable after the failed side-effect-free debit probe");
	const std::string cursor = "exact-later-cursor";
	auto later = duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, &cursor);
	auto authorized_later = duckdb_api::internal::FixedGithubUserBearerAuthenticator::AuthorizeGraphql(
	    *admitted, 16384, std::move(later), authorization);
	Require(authorized_later.headers.size() == admitted->Headers().size() + 1 &&
	            authorized_later.body.find("\"cursor\":\"exact-later-cursor\"") != std::string::npos,
	        "valid exact later-page body lost bearer authority");

	const std::string exact_cursor(512, 'x');
	auto exact_cursor_request = duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, &exact_cursor);
	Require(duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(exact_cursor_request.body),
	        "exact 512-byte decoded cursor must remain canonical");
	auto exact_cursor_authorized = duckdb_api::internal::FixedGithubUserBearerAuthenticator::AuthorizeGraphql(
	    *admitted, 16384, std::move(exact_cursor_request), authorization);
	Require(exact_cursor_authorized.headers.back().name == "Authorization",
	        "exact 512-byte decoded cursor lost bearer authority");

	const std::string oversized_cursor(513, 'x');
	try {
		(void)duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, &oversized_cursor);
		throw std::runtime_error("513-byte decoded cursor entered a request body");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::POLICY && error.Field() == "pagination.cursor",
		        "513-byte decoded cursor used the wrong pre-bearer denial");
	}
	auto oversized_body = exact_cursor_authorized;
	oversized_body.headers.pop_back();
	const auto cursor_end = oversized_body.body.rfind("\"}}");
	Require(cursor_end != std::string::npos, "oversized cursor body fixture drifted");
	oversized_body.body.insert(cursor_end, "x");
	Require(!duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(oversized_body.body),
	        "513-byte decoded cursor passed canonical body classification");
	try {
		(void)duckdb_api::internal::FixedGithubUserBearerAuthenticator::AuthorizeGraphql(*admitted, 16384,
		                                                                                 oversized_body, authorization);
		throw std::runtime_error("513-byte decoded cursor acquired bearer authority");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::POLICY && oversized_body.headers.size() == 3,
		        "513-byte decoded cursor was not rejected before bearer placement");
	}

	std::string invalid_utf8;
	invalid_utf8.push_back(static_cast<char>(0xc0));
	invalid_utf8.push_back(static_cast<char>(0xaf));
	try {
		(void)duckdb_api::internal::BuildAdmittedGraphqlRequest(*admitted, &invalid_utf8);
		throw std::runtime_error("invalid UTF-8 cursor entered a request body");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::POLICY && error.Field() == "pagination.cursor",
		        "invalid UTF-8 cursor used the wrong pre-bearer denial");
	}
	auto invalid_utf8_body = exact_cursor_authorized;
	invalid_utf8_body.headers.pop_back();
	const auto token_begin =
	    invalid_utf8_body.body.find(std::string(4, 'x'), invalid_utf8_body.body.find("\"cursor\":"));
	Require(token_begin != std::string::npos, "invalid UTF-8 body fixture drifted");
	invalid_utf8_body.body.replace(token_begin, 2, invalid_utf8);
	Require(!duckdb_api::internal::IsCanonicalAdmittedGraphqlBody(invalid_utf8_body.body),
	        "invalid UTF-8 cursor passed canonical body classification");
	try {
		(void)duckdb_api::internal::FixedGithubUserBearerAuthenticator::AuthorizeGraphql(
		    *admitted, 16384, invalid_utf8_body, authorization);
		throw std::runtime_error("invalid UTF-8 cursor acquired bearer authority");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::POLICY && invalid_utf8_body.headers.size() == 3,
		        "invalid UTF-8 cursor was not rejected before bearer placement");
	}
}

void TestEveryProviderCounterexampleFailsClosed() {
	const auto count = static_cast<std::size_t>(duckdb_api_test::GraphqlRuntimeAdmissionCounterexample::COUNT);
	Require(count == 143, "Runtime must exercise the complete Semantics counterexample corpus");
	for (std::size_t value = 0; value < count; value++) {
		const auto counterexample = static_cast<duckdb_api_test::GraphqlRuntimeAdmissionCounterexample>(value);
		const auto plan =
		    duckdb_api_test::BuildGraphqlRuntimeAdmissionCounterexample("counterexample_secret", counterexample);
		auto admitted = duckdb_api::internal::TryAdmitGraphqlPlan(plan, PublicProfile());
		if (admitted) {
			throw std::runtime_error("GraphQL admission accepted Semantics counterexample " + std::to_string(value));
		}
	}
}

void TestDocumentAdmissionRequiresIntegrityAndCanonicalMembership() {
	using Counterexample = duckdb_api_test::GraphqlRuntimeAdmissionCounterexample;
	const auto wrong_digest = duckdb_api_test::BuildGraphqlRuntimeAdmissionCounterexample(
	    "digest_secret", Counterexample::OTHER_DOCUMENT_DIGEST);
	Require(!duckdb_api::internal::TryAdmitGraphqlPlan(wrong_digest, PublicProfile()),
	        "Runtime must recompute the digest of the admitted document bytes");

	const auto coherent_drift = duckdb_api_test::BuildGraphqlRuntimeAdmissionCounterexample(
	    "digest_secret", Counterexample::CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST);
	Require(!duckdb_api::internal::TryAdmitGraphqlPlan(coherent_drift, PublicProfile()),
	        "a self-consistent document and digest must not replace canonical-profile membership");
}

} // namespace

int main() {
	try {
		TestValidPlanProducesClosedProfileAndCanonicalBodies();
		TestRequestBodyExactOneOverAggregateAndPreBearerOrdering();
		TestDocumentAdmissionRequiresIntegrityAndCanonicalMembership();
		TestEveryProviderCounterexampleFailsClosed();
		std::cout << "GraphQL plan admission tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
