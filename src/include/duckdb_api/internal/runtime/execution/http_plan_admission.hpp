#pragma once

#include "duckdb_api/execution.hpp"
#include "duckdb_api/internal/runtime/execution/rest_authority_admission.hpp"
#include "duckdb_api/internal/runtime/transport/http_transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {
namespace internal {

struct HttpExecutionProfile;
struct MaterializedRestRequest;

struct AdmittedRestColumn {
	std::string name;
	std::vector<std::string> source_path;
	ValueKind kind;
	bool nullable;
};

struct AdmittedQueryParameter {
	std::string name;
	std::string encoded_value;
};

enum class AdmittedPaginatedRestConditionalInput : uint8_t { NONE, LEGACY_VISIBILITY_PRIVATE };

// Immutable authority for one admitted bodyless REST request. Every value is
// copied from the validated typed plan; provenance names and explanation text
// are intentionally absent. Streams retain this value rather than the plan.
// Copies contain no credential or mutable execution state and are safe for the
// owning stream lifetime; cancellation, transport, and close belong to that
// stream.
class AdmittedRestRequestProfile {
public:
	AdmittedRestRequestProfile(const AdmittedRestRequestProfile &) = default;
	AdmittedRestRequestProfile(AdmittedRestRequestProfile &&) = default;
	AdmittedRestRequestProfile &operator=(const AdmittedRestRequestProfile &) = delete;
	AdmittedRestRequestProfile &operator=(AdmittedRestRequestProfile &&) = delete;

	const std::string &Method() const;
	const std::string &Scheme() const;
	const std::string &Host() const;
	uint16_t Port() const;
	const std::string &Path() const;
	const std::vector<AdmittedQueryParameter> &QueryParameters() const;
	const std::vector<HttpHeader> &Headers() const;
	const std::vector<AdmittedRestColumn> &Columns() const;
	PlannedResponseSource ResponseSource() const;
	const std::vector<std::string> &RecordsPath() const;
	bool RequiresBearer() const;
	bool RequiresApiKey() const;
	// Valid only when RequiresApiKey() is true: true = header, false = query.
	bool ApiKeyHeaderPlacement() const;
	// Valid only when RequiresApiKey() is true: the author-declared header or
	// query-parameter name. Never the credential value.
	const std::string &ApiKeyPlacementName() const;
	const ResourceBudgets &Budgets() const;

private:
	friend std::unique_ptr<const AdmittedRestRequestProfile>
	TryAdmitSingleResponseHttpPlan(const ScanPlan &, const HttpExecutionProfile &);
	AdmittedRestRequestProfile(const ScanPlan &plan, MaterializedRestRequest &&request, RequiredCredential credential);

	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;
	std::vector<AdmittedQueryParameter> query_parameters;
	std::vector<HttpHeader> headers;
	std::vector<AdmittedRestColumn> columns;
	PlannedResponseSource response_source;
	std::vector<std::string> records_path;
	RequiredCredential credential;
	ResourceBudgets budgets;
};

// Immutable authority for a sequential Link traversal. Query order and every
// non-page value are copied exactly; only the typed page-number slot changes.
// It contains no credential or received continuation URL. The scan owns its
// copy and all mutable pagination, cancellation, accounting, and close state.
class AdmittedPaginatedRestRequestProfile {
public:
	AdmittedPaginatedRestRequestProfile(const AdmittedPaginatedRestRequestProfile &) = default;
	AdmittedPaginatedRestRequestProfile(AdmittedPaginatedRestRequestProfile &&) = default;
	AdmittedPaginatedRestRequestProfile &operator=(const AdmittedPaginatedRestRequestProfile &) = delete;
	AdmittedPaginatedRestRequestProfile &operator=(AdmittedPaginatedRestRequestProfile &&) = delete;

	const std::string &Method() const;
	const std::string &Scheme() const;
	const std::string &Host() const;
	uint16_t Port() const;
	const std::string &Path() const;
	const std::vector<AdmittedQueryParameter> &QueryParameters() const;
	const std::vector<HttpHeader> &Headers() const;
	const std::vector<AdmittedRestColumn> &Columns() const;
	PlannedResponseSource ResponseSource() const;
	const std::vector<std::string> &RecordsPath() const;
	const std::string &PageSizeParameter() const;
	uint64_t PageSize() const;
	const std::string &PageNumberParameter() const;
	uint64_t FirstPage() const;
	uint64_t PageIncrement() const;
	uint64_t MaxPages() const;
	PlannedPaginationStrategy PaginationStrategy() const;
	// RESPONSE_NEXT_URL only: empty for other strategies. Carries the
	// declared JSON body path the decoder uses to extract the continuation.
	const std::string &NextUrlPath() const;
	bool RequiresBearer() const;
	bool RequiresApiKey() const;
	// Valid only when RequiresApiKey() is true: true = header, false = query.
	bool ApiKeyHeaderPlacement() const;
	// Valid only when RequiresApiKey() is true: the author-declared header or
	// query-parameter name. Never the credential value.
	const std::string &ApiKeyPlacementName() const;
	AdmittedPaginatedRestConditionalInput ConditionalInput() const;
	const ResourceBudgets &PageBudgets() const;
	const ScanResourceBudgets &ScanBudgets() const;

private:
	friend std::unique_ptr<const AdmittedPaginatedRestRequestProfile>
	TryAdmitPaginatedRestPlan(const ScanPlan &, const HttpExecutionProfile &);
	AdmittedPaginatedRestRequestProfile(const ScanPlan &plan, MaterializedRestRequest &&request,
	                                    RequiredCredential credential);

	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;
	std::vector<AdmittedQueryParameter> query_parameters;
	std::vector<HttpHeader> headers;
	std::vector<AdmittedRestColumn> columns;
	PlannedResponseSource response_source;
	std::vector<std::string> records_path;
	std::string page_size_parameter;
	uint64_t page_size;
	std::string page_number_parameter;
	uint64_t first_page;
	uint64_t page_increment;
	uint64_t max_pages;
	PlannedPaginationStrategy pagination_strategy;
	std::string next_url_path;
	RequiredCredential credential;
	AdmittedPaginatedRestConditionalInput conditional_input;
	ResourceBudgets page_budgets;
	ScanResourceBudgets scan_budgets;
};

std::unique_ptr<const AdmittedRestRequestProfile> TryAdmitSingleResponseHttpPlan(const ScanPlan &plan,
                                                                                 const HttpExecutionProfile &profile);
std::unique_ptr<const AdmittedPaginatedRestRequestProfile>
TryAdmitPaginatedRestPlan(const ScanPlan &plan, const HttpExecutionProfile &profile);
// Admission returns null for any policy or contract mismatch and performs no
// authorization or I/O. Allocation exhaustion is the sole structured error.
// Builders accept only an admitted immutable profile; paginated construction
// additionally rejects page state outside its exact checked progression.
HttpRequest BuildAdmittedRestRequest(const AdmittedRestRequestProfile &profile);
HttpRequest BuildAdmittedPaginatedRestPageRequest(const AdmittedPaginatedRestRequestProfile &profile, uint64_t page);

} // namespace internal
} // namespace duckdb_api
