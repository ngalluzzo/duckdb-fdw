#pragma once

#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <string>

namespace duckdb_api {
namespace internal {

// Constructs the sole canonical GraphQL POST shape. The compact field order is
// fixed; only cursor nullability/value may vary. Returned bytes contain no
// credential and must be debited by scan accounting before authorization.
HttpRequest BuildAdmittedGraphqlRequest(const AdmittedGraphqlRequestProfile &profile, const std::string *cursor);

// Defense-in-depth validation for the fixed installed curl wrapper. Admission
// and the serializer remain the primary authority; this rejects document or
// fixed-variable drift before a concrete transport sees the request.
bool IsCanonicalAdmittedGraphqlBody(const std::string &body) noexcept;

} // namespace internal
} // namespace duckdb_api
