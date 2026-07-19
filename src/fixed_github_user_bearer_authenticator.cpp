#include "duckdb_api/internal/fixed_github_user_bearer_authenticator.hpp"

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/request_header_budget.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

char AsciiLower(char value) noexcept {
	return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool EqualsAsciiIgnoreCase(const std::string &left, const std::string &right) noexcept {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (AsciiLower(left[index]) != AsciiLower(right[index])) {
			return false;
		}
	}
	return true;
}

bool HasFixedHeadersWithoutAuthorization(const std::vector<HttpHeader> &headers, const std::string &user_agent) {
	if (headers.size() != 3 || headers[0].name != "Accept" || headers[0].value != "application/vnd.github+json" ||
	    headers[1].name != "User-Agent" || headers[1].value != user_agent ||
	    headers[2].name != "X-GitHub-Api-Version" || headers[2].value != "2022-11-28") {
		return false;
	}
	for (std::size_t index = 0; index < headers.size(); index++) {
		if (EqualsAsciiIgnoreCase(headers[index].name, "Authorization")) {
			return false;
		}
		for (std::size_t other = index + 1; other < headers.size(); other++) {
			if (EqualsAsciiIgnoreCase(headers[index].name, headers[other].name)) {
				return false;
			}
		}
	}
	return true;
}

bool HasFixedPlanHeaders(const std::vector<PlannedHttpHeader> &headers, const std::string &user_agent) {
	return headers.size() == 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == user_agent &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool HasFixedBearerObligation(const ScanPlan &plan) {
	const auto &obligation = plan.AuthenticationObligation();
	const auto *destination = obligation.Destination();
	return plan.Authentication() == FeatureState::ENABLED && plan.SecretReference().IsPresent() &&
	       obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       destination->scheme == PlannedUrlScheme::HTTPS && destination->host == "api.github.com" &&
	       destination->port == 443;
}

bool IsFixedAuthenticatedUserPlan(const ScanPlan &plan) {
	const auto &operation = plan.Operation();
	return plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.5.0" &&
	       plan.RelationName() == "authenticated_user" && operation.operation_name == "github_authenticated_user" &&
	       operation.protocol == PlannedProtocol::REST && operation.method == PlannedHttpMethod::GET &&
	       operation.cardinality == PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.origin.scheme == PlannedUrlScheme::HTTPS &&
	       operation.origin.host == "api.github.com" && operation.origin.port == 443 && operation.path == "/user" &&
	       operation.query_parameters.empty() && HasFixedPlanHeaders(operation.headers, "duckdb-api/0.5.0") &&
	       operation.response_source == PlannedResponseSource::ROOT_OBJECT && operation.records_extractor == "$" &&
	       HasFixedBearerObligation(plan);
}

bool IsCanonicalRepositoryTarget(const std::string &target) {
	const std::string prefix = "/user/repos?per_page=100&page=";
	if (target.compare(0, prefix.size(), prefix) != 0 || target.size() == prefix.size() ||
	    target[prefix.size()] == '0') {
		return false;
	}
	uint64_t page = 0;
	for (std::size_t index = prefix.size(); index < target.size(); index++) {
		const auto character = target[index];
		if (character < '0' || character > '9') {
			return false;
		}
		const auto digit = static_cast<uint64_t>(character - '0');
		if (page > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
			return false;
		}
		page = page * 10 + digit;
	}
	return page > 0;
}

bool IsFixedRepositoriesPlan(const ScanPlan &plan) {
	const auto &operation = plan.Operation();
	if (plan.ConnectorName() != "github" || plan.ConnectorVersion() != "0.5.0" ||
	    plan.RelationName() != "authenticated_repositories" ||
	    operation.operation_name != "github_authenticated_repositories" ||
	    operation.protocol != PlannedProtocol::REST || operation.method != PlannedHttpMethod::GET ||
	    operation.cardinality != PlannedCardinality::ZERO_TO_MANY ||
	    operation.replay_safety != PlannedReplaySafety::SAFE || operation.origin.scheme != PlannedUrlScheme::HTTPS ||
	    operation.origin.host != "api.github.com" || operation.origin.port != 443 || operation.path != "/user/repos" ||
	    operation.query_parameters.size() != 2 || operation.query_parameters[0].name != "per_page" ||
	    operation.query_parameters[0].encoded_value != "100" || operation.query_parameters[1].name != "page" ||
	    operation.query_parameters[1].encoded_value != "1" ||
	    !HasFixedPlanHeaders(operation.headers, "duckdb-api/0.5.0") ||
	    operation.response_source != PlannedResponseSource::ROOT_ARRAY || operation.records_extractor != "$" ||
	    !HasFixedBearerObligation(plan)) {
		return false;
	}
	const auto &pagination = plan.Pagination();
	const auto &target = pagination.Target();
	return pagination.Strategy() == PlannedPaginationStrategy::LINK_HEADER &&
	       pagination.Dependency() == PlannedPageDependency::SEQUENTIAL &&
	       pagination.Consistency() == PlannedPageConsistency::MUTABLE &&
	       pagination.LinkRelation() == PlannedLinkRelation::NEXT &&
	       pagination.TargetScope() == PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH &&
	       !pagination.SupportsTotal() && !pagination.SupportsResume() &&
	       target.origin.scheme == PlannedUrlScheme::HTTPS && target.origin.host == "api.github.com" &&
	       target.origin.port == 443 && target.path == "/user/repos" && target.page_size_parameter == "per_page" &&
	       target.page_size == 100 && target.page_number_parameter == "page" && target.first_page == 1 &&
	       target.page_increment == 1;
}

bool IsFixedAuthenticatedRequest(const HttpRequest &request, bool repositories) {
	return request.method == "GET" && request.scheme == "https" && request.host == "api.github.com" &&
	       request.port == 443 &&
	       (repositories ? IsCanonicalRepositoryTarget(request.target) : request.target == "/user") &&
	       HasFixedHeadersWithoutAuthorization(request.headers, "duckdb-api/0.5.0");
}

} // namespace

