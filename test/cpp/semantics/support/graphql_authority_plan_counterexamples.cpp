#include "semantics/support/scan_plan_test_access.hpp"

namespace duckdb_api_test {

bool ScanPlanTestAccess::MutateGraphqlRelationalOrAuthority(duckdb_api::ScanPlan &plan,
                                                            GraphqlRuntimeAdmissionCounterexample counterexample) {
	switch (counterexample) {
	case GraphqlRuntimeAdmissionCounterexample::OTHER_DOMAIN:
		plan.domain = duckdb_api::BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_REMOTE_PREDICATE:
		plan.remote_predicate = duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_REMOTE_ACCURACY:
		plan.remote_accuracy = duckdb_api::RemotePredicateAccuracy::SUPERSET;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_RESIDUAL_OWNER:
		plan.residual_owner = static_cast<duckdb_api::RelationalOwner>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_CONDITIONAL_INPUT:
		plan.conditional_input = duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_FILTER_OWNER:
		plan.ownership.filter = static_cast<duckdb_api::RelationalOwner>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_PROJECTION_OWNER:
		plan.ownership.projection = static_cast<duckdb_api::RelationalOwner>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_ORDERING_OWNER:
		plan.ownership.ordering = static_cast<duckdb_api::RelationalOwner>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_LIMIT_OWNER:
		plan.ownership.limit = static_cast<duckdb_api::RelationalOwner>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_OFFSET_OWNER:
		plan.ownership.offset = static_cast<duckdb_api::RelationalOwner>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_REMOTE_ORDERING:
		plan.remote_ordering = static_cast<duckdb_api::RelationalDelegation>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_RUNTIME_ORDERING:
		plan.runtime_ordering = static_cast<duckdb_api::RelationalDelegation>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_REMOTE_LIMIT:
		plan.remote_limit = static_cast<duckdb_api::RelationalDelegation>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_REMOTE_OFFSET:
		plan.remote_offset = static_cast<duckdb_api::RelationalDelegation>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_RUNTIME_LIMIT:
		plan.runtime_limit = static_cast<duckdb_api::RelationalDelegation>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_RUNTIME_OFFSET:
		plan.runtime_offset = static_cast<duckdb_api::RelationalDelegation>(127);
		return true;
	case GraphqlRuntimeAdmissionCounterexample::PROVIDERS_ENABLED:
		plan.providers = duckdb_api::FeatureState::ENABLED;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::RETRY_ENABLED:
		plan.retry = duckdb_api::FeatureState::ENABLED;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::CACHE_ENABLED:
		plan.cache = duckdb_api::FeatureState::ENABLED;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::AUTHENTICATION_DISABLED:
		plan.authentication = duckdb_api::FeatureState::DISABLED;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::SECRET_REFERENCE_ABSENT:
		plan.secret_reference = duckdb_api::PlannedSecretReference();
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_AUTH_REQUIREMENT:
		plan.authentication_obligation.requirement = duckdb_api::PlannedCredentialRequirement::NONE;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_LOGICAL_CREDENTIAL:
		plan.authentication_obligation.logical_credential = "other";
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_AUTHENTICATOR:
		plan.authentication_obligation.authenticator = duckdb_api::PlannedAuthenticator::NONE;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_CREDENTIAL_PLACEMENT:
		plan.authentication_obligation.placement = duckdb_api::PlannedCredentialPlacement::NONE;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::AUTH_DESTINATION_ABSENT:
		plan.authentication_obligation.has_destination = false;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::HTTP_AUTH_DESTINATION:
		plan.authentication_obligation.destination.scheme = duckdb_api::PlannedUrlScheme::HTTP;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_AUTH_DESTINATION_HOST:
		plan.authentication_obligation.destination.host = "other.example";
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_AUTH_DESTINATION_PORT:
		plan.authentication_obligation.destination.port++;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_NETWORK_SCHEME:
		plan.network.allowed_schemes.at(0) = "http";
		return true;
	case GraphqlRuntimeAdmissionCounterexample::OTHER_NETWORK_HOST:
		plan.network.allowed_hosts.at(0) = "other.example";
		return true;
	case GraphqlRuntimeAdmissionCounterexample::EXTRA_NETWORK_SCHEME:
		plan.network.allowed_schemes.push_back("http");
		return true;
	case GraphqlRuntimeAdmissionCounterexample::EXTRA_NETWORK_HOST:
		plan.network.allowed_hosts.push_back("other.example");
		return true;
	case GraphqlRuntimeAdmissionCounterexample::REDIRECTS_ENABLED:
		plan.network.redirects_enabled = true;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::PRIVATE_ADDRESSES_ENABLED:
		plan.network.private_addresses_enabled = true;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::LINK_LOCAL_ADDRESSES_ENABLED:
		plan.network.link_local_addresses_enabled = true;
		return true;
	case GraphqlRuntimeAdmissionCounterexample::LOOPBACK_ADDRESSES_ENABLED:
		plan.network.loopback_addresses_enabled = true;
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_api_test
