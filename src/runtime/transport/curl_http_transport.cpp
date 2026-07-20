#include "duckdb_api/internal/runtime/transport/curl_http_transport.hpp"

#include "duckdb_api/internal/runtime/transport/curl_transfer.hpp"
#include "duckdb_api/internal/runtime/policy/network_policy.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <memory>
#include <limits>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

const char *const PUBLIC_SEARCH_URL = "https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3";
const char *const PUBLIC_AUTHENTICATED_USER_URL = "https://api.github.com/user";

bool HasFixedHeaders(const std::vector<HttpHeader> &headers, const char *user_agent) {
	return headers.size() >= 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == user_agent &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool IsVisibleAsciiBearer(const std::string &value) {
	if (value.size() <= 7 || value.compare(0, 7, "Bearer ") != 0) {
		return false;
	}
	for (std::size_t index = 7; index < value.size(); index++) {
		const auto byte = static_cast<unsigned char>(value[index]);
		if (byte < 0x21 || byte > 0x7e) {
			return false;
		}
	}
	return true;
}

bool HasExpectedAnonymousRequest(const HttpRequest &request) {
	return request.method == "GET" && request.body.empty() && request.content_type.empty() &&
	       request.scheme == "https" && request.host == "api.github.com" && request.port == 443 &&
	       request.target == "/search/users?q=duckdb+in%3Alogin&per_page=3" && request.headers.size() == 3 &&
	       HasFixedHeaders(request.headers, "duckdb-api/0.6.0");
}

bool HasExpectedAuthenticatedRequest(const HttpRequest &request) {
	return request.method == "GET" && request.body.empty() && request.content_type.empty() &&
	       request.scheme == "https" && request.host == "api.github.com" && request.port == 443 &&
	       request.target == "/user" && request.headers.size() == 4 &&
	       HasFixedHeaders(request.headers, "duckdb-api/0.6.0") && request.headers[3].name == "Authorization" &&
	       IsVisibleAsciiBearer(request.headers[3].value);
}

bool HasExpectedRepositoriesRequest(const HttpRequest &request) {
	static const char prefix[] = "/user/repos?per_page=100&page=";
	static const char selective_suffix[] = "&visibility=private";
	const std::size_t prefix_size = sizeof(prefix) - 1;
	const std::size_t selective_suffix_size = sizeof(selective_suffix) - 1;
	if (request.method != "GET" || !request.body.empty() || !request.content_type.empty() ||
	    request.scheme != "https" || request.host != "api.github.com" || request.port != 443 ||
	    request.target.compare(0, prefix_size, prefix) != 0 || request.target.size() == prefix_size ||
	    request.target[prefix_size] == '0' || request.headers.size() != 4 ||
	    !HasFixedHeaders(request.headers, "duckdb-api/0.6.0") || request.headers[3].name != "Authorization" ||
	    !IsVisibleAsciiBearer(request.headers[3].value)) {
		return false;
	}
	const auto page_end = request.target.size() >= selective_suffix_size &&
	                              request.target.compare(request.target.size() - selective_suffix_size,
	                                                     selective_suffix_size, selective_suffix) == 0
	                          ? request.target.size() - selective_suffix_size
	                          : request.target.size();
	if (page_end == prefix_size) {
		return false;
	}
	uint64_t page = 0;
	for (std::size_t index = prefix_size; index < page_end; index++) {
		if (request.target[index] < '0' || request.target[index] > '9') {
			return false;
		}
		const auto digit = static_cast<uint64_t>(request.target[index] - '0');
		if (page > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
			return false;
		}
		page = page * 10 + digit;
	}
	return page > 0;
}

bool IsPublicHttpsSocket(const sockaddr *address, socklen_t address_length, const void *) noexcept {
	if (!address || !IsPublicSocketAddress(address, address_length)) {
		return false;
	}
	if (address->sa_family == AF_INET) {
		return address_length >= sizeof(sockaddr_in) &&
		       ntohs(reinterpret_cast<const sockaddr_in *>(address)->sin_port) == 443;
	}
	if (address->sa_family == AF_INET6) {
		return address_length >= sizeof(sockaddr_in6) &&
		       ntohs(reinterpret_cast<const sockaddr_in6 *>(address)->sin6_port) == 443;
	}
	return false;
}

} // namespace

InstalledHttpRequestKind ClassifyInstalledHttpRequest(const HttpRequest &request) noexcept {
	if (HasExpectedAnonymousRequest(request)) {
		return InstalledHttpRequestKind::ANONYMOUS_SEARCH;
	}
	if (HasExpectedAuthenticatedRequest(request)) {
		return InstalledHttpRequestKind::AUTHENTICATED_USER;
	}
	if (HasExpectedRepositoriesRequest(request)) {
		return InstalledHttpRequestKind::AUTHENTICATED_REPOSITORIES;
	}
	return InstalledHttpRequestKind::UNSUPPORTED;
}

namespace {

class CurlHttpTransport final : public HttpTransport {
public:
	HttpResponse Get(const HttpRequest &request, const HttpLimits &limits, ExecutionControl &control) const override {
		const char *url = nullptr;
		std::string repository_url;
		switch (ClassifyInstalledHttpRequest(request)) {
		case InstalledHttpRequestKind::ANONYMOUS_SEARCH:
			url = PUBLIC_SEARCH_URL;
			break;
		case InstalledHttpRequestKind::AUTHENTICATED_USER:
			url = PUBLIC_AUTHENTICATED_USER_URL;
			break;
		case InstalledHttpRequestKind::AUTHENTICATED_REPOSITORIES:
			repository_url = std::string("https://api.github.com") + request.target;
			url = repository_url.c_str();
			break;
		case InstalledHttpRequestKind::UNSUPPORTED:
			throw ExecutionError(ErrorStage::POLICY, "", "HTTP request is outside the installed execution profile");
		}
		const CurlTransferProfile profile {url,
		                                   "https",
		                                   IsPublicHttpsSocket,
		                                   nullptr
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
		                                   ,
		                                   nullptr,
		                                   nullptr,
		                                   nullptr,
		                                   nullptr
#endif
		};
		return PerformCurlTransfer(profile, request, limits, control);
	}
};

} // namespace

std::unique_ptr<HttpTransport> BuildCurlHttpTransport(const CurlProcessLifetime *lifetime) {
	if (!lifetime) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP runtime is not initialized");
	}
	return std::unique_ptr<HttpTransport>(new CurlHttpTransport());
}

} // namespace internal
} // namespace duckdb_api