HttpRequest FixedGithubUserBearerAuthenticator::Authorize(const ScanPlan &plan, HttpRequest request,
                                                          const ScanAuthorization &authorization) {
	// Destination, operation, and placement are checked while the token remains
	// opaque. A denied plan therefore cannot materialize credential bytes in a
	// request header, even locally.
	const bool user_profile = IsFixedAuthenticatedUserPlan(plan);
	const bool repository_profile = IsFixedRepositoriesPlan(plan);
	if ((!user_profile && !repository_profile) || !IsFixedAuthenticatedRequest(request, repository_profile)) {
		throw ExecutionError(ErrorStage::POLICY, "authorization",
		                     "bearer authorization is outside the installed execution profile");
	}

	auto bearer_value = CopyToken(authorization);
	uint64_t header_bytes = 0;
	for (const auto &header : request.headers) {
		if (!TryAccumulateRequestHeaderBytes(plan.Budgets().header_bytes, header.name.size(), header.value.size(),
		                                     header_bytes)) {
			throw ExecutionError(ErrorStage::RESOURCE, "header_bytes",
			                     "HTTP request headers exceed the 16384-byte aggregate limit");
		}
	}
	if (bearer_value.size() > ScanAuthorization::GithubUserBearerTokenByteLimit() ||
	    !TryAccumulateRequestHeaderBytes(plan.Budgets().header_bytes, sizeof("Authorization") - 1,
	                                     (sizeof("Bearer ") - 1) + bearer_value.size(), header_bytes)) {
		throw ExecutionError(ErrorStage::RESOURCE, "header_bytes",
		                     "HTTP request headers exceed the 16384-byte aggregate limit");
	}
	try {
		bearer_value.insert(0, "Bearer ");
		HttpHeader authorization_header;
		authorization_header.name = "Authorization";
		authorization_header.value = std::move(bearer_value);
		request.headers.push_back(std::move(authorization_header));
		return request;
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "authorization",
		                     "authorization header could not be allocated within its memory budget");
	}
}

} // namespace internal
} // namespace duckdb_api
