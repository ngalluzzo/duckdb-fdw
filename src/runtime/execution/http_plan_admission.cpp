#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"

#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "duckdb_api/internal/runtime/execution/rest_authority_admission.hpp"
#include "duckdb_api/internal/runtime/execution/rest_pagination_admission.hpp"
#include "duckdb_api/internal/runtime/execution/rest_relational_admission.hpp"
#include "duckdb_api/internal/runtime/execution/rest_request_materialization.hpp"
#include "duckdb_api/internal/runtime/policy/request_validation.hpp"

#include <new>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

BaseDomain ExpectedRestDomain(const PlannedRestOperation &operation, PlannedPaginationStrategy pagination) {
	if (pagination == PlannedPaginationStrategy::LINK_HEADER) {
		return operation.response_source == PlannedResponseSource::ROOT_ARRAY ? BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS
		                                                                      : BaseDomain::PAGINATED_JSON_PATH_RECORDS;
	}
	if (operation.response_source == PlannedResponseSource::ROOT_OBJECT) {
		return BaseDomain::SUCCESSFUL_ROOT_OBJECT;
	}
	return operation.response_source == PlannedResponseSource::ROOT_ARRAY ? BaseDomain::ROOT_ARRAY_RECORDS
	                                                                      : BaseDomain::JSON_PATH_RECORDS;
}

bool HasCommonRestPlan(const ScanPlan &plan, const HttpExecutionProfile &profile, MaterializedRestRequest &request,
                       bool &requires_bearer) {
	if (plan.Operation().Protocol() != PlannedProtocol::REST) {
		return false;
	}
	const auto &operation = plan.Operation().Rest();
	RestConditionalBindingAuthority conditional;
	return operation.method == PlannedHttpMethod::GET && operation.replay_safety == PlannedReplaySafety::SAFE &&
	       operation.origin.scheme == PlannedUrlScheme::HTTPS && operation.origin.scheme == profile.scheme &&
	       operation.origin.host == profile.host && operation.origin.port == profile.port &&
	       plan.Network().port == operation.origin.port && plan.Network().port == profile.port &&
	       IsSafeDnsHost(operation.origin.host) && operation.origin.port != 0 &&
	       plan.Domain() == ExpectedRestDomain(operation, plan.Pagination().Strategy()) &&
	       IsSafeRequestPath(operation.path) && TryAdmitRestRelationalEnvelope(plan, conditional) &&
	       TryMaterializeRestRequest(plan, conditional, request) &&
	       HasSupportedRestAuthority(plan, profile, requires_bearer) && plan.Providers() == FeatureState::DISABLED &&
	       plan.Retry() == FeatureState::DISABLED && plan.Cache() == FeatureState::DISABLED;
}

bool HasSingleResponseShape(const ScanPlan &plan, const HttpExecutionProfile &profile, MaterializedRestRequest &request,
                            bool &requires_bearer) {
	if (!HasCommonRestPlan(plan, profile, request, requires_bearer) ||
	    plan.Pagination().Strategy() != PlannedPaginationStrategy::DISABLED ||
	    !plan.Budgets().IsWithinLiveRestBounds() || plan.Budgets().decoded_records > profile.max_decoded_records ||
	    plan.ConditionalInput() == PlannedConditionalInput::VISIBILITY_PRIVATE) {
		return false;
	}
	const auto &operation = plan.Operation().Rest();
	return (operation.cardinality == PlannedCardinality::EXACTLY_ONE_ON_SUCCESS &&
	        operation.response_source == PlannedResponseSource::ROOT_OBJECT && plan.Budgets().decoded_records == 1) ||
	       (operation.cardinality == PlannedCardinality::ZERO_TO_MANY &&
	        (operation.response_source == PlannedResponseSource::ROOT_ARRAY ||
	         operation.response_source == PlannedResponseSource::JSON_PATH_MANY));
}

bool HasPaginatedShape(const ScanPlan &plan, const HttpExecutionProfile &profile, MaterializedRestRequest &request,
                       bool &requires_bearer) {
	return HasCommonRestPlan(plan, profile, request, requires_bearer) &&
	       HasSupportedRestPagination(plan, profile, request.query);
}

} // namespace

std::unique_ptr<const AdmittedRestRequestProfile> TryAdmitSingleResponseHttpPlan(const ScanPlan &plan,
                                                                                 const HttpExecutionProfile &profile) {
	try {
		MaterializedRestRequest request;
		bool requires_bearer = false;
		if (!HasSingleResponseShape(plan, profile, request, requires_bearer)) {
			return {};
		}
		return std::unique_ptr<const AdmittedRestRequestProfile>(
		    new AdmittedRestRequestProfile(plan, std::move(request), requires_bearer));
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "request_profile",
		                     "REST request profile could not be allocated within its memory budget");
	} catch (const ExecutionError &) {
		return {};
	} catch (...) {
		return {};
	}
}

std::unique_ptr<const AdmittedPaginatedRestRequestProfile>
TryAdmitPaginatedRestPlan(const ScanPlan &plan, const HttpExecutionProfile &profile) {
	try {
		MaterializedRestRequest request;
		bool requires_bearer = false;
		if (!HasPaginatedShape(plan, profile, request, requires_bearer)) {
			return {};
		}
		return std::unique_ptr<const AdmittedPaginatedRestRequestProfile>(
		    new AdmittedPaginatedRestRequestProfile(plan, std::move(request), requires_bearer));
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "request_profile",
		                     "paginated REST request profile could not be allocated within its memory budget");
	} catch (const ExecutionError &) {
		return {};
	} catch (...) {
		return {};
	}
}

} // namespace internal
} // namespace duckdb_api
