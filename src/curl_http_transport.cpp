#include "duckdb_api/internal/curl_http_transport.hpp"

#include "duckdb_api/internal/network_policy.hpp"

#include <curl/curl.h>

#include <sys/socket.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

const char *const PUBLIC_URL = "https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3";
const char *const ACCEPT_HEADER = "Accept: application/vnd.github+json";
const char *const USER_AGENT_HEADER = "User-Agent: duckdb-api/0.3.0";
const char *const API_VERSION_HEADER = "X-GitHub-Api-Version: 2022-11-28";

class CurlEasyHandle {
public:
	explicit CurlEasyHandle(CURL *handle_p) : handle(handle_p) {
	}

	~CurlEasyHandle() noexcept {
		if (handle) {
			curl_easy_cleanup(handle);
		}
	}

	CURL *Get() const noexcept {
		return handle;
	}

private:
	CURL *handle;
};

class CurlHeaderList {
public:
	CurlHeaderList() : headers(nullptr) {
	}

	~CurlHeaderList() noexcept {
		if (headers) {
			curl_slist_free_all(headers);
		}
	}

	bool Append(const char *header) noexcept {
		auto appended = curl_slist_append(headers, header);
		if (!appended) {
			return false;
		}
		headers = appended;
		return true;
	}

	curl_slist *Get() const noexcept {
		return headers;
	}

private:
	curl_slist *headers;
};

struct TransferState {
	TransferState(ExecutionControl &control_p, const HttpLimits &limits_p)
	    : control(control_p), limits(limits_p), header_bytes(0), response_bytes(0), cancelled(false),
	      timed_out(false), header_oversized(false), response_oversized(false), decompressed_oversized(false),
	      allocation_failed(false), address_denied(false) {
	}

	bool ShouldContinue() noexcept {
		if (control.IsCancellationRequested()) {
			cancelled = true;
			return false;
		}
		if (std::chrono::steady_clock::now() >= limits.deadline) {
			timed_out = true;
			return false;
		}
		return true;
	}

	ExecutionControl &control;
	const HttpLimits &limits;
	uint64_t header_bytes;
	uint64_t response_bytes;
	std::string body;
	bool cancelled;
	bool timed_out;
	bool header_oversized;
	bool response_oversized;
	bool decompressed_oversized;
	bool allocation_failed;
	bool address_denied;
};

bool AddWithin(uint64_t current, std::size_t amount, uint64_t limit, uint64_t &result) noexcept {
	if (current > limit || static_cast<uint64_t>(amount) > limit - current) {
		return false;
	}
	result = current + static_cast<uint64_t>(amount);
	return true;
}

std::size_t WriteBody(char *data, std::size_t size, std::size_t count, void *opaque) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	if (!state.ShouldContinue()) {
		return 0;
	}
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
		state.decompressed_oversized = true;
		return 0;
	}
	const auto length = size * count;
	uint64_t updated_size = 0;
	if (!AddWithin(static_cast<uint64_t>(state.body.size()), length, state.limits.max_decompressed_bytes,
	               updated_size)) {
		state.decompressed_oversized = true;
		return 0;
	}
	(void)updated_size;
	try {
		state.body.append(data, length);
	} catch (...) {
		state.allocation_failed = true;
		return 0;
	}
	return length;
}

std::size_t ReadHeader(char *, std::size_t size, std::size_t count, void *opaque) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	if (!state.ShouldContinue()) {
		return 0;
	}
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
		state.header_oversized = true;
		return 0;
	}
	const auto length = size * count;
	uint64_t updated_size = 0;
	if (!AddWithin(state.header_bytes, length, state.limits.max_header_bytes, updated_size)) {
		state.header_oversized = true;
		return 0;
	}
	state.header_bytes = updated_size;
	return length;
}

int TransferProgress(void *opaque, curl_off_t total, curl_off_t current, curl_off_t, curl_off_t) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	if (total > 0 && static_cast<uint64_t>(total) > state.limits.max_response_bytes) {
		state.response_oversized = true;
		return 1;
	}
	if (current > 0) {
		state.response_bytes = static_cast<uint64_t>(current);
		if (state.response_bytes > state.limits.max_response_bytes) {
			state.response_oversized = true;
			return 1;
		}
	}
	return state.ShouldContinue() ? 0 : 1;
}

curl_socket_t OpenPublicSocket(void *opaque, curlsocktype purpose, struct curl_sockaddr *address) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	if (state.address_denied || purpose != CURLSOCKTYPE_IPCXN || !address ||
	    !IsPublicSocketAddress(&address->addr, address->addrlen)) {
		state.address_denied = true;
		return CURL_SOCKET_BAD;
	}
	return socket(address->family, address->socktype, address->protocol);
}

void RequireCurlOption(CURLcode result) {
	if (result != CURLE_OK) {
		throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP transport configuration failed");
	}
}

bool HasExpectedRequest(const HttpRequest &request) {
	return request.method == "GET" && request.scheme == "https" && request.host == "api.github.com" &&
	       request.port == 443 && request.target == "/search/users?q=duckdb+in%3Alogin&per_page=3" &&
	       request.headers.size() == 3 && request.headers[0].name == "Accept" &&
	       request.headers[0].value == "application/vnd.github+json" && request.headers[1].name == "User-Agent" &&
	       request.headers[1].value == "duckdb-api/0.3.0" &&
	       request.headers[2].name == "X-GitHub-Api-Version" && request.headers[2].value == "2022-11-28";
}

