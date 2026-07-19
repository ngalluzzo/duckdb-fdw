#include "duckdb_api/internal/fixed_github_user_bearer_authenticator.hpp"

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/request_header_budget.hpp"

#include <cstddef>
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

bool HasFixedHeadersWithoutAuthorization(const std::vector<HttpHeader> &headers) {
	if (headers.size() != 3 || headers[0].name != "Accept" || headers[0].value != "application/vnd.github+json" ||
	    headers[1].name != "User-Agent" || headers[1].value != "duckdb-api/0.4.0" ||
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

bool HasFixedPlanHeaders(const std::vector<PlannedHttpHeader> &headers) {
	return headers.size() == 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.4.0" &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool IsFixedAuthenticatedPlan(const ScanPlan &plan) {
	const auto &operation = plan.Operation();
	const auto &obligation = plan.AuthenticationObligation();
	const auto *destination = obligation.Destination();
	return plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.4.0" &&
	       plan.RelationName() == "authenticated_user" && plan.Authentication() == FeatureState::ENABLED &&
	       plan.SecretReference().IsPresent() && operation.operation_name == "github_authenticated_user" &&
	       operation.protocol == PlannedProtocol::REST && operation.method == PlannedHttpMethod::GET &&
	       operation.cardinality == PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.origin.scheme == PlannedUrlScheme::HTTPS &&
	       operation.origin.host == "api.github.com" && operation.origin.port == 443 && operation.path == "/user" &&
	       operation.query_parameters.empty() && HasFixedPlanHeaders(operation.headers) &&
	       operation.response_source == PlannedResponseSource::ROOT_OBJECT && operation.records_extractor == "$" &&
	       obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       destination->scheme == PlannedUrlScheme::HTTPS && destination->host == "api.github.com" &&
	       destination->port == 443;
}

bool IsFixedAuthenticatedRequest(const HttpRequest &request) {
	return request.method == "GET" && request.scheme == "https" && request.host == "api.github.com" &&
	       request.port == 443 && request.target == "/user" && HasFixedHeadersWithoutAuthorization(request.headers);
}

} // namespace

HttpRequest FixedGithubUserBearerAuthenticator::Authorize(const ScanPlan &plan, HttpRequest request,
                                                          ScanAuthorization authorization) {
	// Destination, operation, and placement are checked while the token remains
	// opaque. A denied plan therefore cannot materialize credential bytes in a
	// request header, even locally.
	if (!IsFixedAuthenticatedPlan(plan) || !IsFixedAuthenticatedRequest(request)) {
		throw ExecutionError(ErrorStage::POLICY, "authorization",
		                     "bearer authorization is outside the installed execution profile");
	}

	auto bearer_value = Consume(authorization);
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
