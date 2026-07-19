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

// Closed result for the two single-response profiles. Repository admission
// instead returns the complete request profile below. Admission performs no
// network I/O and authorization is still matched and moved by the executor.
enum class AdmittedHttpOperation : uint8_t { ANONYMOUS_SEARCH, AUTHENTICATED_USER };

enum class AdmittedRepositoryConditionalInput : uint8_t { NONE, VISIBILITY_PRIVATE };

struct AdmittedRepositoryColumn {
	std::string name;
	std::string source_field;
	ValueKind kind;
};

// Complete immutable authority produced only after Runtime validates the whole
// repository ScanPlan. Request construction, authentication, decoding, and
// Link validation consume this value rather than reinterpreting predicate,
// Connector, or plan fields during execution. Copies own their strings and are
// safe for concurrent read-only use; stream close destroys its retained copy.
class AdmittedRepositoryRequestProfile {
public:
	AdmittedRepositoryRequestProfile(const AdmittedRepositoryRequestProfile &) = default;
	AdmittedRepositoryRequestProfile(AdmittedRepositoryRequestProfile &&) = default;
	AdmittedRepositoryRequestProfile &operator=(const AdmittedRepositoryRequestProfile &) = delete;
	AdmittedRepositoryRequestProfile &operator=(AdmittedRepositoryRequestProfile &&) = delete;

	const std::string &Method() const;
	const std::string &Scheme() const;
	const std::string &Host() const;
	uint16_t Port() const;
	const std::string &Path() const;
	const std::vector<HttpHeader> &Headers() const;
	const std::vector<AdmittedRepositoryColumn> &Columns() const;
	const std::string &PageSizeParameter() const;
	uint64_t PageSize() const;
	const std::string &PageNumberParameter() const;
	uint64_t FirstPage() const;
	uint64_t PageIncrement() const;
	uint64_t MaxPages() const;
	AdmittedRepositoryConditionalInput ConditionalInput() const;

private:
	friend std::unique_ptr<const AdmittedRepositoryRequestProfile>
	TryAdmitRepositoryHttpPlan(const ScanPlan &, const HttpExecutionProfile &);

	explicit AdmittedRepositoryRequestProfile(AdmittedRepositoryConditionalInput conditional_input);

	std::string method;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;
	std::vector<HttpHeader> headers;
	std::vector<AdmittedRepositoryColumn> columns;
	std::string page_size_parameter;
	uint64_t page_size;
	std::string page_number_parameter;
	uint64_t first_page;
	uint64_t page_increment;
	uint64_t max_pages;
	AdmittedRepositoryConditionalInput conditional_input;
};

bool TryAdmitSingleResponseHttpPlan(const ScanPlan &plan, const HttpExecutionProfile &profile,
                                    AdmittedHttpOperation &operation);
std::unique_ptr<const AdmittedRepositoryRequestProfile> TryAdmitRepositoryHttpPlan(const ScanPlan &plan,
                                                                                   const HttpExecutionProfile &profile);
HttpRequest BuildAdmittedRepositoryPageRequest(const AdmittedRepositoryRequestProfile &profile, uint64_t page);

} // namespace internal
} // namespace duckdb_api
