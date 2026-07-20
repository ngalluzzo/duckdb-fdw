#include "duckdb_api/internal/runtime/authentication/fixed_github_user_bearer_authenticator.hpp"

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/policy/request_header_budget.hpp"

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
	if (plan.Operation().Protocol() != PlannedProtocol::REST) {
		return false;
	}
	const auto &operation = plan.Operation().Rest();
	return plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.7.0" &&
	       plan.RelationName() == "authenticated_user" && operation.operation_name == "github_authenticated_user" &&
	       operation.method == PlannedHttpMethod::GET &&
	       operation.cardinality == PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	       operation.replay_safety == PlannedReplaySafety::SAFE && operation.origin.scheme == PlannedUrlScheme::HTTPS &&
	       operation.origin.host == "api.github.com" && operation.origin.port == 443 && operation.path == "/user" &&
	       operation.query_parameters.empty() && HasFixedPlanHeaders(operation.headers, "duckdb-api/0.6.0") &&
	       operation.response_source == PlannedResponseSource::ROOT_OBJECT && operation.records_extractor == "$" &&
	       HasFixedBearerObligation(plan);
}

bool IsCanonicalAdmittedRepositoryTarget(const AdmittedRepositoryRequestProfile &profile, const std::string &target) {
	const std::string prefix = profile.Path() + "?" + profile.PageSizeParameter() + "=" +
	                           std::to_string(profile.PageSize()) + "&" + profile.PageNumberParameter() + "=";
	if (target.compare(0, prefix.size(), prefix) != 0 || target.size() == prefix.size() ||
	    target[prefix.size()] == '0') {
		return false;
	}
	const std::string suffix = profile.ConditionalInput() == AdmittedRepositoryConditionalInput::VISIBILITY_PRIVATE
	                               ? "&visibility=private"
	                               : "";
	const auto digits_end = suffix.empty() ? target.size() : target.size() - suffix.size();
	if (digits_end <= prefix.size() || (!suffix.empty() && target.compare(digits_end, suffix.size(), suffix) != 0)) {
		return false;
	}
	uint64_t page = 0;
	for (std::size_t index = prefix.size(); index < digits_end; index++) {
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

bool IsFixedAdmittedRepositoryRequest(const AdmittedRepositoryRequestProfile &profile, const HttpRequest &request) {
	if (request.method != profile.Method() || request.scheme != profile.Scheme() || request.host != profile.Host() ||
	    request.port != profile.Port() || request.headers.size() != profile.Headers().size() || !request.body.empty() ||
	    !request.content_type.empty() || !IsCanonicalAdmittedRepositoryTarget(profile, request.target)) {
		return false;
	}
	for (std::size_t index = 0; index < request.headers.size(); index++) {
		if (request.headers[index].name != profile.Headers()[index].name ||
		    request.headers[index].value != profile.Headers()[index].value ||
		    EqualsAsciiIgnoreCase(request.headers[index].name, "Authorization")) {
			return false;
		}
	}
	return true;
}

bool IsFixedAuthenticatedUserRequest(const HttpRequest &request) {
	return request.method == "GET" && request.scheme == "https" && request.host == "api.github.com" &&
	       request.port == 443 && request.target == "/user" && request.body.empty() && request.content_type.empty() &&
	       HasFixedHeadersWithoutAuthorization(request.headers, "duckdb-api/0.6.0");
}

} // namespace

HttpRequest FixedGithubUserBearerAuthenticator::Authorize(const ScanPlan &plan, HttpRequest request,
                                                          const ScanAuthorization &authorization) {
	// Destination, operation, and placement are checked while the token remains
	// opaque. A denied plan therefore cannot materialize credential bytes in a
	// request header, even locally.
	if (!IsFixedAuthenticatedUserPlan(plan) || !IsFixedAuthenticatedUserRequest(request)) {
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

HttpRequest FixedGithubUserBearerAuthenticator::AuthorizeRepository(const AdmittedRepositoryRequestProfile &profile,
                                                                    uint64_t max_header_bytes, HttpRequest request,
                                                                    const ScanAuthorization &authorization) {
	if (!IsFixedAdmittedRepositoryRequest(profile, request)) {
		throw ExecutionError(ErrorStage::POLICY, "authorization",
		                     "bearer authorization is outside the installed execution profile");
	}
	auto bearer_value = CopyToken(authorization);
	uint64_t header_bytes = 0;
	for (const auto &header : request.headers) {
		if (!TryAccumulateRequestHeaderBytes(max_header_bytes, header.name.size(), header.value.size(), header_bytes)) {
			throw ExecutionError(ErrorStage::RESOURCE, "header_bytes",
			                     "HTTP request headers exceed the 16384-byte aggregate limit");
		}
	}
	if (bearer_value.size() > ScanAuthorization::GithubUserBearerTokenByteLimit() ||
	    !TryAccumulateRequestHeaderBytes(max_header_bytes, sizeof("Authorization") - 1,
	                                     (sizeof("Bearer ") - 1) + bearer_value.size(), header_bytes)) {
		throw ExecutionError(ErrorStage::RESOURCE, "header_bytes",
		                     "HTTP request headers exceed the 16384-byte aggregate limit");
	}
	try {
		bearer_value.insert(0, "Bearer ");
		request.headers.push_back({"Authorization", std::move(bearer_value)});
		return request;
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "authorization",
		                     "authorization header could not be allocated within its memory budget");
	}
}

} // namespace internal
} // namespace duckdb_api
