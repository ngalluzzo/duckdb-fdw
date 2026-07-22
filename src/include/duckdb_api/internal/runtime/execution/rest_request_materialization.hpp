#pragma once

#include "duckdb_api/internal/runtime/execution/http_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/rest_relational_admission.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

// Validated, owned request and response facts copied from one REST plan.
// Admission moves this value into an immutable profile, so execution never
// retains the plan or reinterprets Connector/Semantics internals.
struct MaterializedRestRequest {
	std::vector<AdmittedQueryParameter> query;
	std::vector<HttpHeader> headers;
	std::vector<AdmittedRestColumn> columns;
	std::vector<std::string> records_path;
};

bool TryMaterializeRestRequest(const ScanPlan &plan, const RestConditionalBindingAuthority &conditional,
                               MaterializedRestRequest &request);
bool FitsRestRequestTarget(const std::string &path, const std::vector<AdmittedQueryParameter> &query,
                           uint64_t additional_bytes = 0) noexcept;
// form_urlencoded encoding shared by REST query-field materialization and the
// api_key query-placement authenticator.
std::string FormUrlEncode(const std::string &value);
const char *RestSchemeName(PlannedUrlScheme scheme);
std::string BuildRestTarget(const std::string &path, const std::vector<AdmittedQueryParameter> &query,
                            const std::string *page_name, uint64_t page,
                            AdmittedPaginatedRestConditionalInput conditional);

} // namespace internal
} // namespace duckdb_api
