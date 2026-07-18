#include "duckdb_api/internal/curl_http_transport.hpp"

#include "duckdb_api/internal/curl_transfer.hpp"
#include "duckdb_api/internal/network_policy.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <memory>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

const char *const PUBLIC_SEARCH_URL = "https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3";
const char *const PUBLIC_AUTHENTICATED_USER_URL = "https://api.github.com/user";

bool HasFixedHeaders(const std::vector<HttpHeader> &headers) {
	return headers.size() >= 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.4.0" &&
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
	return request.method == "GET" && request.scheme == "https" && request.host == "api.github.com" &&
	       request.port == 443 && request.target == "/search/users?q=duckdb+in%3Alogin&per_page=3" &&
	       request.headers.size() == 3 && HasFixedHeaders(request.headers);
}

bool HasExpectedAuthenticatedRequest(const HttpRequest &request) {
	return request.method == "GET" && request.scheme == "https" && request.host == "api.github.com" &&
	       request.port == 443 && request.target == "/user" && request.headers.size() == 4 &&
	       HasFixedHeaders(request.headers) && request.headers[3].name == "Authorization" &&
	       IsVisibleAsciiBearer(request.headers[3].value);
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

class CurlHttpTransport final : public HttpTransport {
public:
	HttpResponse Get(const HttpRequest &request, const HttpLimits &limits, ExecutionControl &control) const override {
		const char *url = nullptr;
		if (HasExpectedAnonymousRequest(request)) {
			url = PUBLIC_SEARCH_URL;
		} else if (HasExpectedAuthenticatedRequest(request)) {
			url = PUBLIC_AUTHENTICATED_USER_URL;
		} else {
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
