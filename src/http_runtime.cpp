#include "duckdb_api/http_runtime.hpp"

#include "duckdb_api/internal/curl_http_transport.hpp"
#include "duckdb_api/internal/http_scan_executor.hpp"

#include <curl/curl.h>

#include <memory>
#include <string>
#include <utility>

#if LIBCURL_VERSION_NUM != 0x080701
#error "RFC 0005 supports only libcurl 8.7.1 headers"
#endif

namespace duckdb_api {
namespace internal {

// Shared ownership encodes the teardown ordering: the process-resident owner
// prevents unload-time cleanup, while every transport and active stream keeps
// this token alive until its final easy handle has been released.
class CurlProcessLifetime {
public:
	CurlProcessLifetime() : initialized(false) {
		const auto *version = curl_version_info(CURLVERSION_NOW);
		if (!version || version->version_num != 0x080701 || !version->version ||
		    std::string(version->version) != "8.7.1" || !version->ssl_version ||
		    std::string(version->ssl_version) != "SecureTransport (LibreSSL/3.3.6)" ||
		    (version->features & CURL_VERSION_THREADSAFE) == 0) {
			throw ExecutionError(ErrorStage::POLICY, "", "HTTP runtime identity is outside the supported cell");
		}

		identity.libcurl_version = version->version;
		identity.ssl_backend = version->ssl_version;
		identity.thread_safe = true;
		if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP runtime initialization failed");
		}
		initialized = true;
	}

	~CurlProcessLifetime() noexcept {
		if (initialized) {
			curl_global_cleanup();
		}
	}

	const HttpRuntimeIdentity &Identity() const noexcept {
		return identity;
	}

private:
	bool initialized;
	HttpRuntimeIdentity identity;
};

namespace {

std::shared_ptr<const CurlProcessLifetime> ProcessCurlLifetime() {
	// Function-local initialization is thread-safe in C++11. A failed identity
	// or init check does not publish the singleton and therefore cannot allow
	// registration to continue with a partially initialized runtime.
	static const std::shared_ptr<const CurlProcessLifetime> lifetime = std::make_shared<CurlProcessLifetime>();
	return lifetime;
}

} // namespace
} // namespace internal

HttpRuntimeService InitializeHttpRuntime() {
	try {
		auto lifetime = internal::ProcessCurlLifetime();
		HttpRuntimeService result;
		result.identity = lifetime->Identity();
		result.executor =
		    internal::BuildHttpScanExecutor(internal::BuildCurlHttpTransport(std::move(lifetime)));
		if (!result.executor) {
			throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP runtime initialization failed");
		}
		return result;
	} catch (const ExecutionError &) {
		throw;
	} catch (...) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "HTTP runtime initialization failed");
	}
}

} // namespace duckdb_api
