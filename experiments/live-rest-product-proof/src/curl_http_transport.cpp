#include "live_rest_product_proof_extension.hpp"
#include "live_rest/internal/network_policy.hpp"

#include <curl/curl.h>

#include <chrono>
#include <limits>
#include <memory>
#include <utility>

namespace duckdb {
namespace {

const char *const ACCEPT_HEADER = "Accept: application/vnd.github+json";
const char *const USER_AGENT_HEADER = "User-Agent: duckdb-fdw-live-rest-product-proof";
const char *const API_VERSION_HEADER = "X-GitHub-Api-Version: 2022-11-28";
const char *const PUBLIC_URL = "https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3";
const char *const LOOPBACK_PREFIX = "http://127.0.0.1:";
const char *const FIXED_PATH = "/search/users?q=duckdb+in%3Alogin&per_page=3";

class CurlEasyHandle {
public:
	explicit CurlEasyHandle(CURL *handle_p) : handle(handle_p) {
	}

	~CurlEasyHandle() {
		if (handle) {
			curl_easy_cleanup(handle);
		}
	}

	CURL *Get() const {
		return handle;
	}

private:
	CURL *handle;
};

class CurlHeaderList {
public:
	CurlHeaderList() : headers(nullptr) {
	}

	~CurlHeaderList() {
		if (headers) {
			curl_slist_free_all(headers);
		}
	}

	bool Append(const std::string &header) {
		auto appended = curl_slist_append(headers, header.c_str());
		if (!appended) {
			return false;
		}
		headers = appended;
		return true;
	}

	curl_slist *Get() const {
		return headers;
	}

private:
	curl_slist *headers;
};

struct TransferState {
	TransferState(const live_rest::CancellationView &cancellation_p, uint64_t max_response_bytes_p,
	              uint64_t wall_milliseconds_p)
	    : cancellation(cancellation_p), max_response_bytes(max_response_bytes_p),
	      deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(wall_milliseconds_p)),
	      cancelled(false), timed_out(false), oversized(false), allocation_failed(false) {
		body.reserve(static_cast<std::size_t>(max_response_bytes));
	}

	bool ShouldContinue() noexcept {
		if (cancellation.IsCancellationRequested()) {
			cancelled = true;
			return false;
		}
		if (std::chrono::steady_clock::now() >= deadline) {
			timed_out = true;
			return false;
		}
		return true;
	}

	const live_rest::CancellationView &cancellation;
	uint64_t max_response_bytes;
	std::chrono::steady_clock::time_point deadline;
	std::string body;
	bool cancelled;
	bool timed_out;
	bool oversized;
	bool allocation_failed;
};

size_t WriteBody(char *data, size_t size, size_t count, void *opaque) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	if (!state.ShouldContinue()) {
		return 0;
	}
	if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
		state.oversized = true;
		return 0;
	}
	const auto length = size * count;
	const auto current_size = static_cast<uint64_t>(state.body.size());
	if (current_size > state.max_response_bytes ||
	    static_cast<uint64_t>(length) > state.max_response_bytes - current_size) {
		state.oversized = true;
		return 0;
	}
	try {
		state.body.append(data, length);
	} catch (...) {
		state.allocation_failed = true;
		return 0;
	}
	return length;
}

int TransferProgress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	return state.ShouldContinue() ? 0 : 1;
}

live_rest::internal::DestinationProfile DestinationProfileForUrl(const std::string &url) {
	if (url == PUBLIC_URL) {
		return live_rest::internal::DestinationProfile::PUBLIC_API;
	}
	const std::string prefix(LOOPBACK_PREFIX);
	const std::string path(FIXED_PATH);
	if (url.compare(0, prefix.size(), prefix) != 0) {
		throw live_rest::RuntimeError(live_rest::RuntimeStage::PLAN, "",
		                              "live REST destination is outside the fixed profile");
	}
	const auto path_offset = url.find('/', prefix.size());
	if (path_offset == std::string::npos || url.substr(path_offset) != path || path_offset == prefix.size()) {
		throw live_rest::RuntimeError(live_rest::RuntimeStage::PLAN, "",
		                              "live REST destination is outside the fixed profile");
	}
	for (auto index = prefix.size(); index < path_offset; index++) {
		if (url[index] < '0' || url[index] > '9') {
			throw live_rest::RuntimeError(live_rest::RuntimeStage::PLAN, "",
			                              "live REST destination is outside the fixed profile");
		}
	}
	uint32_t port = 0;
	for (auto index = prefix.size(); index < path_offset; index++) {
		port = port * 10U + static_cast<uint32_t>(url[index] - '0');
		if (port > 65535U) {
			throw live_rest::RuntimeError(live_rest::RuntimeStage::PLAN, "",
			                              "live REST destination is outside the fixed profile");
		}
	}
	if (port == 0) {
		throw live_rest::RuntimeError(live_rest::RuntimeStage::PLAN, "",
		                              "live REST destination is outside the fixed profile");
	}
	return live_rest::internal::DestinationProfile::LOOPBACK_ORACLE;
}

struct OpenSocketState {
	explicit OpenSocketState(live_rest::internal::DestinationProfile profile_p) : profile(profile_p) {
	}

	live_rest::internal::DestinationProfile profile;
};

curl_socket_t OpenAllowedSocket(void *opaque, curlsocktype purpose, struct curl_sockaddr *address) noexcept {
	if (purpose != CURLSOCKTYPE_IPCXN || !opaque || !address) {
		return CURL_SOCKET_BAD;
	}
	const auto &state = *static_cast<OpenSocketState *>(opaque);
	if (!live_rest::internal::IsAllowedSocketAddress(&address->addr, address->addrlen, state.profile)) {
		return CURL_SOCKET_BAD;
	}
	return socket(address->family, address->socktype, address->protocol);
}

