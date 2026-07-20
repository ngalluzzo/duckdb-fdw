#include "runtime/support/private_curl_probe.hpp"

#include "duckdb_api/internal/runtime/transport/curl_http_transport.hpp"
#include "duckdb_api/internal/runtime/transport/curl_transfer.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <atomic>
#include <chrono>
#include <utility>

#ifndef DUCKDB_API_PRIVATE_CURL_TESTS
#error "private curl probe requires DUCKDB_API_PRIVATE_CURL_TESTS"
#endif

namespace duckdb_api_test {
namespace {

struct SocketAuthority {
	SocketAuthority(uint16_t port_p, PrivateCurlSocketPolicy policy_p) : port(port_p), policy(policy_p), checks(0) {
	}

	uint16_t port;
	PrivateCurlSocketPolicy policy;
	mutable std::atomic<uint64_t> checks;
};

class PublishChecks {
public:
	PublishChecks(const SocketAuthority &authority_p, uint64_t *output_p) : authority(authority_p), output(output_p) {
	}

	~PublishChecks() noexcept {
		if (output) {
			*output = authority.checks.load(std::memory_order_relaxed);
		}
	}

private:
	const SocketAuthority &authority;
	uint64_t *output;
};

bool ApplySocketPolicy(const sockaddr *address, socklen_t address_length, const void *context) noexcept {
	const auto &authority = *static_cast<const SocketAuthority *>(context);
	authority.checks.fetch_add(1, std::memory_order_relaxed);
	if (authority.policy == PrivateCurlSocketPolicy::DENY_ALL || !address) {
		return false;
	}
	if (address->sa_family == AF_INET && address_length >= sizeof(sockaddr_in)) {
		const auto &ipv4 = *reinterpret_cast<const sockaddr_in *>(address);
		return (ntohl(ipv4.sin_addr.s_addr) & 0xff000000U) == 0x7f000000U && ntohs(ipv4.sin_port) == authority.port;
	}
	if (address->sa_family == AF_INET6 && address_length >= sizeof(sockaddr_in6)) {
		const auto &ipv6 = *reinterpret_cast<const sockaddr_in6 *>(address);
		return IN6_IS_ADDR_LOOPBACK(&ipv6.sin6_addr) && ntohs(ipv6.sin6_port) == authority.port;
	}
	return false;
}

duckdb_api::internal::HttpRequest BuildRequest(const PrivateCurlProbeOptions &options) {
	duckdb_api::internal::HttpRequest request;
	request.method = "GET";
	request.scheme = options.request_scheme;
	request.host = options.request_host;
	request.port = options.request_port;
	request.target = "/search/users?q=duckdb+in%3Alogin&per_page=3";
	request.headers = {{"Accept", "application/vnd.github+json"},
	                   {"User-Agent", "duckdb-api/0.6.0"},
	                   {"X-GitHub-Api-Version", "2022-11-28"}};
	return request;
}

duckdb_api::internal::HttpRequest BuildAuthorizedRequest(const PrivateCurlProbeOptions &options,
                                                         std::string bearer_token) {
	auto request = BuildRequest(options);
	request.target = "/user";
	request.headers.push_back({"Authorization", "Bearer " + std::move(bearer_token)});
	return request;
}

void ObserveOption(CURLoption option, const char *normalized_value, void *context) {
	auto &observations = *static_cast<std::vector<PrivateCurlProbeResult::OptionObservation> *>(context);
	observations.push_back({option, normalized_value ? normalized_value : "<null>"});
}

} // namespace

PrivateCurlProbeResult PerformProbe(const PrivateCurlProbeOptions &options, duckdb_api::internal::HttpRequest request,
                                    duckdb_api::ExecutionControl &control) {
	(void)duckdb_api::internal::AcquireCurlProcessLifetime();
	SocketAuthority authority(options.request_port, options.socket_policy);
	PublishChecks publish_checks(authority, options.completed_socket_policy_checks);
	std::vector<PrivateCurlProbeResult::OptionObservation> option_observations;
	option_observations.reserve(48);
	const duckdb_api::internal::HttpLimits limits {0,
	                                               duckdb_api::HOST_MAX_HEADER_BYTES,
	                                               duckdb_api::HOST_MAX_RESPONSE_BYTES,
	                                               duckdb_api::HOST_MAX_DECOMPRESSED_BYTES,
	                                               duckdb_api::HOST_MAX_HEADER_BYTES,
	                                               std::chrono::steady_clock::now() +
	                                                   std::chrono::milliseconds(options.wall_milliseconds)};
	const duckdb_api::internal::CurlTransferProfile profile {
	    options.url.c_str(),
	    options.protocols.c_str(),
	    ApplySocketPolicy,
	    &authority,
	    options.trusted_ca_file.empty() ? nullptr : options.trusted_ca_file.c_str(),
	    options.resolve_entry.empty() ? nullptr : options.resolve_entry.c_str(),
	    ObserveOption,
	    &option_observations};
	auto response = duckdb_api::internal::PerformCurlTransfer(profile, request, limits, control);
	return {std::move(response), authority.checks.load(std::memory_order_relaxed), std::move(option_observations)};
}

PrivateCurlProbeResult PerformPrivateCurlProbe(const PrivateCurlProbeOptions &options,
                                               duckdb_api::ExecutionControl &control) {
	return PerformProbe(options, BuildRequest(options), control);
}

PrivateCurlProbeResult PerformPrivateAuthorizedCurlProbe(const PrivateCurlProbeOptions &options,
                                                         std::string bearer_token,
                                                         duckdb_api::ExecutionControl &control) {
	return PerformProbe(options, BuildAuthorizedRequest(options, std::move(bearer_token)), control);
}

} // namespace duckdb_api_test
