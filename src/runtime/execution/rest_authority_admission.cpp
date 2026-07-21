#include "duckdb_api/internal/runtime/execution/rest_authority_admission.hpp"

#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

namespace duckdb_api {
namespace internal {
namespace {

bool HasExpectedNetwork(const NetworkCapability &network, const HttpExecutionProfile &profile) {
	return profile.scheme == PlannedUrlScheme::HTTPS && network.allowed_schemes.size() == 1 &&
	       network.allowed_schemes[0] == "https" && network.allowed_hosts.size() == 1 &&
	       network.allowed_hosts[0] == profile.host && !network.redirects_enabled &&
	       network.private_addresses_enabled == profile.private_addresses_enabled &&
	       network.link_local_addresses_enabled == profile.link_local_addresses_enabled &&
	       network.loopback_addresses_enabled == profile.loopback_addresses_enabled;
}

bool HasAuthentication(const ScanPlan &plan, const HttpExecutionProfile &profile, bool &requires_bearer) {
	const auto &obligation = plan.AuthenticationObligation();
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
	       destination->scheme == profile.scheme && destination->host == profile.host &&
	       destination->port == profile.port;
}

} // namespace

bool HasSupportedRestAuthority(const ScanPlan &plan, const HttpExecutionProfile &profile, bool &requires_bearer) {
	return HasExpectedNetwork(plan.Network(), profile) && HasAuthentication(plan, profile, requires_bearer);
}

} // namespace internal
} // namespace duckdb_api