long RemainingMilliseconds(std::chrono::steady_clock::time_point deadline) {
	const auto now = std::chrono::steady_clock::now();
	if (now >= deadline) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
	}
	const auto remaining = deadline - now;
	auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
	if (milliseconds == 0) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
	}
	if (milliseconds > std::numeric_limits<long>::max()) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "wall-time budget is not executable");
	}
	return static_cast<long>(milliseconds);
}

void ValidateLimits(const HttpLimits &limits) {
	if (limits.max_header_bytes == 0 || limits.max_response_bytes == 0 || limits.max_decompressed_bytes == 0 ||
	    limits.max_decompressed_bytes > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) ||
	    limits.max_response_bytes > static_cast<uint64_t>(std::numeric_limits<curl_off_t>::max())) {
		throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP transport received an invalid resource budget");
	}
}

class CurlHttpTransport final : public HttpTransport {
public:
	explicit CurlHttpTransport(std::shared_ptr<const CurlProcessLifetime> lifetime_p)
	    : lifetime(std::move(lifetime_p)) {
	}

	HttpResponse Get(const HttpRequest &request, const HttpLimits &limits, ExecutionControl &control) const override {
		if (!HasExpectedRequest(request)) {
			throw ExecutionError(ErrorStage::POLICY, "", "HTTP request is outside the installed execution profile");
		}
		ValidateLimits(limits);
		if (control.IsCancellationRequested()) {
			throw ExecutionCancelled();
		}

		try {
			TransferState state(control, limits);
			CurlHeaderList headers;
			if (!headers.Append(ACCEPT_HEADER) || !headers.Append(USER_AGENT_HEADER) ||
			    !headers.Append(API_VERSION_HEADER)) {
				throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP request headers exceeded available memory");
			}
			CurlEasyHandle easy(curl_easy_init());
			if (!easy.Get()) {
				throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP transport is unavailable");
			}
			auto *handle = easy.Get();
			const auto timeout_milliseconds = RemainingMilliseconds(limits.deadline);
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_URL, PUBLIC_URL));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1)));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers.Get()));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https"));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https"));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_AUTOREFERER, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PROXY, ""));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PRE_PROXY, ""));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_NETRC, CURL_NETRC_IGNORED));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_NONE));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PROXYAUTH, CURLAUTH_NONE));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_UNRESTRICTED_AUTH, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_milliseconds));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, timeout_milliseconds));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, TransferProgress));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteBody));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, ReadHeader));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HEADERDATA, &state));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, ""));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTP_CONTENT_DECODING, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE,
			                                    static_cast<curl_off_t>(limits.max_response_bytes)));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_PATH_AS_IS, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 1L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_DNS_CACHE_TIMEOUT, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_HTTP09_ALLOWED, 0L));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, OpenPublicSocket));
			RequireCurlOption(curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, &state));

			const auto transfer_result = curl_easy_perform(handle);
			if (state.cancelled || control.IsCancellationRequested()) {
				throw ExecutionCancelled();
			}
			if (state.timed_out || transfer_result == CURLE_OPERATION_TIMEDOUT) {
				throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds",
				                     "execution exceeded its wall-time budget");
			}
			if (state.header_oversized) {
				throw ExecutionError(ErrorStage::RESOURCE, "header_bytes", "HTTP response exceeded its header budget");
			}
			if (state.response_oversized || transfer_result == CURLE_FILESIZE_EXCEEDED) {
				throw ExecutionError(ErrorStage::RESOURCE, "response_bytes", "HTTP response exceeded its byte budget");
			}
			if (state.decompressed_oversized) {
				throw ExecutionError(ErrorStage::RESOURCE, "decompressed_bytes",
				                     "HTTP response exceeded its decompressed-byte budget");
			}
			if (state.allocation_failed) {
				throw ExecutionError(ErrorStage::RESOURCE, "decompressed_bytes",
				                     "HTTP response could not be buffered within its budget");
			}
			if (state.address_denied) {
				throw ExecutionError(ErrorStage::POLICY, "", "resolved address is outside network policy");
			}
			if (transfer_result != CURLE_OK) {
				throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP request failed");
			}

			long response_status = 0;
			RequireCurlOption(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_status));
			curl_off_t response_bytes = 0;
			RequireCurlOption(curl_easy_getinfo(handle, CURLINFO_SIZE_DOWNLOAD_T, &response_bytes));
			if (response_bytes < 0 || static_cast<uint64_t>(response_bytes) > limits.max_response_bytes) {
				throw ExecutionError(ErrorStage::RESOURCE, "response_bytes", "HTTP response exceeded its byte budget");
			}
			if (response_status < 0 || static_cast<unsigned long>(response_status) >
			                               static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
				throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP transport returned invalid metadata");
			}
			if (response_status < 200 || response_status >= 300) {
				state.body.clear();
			}
			return {static_cast<uint32_t>(response_status), state.header_bytes,
			        static_cast<uint64_t>(response_bytes), std::move(state.body)};
		} catch (const ExecutionCancelled &) {
			throw;
		} catch (const ExecutionError &) {
			throw;
		} catch (...) {
			// Raw dependency diagnostics may contain the destination or ambient
			// system details and never cross the runtime service boundary.
			throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP request failed");
		}
	}

private:
	const std::shared_ptr<const CurlProcessLifetime> lifetime;
};

} // namespace

std::unique_ptr<HttpTransport> BuildCurlHttpTransport(std::shared_ptr<const CurlProcessLifetime> lifetime) {
	if (!lifetime) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP runtime is not initialized");
	}
	return std::unique_ptr<HttpTransport>(new CurlHttpTransport(std::move(lifetime)));
}

} // namespace internal
} // namespace duckdb_api
