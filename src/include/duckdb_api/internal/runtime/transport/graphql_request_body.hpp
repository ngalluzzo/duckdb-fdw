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

// The executor supplies the current attempt's remaining aggregate body
// authority. Exact sizing rejects a body that cannot fit before reserve or any
// serialization allocation occurs.
HttpRequest BuildAdmittedGraphqlRequest(const AdmittedGraphqlRequestProfile &profile, const std::string *cursor,
                                        uint64_t serialized_body_allowance);

// Revalidates exact serialized bytes against one immutable admitted profile.
// This is used before bearer placement; callers cannot substitute a document,
// variable name, page size, cursor grammar, or noncanonical JSON spelling.
bool IsAdmittedGraphqlBody(const AdmittedGraphqlRequestProfile &profile, const std::string &body) noexcept;

// Compatibility classifier for the fixed 0.7 production curl composition.
bool IsCanonicalAdmittedGraphqlBody(const std::string &body) noexcept;

} // namespace internal
} // namespace duckdb_api
