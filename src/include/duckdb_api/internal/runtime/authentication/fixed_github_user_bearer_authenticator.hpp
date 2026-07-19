#pragma once

#include "duckdb_api/authorization.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api {
namespace internal {

// The sole consumer of ScanAuthorization credential bytes. The fixed
// authenticator revalidates the already approved plan and structural request
// before copying the scan-owned token into one transient canonical bearer
// header. The capability remains opaque and valid only so its owning stream can
// decorate a later validated page; terminal stream cleanup releases it. This
// service returns no generic placement or destination facility.
class FixedGithubUserBearerAuthenticator {
public:
	static HttpRequest Authorize(const ScanPlan &plan, HttpRequest request, const ScanAuthorization &authorization);

private:
	static std::string CopyToken(const ScanAuthorization &authorization);
};

} // namespace internal
} // namespace duckdb_api
