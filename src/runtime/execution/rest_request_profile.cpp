#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"

#include "duckdb_api/internal/runtime/execution/rest_request_materialization.hpp"

#include <limits>
#include <utility>

namespace duckdb_api {
namespace internal {

AdmittedRestRequestProfile::AdmittedRestRequestProfile(const ScanPlan &plan, MaterializedRestRequest &&request,
                                                       bool requires_bearer_p)
    : method("GET"), scheme(RestSchemeName(plan.Operation().Rest().origin.scheme)),
      host(plan.Operation().Rest().origin.host), port(plan.Operation().Rest().origin.port),
      path(plan.Operation().Rest().path), query_parameters(std::move(request.query)),
      headers(std::move(request.headers)), columns(std::move(request.columns)),
      response_source(plan.Operation().Rest().response_source), records_path(std::move(request.records_path)),
      requires_bearer(requires_bearer_p), budgets(plan.Budgets()) {
}

const std::string &AdmittedRestRequestProfile::Method() const {
	return method;
}
const std::string &AdmittedRestRequestProfile::Scheme() const {
	return scheme;
}
const std::string &AdmittedRestRequestProfile::Host() const {
	return host;
}
uint16_t AdmittedRestRequestProfile::Port() const {
	return port;
}
const std::string &AdmittedRestRequestProfile::Path() const {
	return path;
}
const std::vector<AdmittedQueryParameter> &AdmittedRestRequestProfile::QueryParameters() const {
	return query_parameters;
}
const std::vector<HttpHeader> &AdmittedRestRequestProfile::Headers() const {
	return headers;
}
const std::vector<AdmittedRestColumn> &AdmittedRestRequestProfile::Columns() const {
	return columns;
}
PlannedResponseSource AdmittedRestRequestProfile::ResponseSource() const {
	return response_source;
}
const std::vector<std::string> &AdmittedRestRequestProfile::RecordsPath() const {
	return records_path;
}
bool AdmittedRestRequestProfile::RequiresBearer() const {
	return requires_bearer;
}
const ResourceBudgets &AdmittedRestRequestProfile::Budgets() const {
	return budgets;
}

AdmittedPaginatedRestRequestProfile::AdmittedPaginatedRestRequestProfile(const ScanPlan &plan,
                                                                         MaterializedRestRequest &&request,
                                                                         bool requires_bearer_p)
    : method("GET"), scheme(RestSchemeName(plan.Operation().Rest().origin.scheme)),
      host(plan.Operation().Rest().origin.host), port(plan.Operation().Rest().origin.port),
      path(plan.Operation().Rest().path), query_parameters(std::move(request.query)),
      headers(std::move(request.headers)), columns(std::move(request.columns)),
      response_source(plan.Operation().Rest().response_source), records_path(std::move(request.records_path)),
      page_size_parameter(plan.Pagination().Target().page_size_parameter),
      page_size(plan.Pagination().Target().page_size),
      page_number_parameter(plan.Pagination().Target().page_number_parameter),
      first_page(plan.Pagination().Target().first_page), page_increment(plan.Pagination().Target().page_increment),
      max_pages(plan.Pagination().ScanBudgets().pages), requires_bearer(requires_bearer_p),
      conditional_input(plan.ConditionalInput() == PlannedConditionalInput::VISIBILITY_PRIVATE
                            ? AdmittedPaginatedRestConditionalInput::LEGACY_VISIBILITY_PRIVATE
                            : AdmittedPaginatedRestConditionalInput::NONE),
      page_budgets(plan.Pagination().PageBudgets()), scan_budgets(plan.Pagination().ScanBudgets()) {
}

const std::string &AdmittedPaginatedRestRequestProfile::Method() const {
	return method;
}
const std::string &AdmittedPaginatedRestRequestProfile::Scheme() const {
	return scheme;
}
const std::string &AdmittedPaginatedRestRequestProfile::Host() const {
	return host;
}
uint16_t AdmittedPaginatedRestRequestProfile::Port() const {
	return port;
}
const std::string &AdmittedPaginatedRestRequestProfile::Path() const {
	return path;
}
const std::vector<AdmittedQueryParameter> &AdmittedPaginatedRestRequestProfile::QueryParameters() const {
	return query_parameters;
}
const std::vector<HttpHeader> &AdmittedPaginatedRestRequestProfile::Headers() const {
	return headers;
}
const std::vector<AdmittedRestColumn> &AdmittedPaginatedRestRequestProfile::Columns() const {
	return columns;
}
PlannedResponseSource AdmittedPaginatedRestRequestProfile::ResponseSource() const {
	return response_source;
}
const std::vector<std::string> &AdmittedPaginatedRestRequestProfile::RecordsPath() const {
	return records_path;
}
const std::string &AdmittedPaginatedRestRequestProfile::PageSizeParameter() const {
	return page_size_parameter;
}
uint64_t AdmittedPaginatedRestRequestProfile::PageSize() const {
	return page_size;
}
const std::string &AdmittedPaginatedRestRequestProfile::PageNumberParameter() const {
	return page_number_parameter;
}
uint64_t AdmittedPaginatedRestRequestProfile::FirstPage() const {
	return first_page;
}
uint64_t AdmittedPaginatedRestRequestProfile::PageIncrement() const {
	return page_increment;
}
uint64_t AdmittedPaginatedRestRequestProfile::MaxPages() const {
	return max_pages;
}
bool AdmittedPaginatedRestRequestProfile::RequiresBearer() const {
	return requires_bearer;
}
AdmittedPaginatedRestConditionalInput AdmittedPaginatedRestRequestProfile::ConditionalInput() const {
	return conditional_input;
}
const ResourceBudgets &AdmittedPaginatedRestRequestProfile::PageBudgets() const {
	return page_budgets;
}
const ScanResourceBudgets &AdmittedPaginatedRestRequestProfile::ScanBudgets() const {
	return scan_budgets;
}

HttpRequest BuildAdmittedRestRequest(const AdmittedRestRequestProfile &profile) {
	return {profile.Method(),
	        profile.Scheme(),
	        profile.Host(),
	        profile.Port(),
	        BuildRestTarget(profile.Path(), profile.QueryParameters(), nullptr, 0,
	                        AdmittedPaginatedRestConditionalInput::NONE),
	        profile.Headers(),
	        {},
	        {}};
}

HttpRequest BuildAdmittedPaginatedRestPageRequest(const AdmittedPaginatedRestRequestProfile &profile, uint64_t page) {
	if (profile.MaxPages() == 0 || profile.PageIncrement() == 0 ||
	    profile.MaxPages() - 1 >
	        (std::numeric_limits<uint64_t>::max() - profile.FirstPage()) / profile.PageIncrement()) {
		throw ExecutionError(ErrorStage::POLICY, "pagination.page", "paginated REST profile cannot advance safely");
	}
	const auto last_page = profile.FirstPage() + (profile.MaxPages() - 1) * profile.PageIncrement();
	if (page < profile.FirstPage() || page > last_page || (page - profile.FirstPage()) % profile.PageIncrement() != 0) {
		throw ExecutionError(ErrorStage::POLICY, "pagination.page",
		                     "REST page is outside the admitted request profile");
	}
	return {profile.Method(),
	        profile.Scheme(),
	        profile.Host(),
	        profile.Port(),
	        BuildRestTarget(profile.Path(), profile.QueryParameters(), &profile.PageNumberParameter(), page,
	                        profile.ConditionalInput()),
	        profile.Headers(),
	        {},
	        {}};
}

} // namespace internal
} // namespace duckdb_api
