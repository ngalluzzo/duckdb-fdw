#include "duckdb_api/internal/curl_transfer.hpp"

#include <curl/curl.h>

#include <sys/socket.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

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
	CurlEasyHandle(const CurlEasyHandle &) = delete;
	CurlEasyHandle &operator=(const CurlEasyHandle &) = delete;
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

	bool Append(const std::string &header) noexcept {
		auto appended = curl_slist_append(headers, header.c_str());
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
	CurlHeaderList(const CurlHeaderList &) = delete;
	CurlHeaderList &operator=(const CurlHeaderList &) = delete;
	curl_slist *headers;
};

struct TransferState {
	TransferState(ExecutionControl &control_p, const HttpLimits &limits_p, const CurlTransferProfile &profile_p)
	    : control(control_p), limits(limits_p), profile(profile_p), header_bytes(0), response_bytes(0),
	      cancelled(false), timed_out(false), header_oversized(false), response_oversized(false),
	      decompressed_oversized(false), allocation_failed(false), address_denied(false), socket_attempted(false) {
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
	const CurlTransferProfile &profile;
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
	bool socket_attempted;
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

curl_socket_t OpenPolicySocket(void *opaque, curlsocktype purpose, struct curl_sockaddr *address) noexcept {
	auto &state = *static_cast<TransferState *>(opaque);
	// libcurl may otherwise try multiple DNS answers (including Happy Eyeballs)
	// within one easy transfer. The preview permits one connection attempt, so
	// a second socket is unavailable even when its address would be allowed.
	if (state.socket_attempted) {
		return CURL_SOCKET_BAD;
	}
	if (state.address_denied || purpose != CURLSOCKTYPE_IPCXN || !address || !state.profile.socket_policy ||
	    !state.profile.socket_policy(&address->addr, address->addrlen, state.profile.socket_policy_context)) {
		state.address_denied = true;
		return CURL_SOCKET_BAD;
	}
	state.socket_attempted = true;
	return socket(address->family, address->socktype, address->protocol);
}

void RequireCurlOption(CURLcode result) {
	if (result != CURLE_OK) {
		throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP transport configuration failed");
	}
}

// Every easy-handle option is applied through this wrapper so private tests
// observe exactly the configuration that libcurl receives. Observer fields,
// normalization, and calls compile out of production objects entirely.
class CurlOptionSetter {
public:
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
	CurlOptionSetter(CURL *handle_p, const CurlTransferProfile &profile_p) : handle(handle_p), profile(profile_p) {
	}
#else
	explicit CurlOptionSetter(CURL *handle_p) : handle(handle_p) {
	}
#endif

	template <class VALUE>
	void Set(CURLoption option, VALUE value) const {
		RequireCurlOption(curl_easy_setopt(handle, option, value));
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
		if (profile.option_observer) {
			const auto normalized = Normalize(value);
			profile.option_observer(option, normalized.c_str(), profile.option_observer_context);
		}
#endif
	}

private:
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
	static std::string Normalize(const char *value) {
		return value ? value : "<null>";
	}

	template <class VALUE>
	static typename std::enable_if<std::is_integral<VALUE>::value || std::is_enum<VALUE>::value, std::string>::type
	Normalize(VALUE value) {
		return std::to_string(static_cast<long long>(value));
	}

	template <class VALUE>
	static typename std::enable_if<std::is_pointer<VALUE>::value, std::string>::type Normalize(VALUE value) {
		return value ? "<set>" : "<null>";
	}
#endif

	CURL *handle;
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
	const CurlTransferProfile &profile;
#endif
};

long RemainingMilliseconds(std::chrono::steady_clock::time_point deadline) {
	const auto now = std::chrono::steady_clock::now();
	if (now >= deadline) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
	}
	const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
	if (milliseconds <= 0) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
	}
	if (milliseconds > std::numeric_limits<long>::max()) {
		throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "wall-time budget is not executable");
	}
	return static_cast<long>(milliseconds);
}

void ValidateInputs(const CurlTransferProfile &profile, const HttpRequest &request, const HttpLimits &limits) {
	if (!profile.url || !profile.protocols || !profile.socket_policy || request.headers.empty()) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP transport profile is invalid");
	}
	if (limits.max_header_bytes == 0 || limits.max_response_bytes == 0 || limits.max_decompressed_bytes == 0 ||
	    limits.max_decompressed_bytes > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) ||
	    limits.max_response_bytes > static_cast<uint64_t>(std::numeric_limits<curl_off_t>::max())) {
		throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP transport received an invalid resource budget");
	}
}

void BuildHeaders(const HttpRequest &request, CurlHeaderList &headers) {
	for (std::size_t index = 0; index < request.headers.size(); index++) {
		const auto header = request.headers[index].name + ": " + request.headers[index].value;
		if (!headers.Append(header)) {
			throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP request headers exceeded available memory");
		}
	}
}

} // namespace

#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
const char *PrivateCurlOptionObserverCanary() noexcept {
	return "duckdb_api_private_curl_option_observer_v1";
}
#endif

