#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

struct HttpExecutionProfile;

// One Runtime-private source for the exact admitted document bytes. Request
// serialization and the installed transport's defense-in-depth classifier use
// this identity rather than maintaining lookalike document prefixes.
const char *CanonicalGraphqlDocumentBytes() noexcept;

struct AdmittedGraphqlColumn {
	std::string name;
	ValueKind kind;
	bool nullable;
	std::vector<std::string> response_path;
};

// Immutable Runtime authority for the sole installed GraphQL operation. The
// complete ScanPlan is checked once, before authorization materialization or
// I/O. Downstream services consume this closed value and therefore cannot
// reinterpret a relation name, Connector declaration, or planner-private fact.
class AdmittedGraphqlRequestProfile {
public:
	AdmittedGraphqlRequestProfile(const AdmittedGraphqlRequestProfile &) = default;
	AdmittedGraphqlRequestProfile(AdmittedGraphqlRequestProfile &&) = default;
	AdmittedGraphqlRequestProfile &operator=(const AdmittedGraphqlRequestProfile &) = delete;
	AdmittedGraphqlRequestProfile &operator=(AdmittedGraphqlRequestProfile &&) = delete;

	const std::string &Method() const;
	const std::string &Scheme() const;
	const std::string &Host() const;
	uint16_t Port() const;
	const std::string &Path() const;
	const std::vector<HttpHeader> &Headers() const;
	const std::string &Document() const;
	const std::vector<AdmittedGraphqlColumn> &Columns() const;
	uint64_t PageSize() const;
	uint64_t MaxPages() const;
	uint64_t MaxRequestBodyBytes() const;
	uint64_t MaxScanBodyBytes() const;

private:
	friend std::unique_ptr<const AdmittedGraphqlRequestProfile> TryAdmitGraphqlPlan(const ScanPlan &,
	                                                                                const HttpExecutionProfile &);

	AdmittedGraphqlRequestProfile();

	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;
	std::vector<HttpHeader> headers;
	std::string document;
	std::vector<AdmittedGraphqlColumn> columns;
	uint64_t page_size;
	uint64_t max_pages;
	uint64_t max_request_body_bytes;
	uint64_t max_scan_body_bytes;
};

// Returns null for every plan outside the exact accepted profile. Malformed
// protocol sums are also rejected here rather than leaking provider errors.
std::unique_ptr<const AdmittedGraphqlRequestProfile> TryAdmitGraphqlPlan(const ScanPlan &plan,
                                                                         const HttpExecutionProfile &profile);

} // namespace internal
} // namespace duckdb_api
