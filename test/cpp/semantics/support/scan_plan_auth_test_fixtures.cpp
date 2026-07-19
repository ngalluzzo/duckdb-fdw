#include "semantics/support/scan_plan_test_access.hpp"

#include <stdexcept>

namespace duckdb_api_test {

duckdb_api::ScanPlan ScanPlanTestAccess::Authenticated(duckdb_api::ScanPlan plan,
                                                       AuthenticatedPlanCounterexample counterexample) {
	switch (counterexample) {
	case AuthenticatedPlanCounterexample::FEATURE_DISABLED:
		plan.authentication = duckdb_api::FeatureState::DISABLED;
		break;
	case AuthenticatedPlanCounterexample::REQUIREMENT_NONE:
		plan.authentication_obligation.requirement = duckdb_api::PlannedCredentialRequirement::NONE;
		break;
	case AuthenticatedPlanCounterexample::EMPTY_LOGICAL_BINDING:
		plan.authentication_obligation.logical_credential.clear();
		break;
	case AuthenticatedPlanCounterexample::AUTHENTICATOR_NONE:
		plan.authentication_obligation.authenticator = duckdb_api::PlannedAuthenticator::NONE;
		break;
	case AuthenticatedPlanCounterexample::PLACEMENT_NONE:
		plan.authentication_obligation.placement = duckdb_api::PlannedCredentialPlacement::NONE;
		break;
	case AuthenticatedPlanCounterexample::DESTINATION_ABSENT:
		plan.authentication_obligation.has_destination = false;
		break;
	case AuthenticatedPlanCounterexample::HTTP_DESTINATION_SCHEME:
		plan.authentication_obligation.destination.scheme = duckdb_api::PlannedUrlScheme::HTTP;
		break;
	case AuthenticatedPlanCounterexample::OTHER_DESTINATION_HOST:
		plan.authentication_obligation.destination.host = "other.example";
		break;
	case AuthenticatedPlanCounterexample::OTHER_DESTINATION_PORT:
		plan.authentication_obligation.destination.port = 444;
		break;
	case AuthenticatedPlanCounterexample::MISSING_SECRET_REFERENCE:
		plan.secret_reference = duckdb_api::PlannedSecretReference();
		break;
	default:
		throw std::invalid_argument("unknown closed authenticated plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan ScanPlanTestAccess::AnonymousAuth(duckdb_api::ScanPlan plan,
                                                       AnonymousAuthPlanCounterexample counterexample) {
	switch (counterexample) {
	case AnonymousAuthPlanCounterexample::FEATURE_ENABLED:
		plan.authentication = duckdb_api::FeatureState::ENABLED;
		break;
	case AnonymousAuthPlanCounterexample::REQUIREMENT_REQUIRED:
		plan.authentication_obligation.requirement = duckdb_api::PlannedCredentialRequirement::REQUIRED;
		break;
	case AnonymousAuthPlanCounterexample::LOGICAL_BINDING_PRESENT:
		plan.authentication_obligation.logical_credential = "token";
		break;
	case AnonymousAuthPlanCounterexample::AUTHENTICATOR_BEARER:
		plan.authentication_obligation.authenticator = duckdb_api::PlannedAuthenticator::BEARER;
		break;
	case AnonymousAuthPlanCounterexample::AUTHORIZATION_PLACEMENT:
		plan.authentication_obligation.placement = duckdb_api::PlannedCredentialPlacement::AUTHORIZATION_HEADER;
		break;
	case AnonymousAuthPlanCounterexample::DESTINATION_PRESENT:
		plan.authentication_obligation.has_destination = true;
		plan.authentication_obligation.destination = plan.operation.origin;
		break;
	default:
		throw std::invalid_argument("unknown closed anonymous authentication plan counterexample");
	}
	return plan;
}

duckdb_api::ScanPlan ScanPlanTestAccess::AnonymousSecretReference(duckdb_api::ScanPlan plan,
                                                                  const std::string &exact_logical_secret_name) {
	plan.secret_reference = duckdb_api::PlannedSecretReference(exact_logical_secret_name);
	return plan;
}

duckdb_api::ScanPlan BuildAuthenticatedPlanCounterexample(const std::string &exact_logical_secret_name,
                                                          AuthenticatedPlanCounterexample counterexample) {
	return ScanPlanTestAccess::Authenticated(BuildValidAuthenticatedPlanFixture(exact_logical_secret_name),
	                                         counterexample);
}

duckdb_api::ScanPlan BuildAnonymousAuthPlanCounterexample(AnonymousAuthPlanCounterexample counterexample) {
	return ScanPlanTestAccess::AnonymousAuth(BuildValidAnonymousPlanFixture(), counterexample);
}

duckdb_api::ScanPlan BuildAnonymousSecretReferenceCounterexample(const std::string &exact_logical_secret_name) {
	return ScanPlanTestAccess::AnonymousSecretReference(BuildValidAnonymousPlanFixture(), exact_logical_secret_name);
}

} // namespace duckdb_api_test