HttpResponse PerformCurlTransfer(const CurlTransferProfile &profile, const HttpRequest &request,
                                 const HttpLimits &limits, ExecutionControl &control) {
	ValidateInputs(profile, request, limits);
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}

	try {
		TransferState state(control, limits, profile);
		CurlHeaderList headers;
		CurlHeaderList resolve_entries;
		BuildHeaders(request, headers);
		CurlEasyHandle easy(curl_easy_init());
		if (!easy.Get()) {
			throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP transport is unavailable");
		}
		auto *handle = easy.Get();
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
		const CurlOptionSetter options(handle, profile);
#else
		const CurlOptionSetter options(handle);
#endif
		const auto timeout_milliseconds = RemainingMilliseconds(limits.deadline);
		options.Set(CURLOPT_URL, profile.url);
		options.Set(CURLOPT_HTTPGET, 1L);
		options.Set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
		options.Set(CURLOPT_HTTPHEADER, headers.Get());
		options.Set(CURLOPT_PROTOCOLS_STR, profile.protocols);
		options.Set(CURLOPT_REDIR_PROTOCOLS_STR, profile.protocols);
		options.Set(CURLOPT_FOLLOWLOCATION, 0L);
		options.Set(CURLOPT_MAXREDIRS, 0L);
		options.Set(CURLOPT_AUTOREFERER, 0L);
		options.Set(CURLOPT_PROXY, "");
		options.Set(CURLOPT_PRE_PROXY, "");
		options.Set(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
		options.Set(CURLOPT_HTTPAUTH, CURLAUTH_NONE);
		options.Set(CURLOPT_PROXYAUTH, CURLAUTH_NONE);
		options.Set(CURLOPT_UNRESTRICTED_AUTH, 0L);
		options.Set(CURLOPT_SSL_VERIFYPEER, 1L);
		options.Set(CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
		if (profile.trusted_ca_file) {
			options.Set(CURLOPT_CAINFO, profile.trusted_ca_file);
		}
		if (profile.resolve_entry) {
			if (!resolve_entries.Append(profile.resolve_entry)) {
				throw ExecutionError(ErrorStage::RESOURCE, "", "test resolution entry exceeded available memory");
			}
			options.Set(CURLOPT_RESOLVE, resolve_entries.Get());
		}
#endif
		options.Set(CURLOPT_TIMEOUT_MS, timeout_milliseconds);
		options.Set(CURLOPT_CONNECTTIMEOUT_MS, timeout_milliseconds);
		options.Set(CURLOPT_NOSIGNAL, 1L);
		options.Set(CURLOPT_NOPROGRESS, 0L);
		options.Set(CURLOPT_XFERINFOFUNCTION, TransferProgress);
		options.Set(CURLOPT_XFERINFODATA, &state);
		options.Set(CURLOPT_WRITEFUNCTION, WriteBody);
		options.Set(CURLOPT_WRITEDATA, &state);
		options.Set(CURLOPT_HEADERFUNCTION, ReadHeader);
		options.Set(CURLOPT_HEADERDATA, &state);
		options.Set(CURLOPT_ACCEPT_ENCODING, "");
		options.Set(CURLOPT_HTTP_CONTENT_DECODING, 1L);
		options.Set(CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(limits.max_response_bytes));
		options.Set(CURLOPT_PATH_AS_IS, 1L);
		options.Set(CURLOPT_FRESH_CONNECT, 1L);
		options.Set(CURLOPT_FORBID_REUSE, 1L);
		options.Set(CURLOPT_DNS_CACHE_TIMEOUT, 0L);
		options.Set(CURLOPT_HTTP09_ALLOWED, 0L);
		options.Set(CURLOPT_OPENSOCKETFUNCTION, OpenPolicySocket);
		options.Set(CURLOPT_OPENSOCKETDATA, &state);

		const auto transfer_result = curl_easy_perform(handle);
		if (state.cancelled || control.IsCancellationRequested()) {
			throw ExecutionCancelled();
		}
		if (state.address_denied) {
			throw ExecutionError(ErrorStage::POLICY, "", "resolved address is outside network policy");
		}
		if (state.timed_out || transfer_result == CURLE_OPERATION_TIMEDOUT) {
			throw ExecutionError(ErrorStage::RESOURCE, "wall_milliseconds", "execution exceeded its wall-time budget");
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
		return {static_cast<uint32_t>(response_status), state.header_bytes, static_cast<uint64_t>(response_bytes),
		        std::move(state.body)};
	} catch (const ExecutionCancelled &) {
		throw;
	} catch (const ExecutionError &) {
		throw;
	} catch (const std::bad_alloc &) {
		throw ExecutionError(ErrorStage::RESOURCE, "", "HTTP transport exceeded available memory");
	} catch (...) {
		throw ExecutionError(ErrorStage::TRANSPORT, "", "HTTP request failed");
	}
}

} // namespace internal
} // namespace duckdb_api
