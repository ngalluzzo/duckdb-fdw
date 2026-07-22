#include "duckdb_api/internal/runtime/execution/rest_authority_admission.hpp"

#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"

namespace duckdb_api {
namespace internal {
namespace {

bool HasAuthentication(const ScanPlan &plan, RequiredCredential &credential) {
	const auto &obligation = plan.AuthenticationObligation();
	const auto &origin = plan.Operation().Rest().origin;
	const auto *destination = obligation.Destination();
	if (plan.Authentication() == FeatureState::DISABLED) {
		credential = RequiredCredential();
		return !plan.SecretReference().IsPresent() && obligation.Requirement() == PlannedCredentialRequirement::NONE &&
		       obligation.LogicalCredential().empty() && obligation.Authenticator() == PlannedAuthenticator::NONE &&
		       obligation.Placement() == PlannedCredentialPlacement::NONE && destination == nullptr;
	}
	if (plan.Authentication() != FeatureState::ENABLED) {
		return false;
	}
	const bool destination_matches = destination != nullptr && destination->scheme == origin.scheme &&
	                                 destination->host == origin.host && destination->port == origin.port;
	const bool common = plan.SecretReference().IsPresent() &&
	                    obligation.Requirement() == PlannedCredentialRequirement::REQUIRED &&
	                    obligation.LogicalCredential() == "token" && destination_matches;
	if (obligation.Authenticator() == PlannedAuthenticator::BEARER &&
	    obligation.Placement() == PlannedCredentialPlacement::AUTHORIZATION_HEADER) {
		credential = RequiredCredential();
		credential.bearer = true;
		return common;
	}
	if (obligation.Authenticator() == PlannedAuthenticator::API_KEY &&
	    (obligation.Placement() == PlannedCredentialPlacement::HEADER_NAMED ||
	     obligation.Placement() == PlannedCredentialPlacement::QUERY_NAMED) &&
	    !obligation.PlacementName().empty()) {
		credential = RequiredCredential();
		credential.api_key = true;
		credential.header_placement = obligation.Placement() == PlannedCredentialPlacement::HEADER_NAMED;
		credential.placement_name = obligation.PlacementName();
		return common;
	}
	return false;
}

} // namespace

bool HasSupportedRestAuthority(const ScanPlan &plan, const HttpExecutionProfile &profile,
                               RequiredCredential &credential) {
	return HasExactNetworkCapability(plan.Network(), plan.Operation().Rest().origin, profile) &&
	       HasAuthentication(plan, credential);
}

} // namespace internal
} // namespace duckdb_api