void RequireCurlOption(CURLcode result) {
	if (result != CURLE_OK) {
		throw live_rest::RuntimeError(live_rest::RuntimeStage::TRANSPORT, "",
		                              "live REST transport configuration failed");
	}
}

void ValidateFixedHeaders(const std::vector<live_rest::HttpHeader> &headers) {
	const auto valid = headers.size() == 3 && headers[0].name == "Accept" &&
	                   headers[0].value == "application/vnd.github+json" && headers[1].name == "User-Agent" &&
	                   headers[1].value == "duckdb-fdw-live-rest-product-proof" &&
	                   headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
	if (!valid) {
		throw live_rest::RuntimeError(live_rest::RuntimeStage::PLAN, "",
		                              "live REST request headers are outside the fixed profile");
	}
}

CURLcode EnsureCurlInitialized() {
	// libcurl global state is process-scoped because other extensions or the
	// host may share it. Query state still owns and closes every easy handle.
	static const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
	return result;
}

class CurlHttpTransport final : public live_rest::HttpTransport {
public:
	live_rest::HttpResponse Get(const std::string &url, const std::vector<live_rest::HttpHeader> &fixed_headers,
	                            const live_rest::HttpLimits &limits,
	                            const live_rest::CancellationView &cancellation) const override {
		if (limits.max_response_bytes == 0 || limits.wall_milliseconds == 0 ||
		    limits.max_response_bytes > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
			throw live_rest::RuntimeError(live_rest::RuntimeStage::RESOURCE, "",
			                              "live REST transport received an invalid resource budget");
		}
		if (limits.wall_milliseconds > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
			throw live_rest::RuntimeError(live_rest::RuntimeStage::RESOURCE, "wall_milliseconds",
			                              "live REST transport received an invalid wall-time budget");
		}
		ValidateFixedHeaders(fixed_headers);
		const auto destination_profile = DestinationProfileForUrl(url);
		if (cancellation.IsCancellationRequested()) {
			throw live_rest::ExecutionCancelled();
		}

		try {
			if (EnsureCurlInitialized() != CURLE_OK) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::TRANSPORT, "",
				                              "live REST transport is unavailable");
			}
			TransferState state(cancellation, limits.max_response_bytes, limits.wall_milliseconds);
			OpenSocketState open_socket_state(destination_profile);
			CurlHeaderList headers;
			if (!headers.Append(ACCEPT_HEADER) || !headers.Append(USER_AGENT_HEADER) ||
			    !headers.Append(API_VERSION_HEADER)) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::RESOURCE, "",
				                              "live REST request headers could not be allocated");
			}
			// Construct the easy handle after every callback target so cleanup
			// cannot observe callback state whose lifetime has already ended.
			CurlEasyHandle easy(curl_easy_init());
			if (!easy.Get()) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::TRANSPORT, "",
				                              "live REST transport is unavailable");
			}
			auto *handle = easy.Get();
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_URL, url.c_str()));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L));
			// HTTP/1.1 avoids libcurl's HTTP/2 stream-refusal replay path. The
			// trial permits connection fallback but never a second wire request.
			RequireCurlOption(
			    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1)));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers.Get()));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https"));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https"));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PROXY, ""));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PRE_PROXY, ""));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_NETRC, CURL_NETRC_IGNORED));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_NONE));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PROXYAUTH, CURLAUTH_NONE));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_UNRESTRICTED_AUTH, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(limits.wall_milliseconds)));
			RequireCurlOption(
			    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(limits.wall_milliseconds)));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, TransferProgress));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteBody));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PATH_AS_IS, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, OpenAllowedSocket));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, &open_socket_state));

			const auto transfer_result = curl_easy_perform(handle);
			if (state.cancelled || cancellation.IsCancellationRequested()) {
				throw live_rest::ExecutionCancelled();
			}
			if (state.timed_out || transfer_result == CURLE_OPERATION_TIMEDOUT) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::RESOURCE, "wall_milliseconds",
				                              "live REST response exceeded the wall-time budget");
			}
			if (state.oversized) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::RESOURCE, "max_response_bytes",
				                              "live REST response exceeded the byte budget");
			}
			if (state.allocation_failed) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::RESOURCE, "max_response_bytes",
				                              "live REST response could not be buffered within its budget");
			}
			if (transfer_result != CURLE_OK) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::TRANSPORT, "",
				                              "live REST transport request failed");
			}

			long response_status = 0;
			RequireCurlOption(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_status));
			if (response_status < 0 || static_cast<unsigned long>(response_status) >
			                               static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
				throw live_rest::RuntimeError(live_rest::RuntimeStage::TRANSPORT, "",
				                              "live REST transport returned an invalid status");
			}
			if (response_status < 200 || response_status >= 300) {
				state.body.clear();
			}
			return {static_cast<uint32_t>(response_status), std::move(state.body)};
		} catch (const live_rest::ExecutionCancelled &) {
			throw;
		} catch (const live_rest::RuntimeError &) {
			throw;
		} catch (...) {
			// Dependency diagnostics can contain URLs or ambient system details.
			throw live_rest::RuntimeError(live_rest::RuntimeStage::TRANSPORT, "",
			                              "live REST transport request failed");
		}
	}
};

} // namespace

std::unique_ptr<live_rest::HttpTransport> BuildCurlHttpTransport() {
	return std::unique_ptr<live_rest::HttpTransport>(new CurlHttpTransport());
}

} // namespace duckdb
