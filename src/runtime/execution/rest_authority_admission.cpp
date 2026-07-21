#include "duckdb_api/internal/runtime/execution/rest_authority_admission.hpp"

#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

namespace duckdb_api {
namespace internal {
namespace {

bool HasAuthentication(const ScanPlan &plan, bool &requires_bearer) {
	const auto &obligation = plan.AuthenticationObligation();
	const auto &origin = plan.Operation().Rest().origin;
	const auto *destination = obligation.Destination();
	if (plan.Authentication() == FeatureState::DISABLED) {
		requires_bearer = false;
		return !plan.SecretReference().IsPresent() && obligation.Requirement() == PlannedCredentialRequirement::NONE &&
		       obligation.LogicalCredential().empty() && obligation.Authenticator() == PlannedAuthenticator::NONE &&
		       obligation.Placement() == PlannedCredentialPlacement::NONE && destination == nullptr;
	}
	if (plan.Authentication() != FeatureState::ENABLED) {
		return false;
	}
	requires_bearer = true;
	return plan.SecretReference().IsPresent() && obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	       obligation.LogicalCredential() == "token" && obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	       obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       destination->scheme == origin.scheme && destination->host == origin.host && destination->port == origin.port;
}

} // namespace

bool HasSupportedRestAuthority(const ScanPlan &plan, const HttpExecutionProfile &profile, bool &requires_bearer) {
	return HasExactNetworkCapability(plan.Network(), plan.Operation().Rest().origin, profile) &&
	       HasAuthentication(plan, requires_bearer);
}

} // namespace internal
} // namespace duckdb_api
