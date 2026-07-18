#include "support/loopback_curl_runtime.hpp"

#include "duckdb_api/internal/curl_http_transport.hpp"
#include "duckdb_api/internal/curl_transfer.hpp"
#include "duckdb_api/internal/http_scan_executor.hpp"
#include "duckdb_api/internal/http_transport.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace duckdb_api_test {

struct LoopbackCurlRuntime::State {
	explicit State(uint16_t port_p) : port(port_p), request_count(0), socket_policy_checks(0) {
	}

	const uint16_t port;
	std::atomic<uint64_t> request_count;
	mutable std::atomic<uint64_t> socket_policy_checks;
};

namespace {

bool HasFixedHeaders(const std::vector<duckdb_api::internal::HttpHeader> &headers) {
	return headers.size() >= 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.4.0" &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool IsBearerHeader(const duckdb_api::internal::HttpHeader &header) {
	return header.name == "Authorization" && header.value.size() > 7 && header.value.compare(0, 7, "Bearer ") == 0;
}

bool HasExpectedRequest(const duckdb_api::internal::HttpRequest &request) {
	if (request.method != "GET" || request.scheme != "https" || request.host != "api.github.com" ||
	    request.port != 443 || !HasFixedHeaders(request.headers)) {
		return false;
	}
	if (request.target == "/search/users?q=duckdb+in%3Alogin&per_page=3") {
		return request.headers.size() == 3;
	}
	return request.target == "/user" && request.headers.size() == 4 && IsBearerHeader(request.headers[3]);
}

bool IsOwnedLoopbackSocket(const sockaddr *address, socklen_t address_length, const void *context) noexcept {
	const auto &state = *static_cast<const LoopbackCurlRuntime::State *>(context);
	state.socket_policy_checks.fetch_add(1, std::memory_order_relaxed);
	if (!address || address->sa_family != AF_INET || address_length < sizeof(sockaddr_in)) {
		return false;
	}
	const auto &ipv4 = *reinterpret_cast<const sockaddr_in *>(address);
	return ntohl(ipv4.sin_addr.s_addr) == INADDR_LOOPBACK && ntohs(ipv4.sin_port) == state.port;
}

class LoopbackCurlTransport final : public duckdb_api::internal::HttpTransport {
public:
	explicit LoopbackCurlTransport(std::shared_ptr<LoopbackCurlRuntime::State> state_p)
	    : state(std::move(state_p)), authority("http://127.0.0.1:" + std::to_string(state->port)) {
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &request,
	                                       const duckdb_api::internal::HttpLimits &limits,
	                                       duckdb_api::ExecutionControl &control) const override {
		if (!HasExpectedRequest(request)) {
			throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::POLICY, "",
			                                 "HTTP request is outside the controlled test profile");
		}
		state->request_count.fetch_add(1, std::memory_order_relaxed);
		// This separately linked private seam routes an already validated public
		// request to the owned loopback socket. Installed objects contain neither
		// this numeric authority nor a caller-selected routing facility.
		const auto url = authority + request.target;
		const duckdb_api::internal::CurlTransferProfile profile {url.c_str(),
		                                                         "http",
		                                                         IsOwnedLoopbackSocket,
		                                                         state.get()
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
		                                                             ,
		                                                         nullptr,
		                                                         nullptr,
		                                                         nullptr,
		                                                         nullptr
#endif
		};
		return duckdb_api::internal::PerformCurlTransfer(profile, request, limits, control);
	}

private:
	const std::shared_ptr<LoopbackCurlRuntime::State> state;
	const std::string authority;
};

} // namespace

LoopbackCurlRuntime::LoopbackCurlRuntime(std::shared_ptr<State> state_p,
                                         std::shared_ptr<const duckdb_api::ScanExecutor> executor_p,
                                         duckdb_api::CompiledConnector connector_p)
    : state(std::move(state_p)), executor(std::move(executor_p)), connector(std::move(connector_p)) {
}

std::shared_ptr<const duckdb_api::ScanExecutor> LoopbackCurlRuntime::Executor() const {
	return executor;
}

const duckdb_api::CompiledConnector &LoopbackCurlRuntime::Connector() const {
	return connector;
}

LoopbackCurlObservation LoopbackCurlRuntime::Observation() const noexcept {
	return {state->request_count.load(std::memory_order_relaxed),
	        state->socket_policy_checks.load(std::memory_order_relaxed)};
}

std::shared_ptr<LoopbackCurlRuntime> BuildLoopbackCurlRuntime(uint16_t port) {
	if (port == 0) {
		throw duckdb_api::ExecutionError(duckdb_api::ErrorStage::INTERNAL, "",
		                                 "controlled HTTP service port is invalid");
	}
	auto state = std::make_shared<LoopbackCurlRuntime::State>(port);
	auto connector = duckdb_api::BuildNativeGithubConnector();
	(void)duckdb_api::internal::AcquireCurlProcessLifetime();
	std::unique_ptr<duckdb_api::internal::HttpTransport> transport(new LoopbackCurlTransport(state));
	const duckdb_api::internal::HttpExecutionProfile profile {duckdb_api::PlannedUrlScheme::HTTPS,
	                                                          "api.github.com",
	                                                          443,
	                                                          false,
	                                                          false,
	                                                          false,
	                                                          duckdb_api::MAX_EXECUTION_MILLISECONDS,
	                                                          3};
	auto executor = duckdb_api::internal::BuildHttpScanExecutorForProfile(std::move(transport), profile);
	return std::shared_ptr<LoopbackCurlRuntime>(
	    new LoopbackCurlRuntime(std::move(state), std::move(executor), std::move(connector)));
}

} // namespace duckdb_api_test
