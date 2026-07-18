#include "live_rest/internal/network_policy.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using live_rest::internal::DestinationProfile;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

bool AllowedIpv4(const char *text, DestinationProfile profile) {
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	Require(inet_pton(AF_INET, text, &address.sin_addr) == 1, "invalid IPv4 test input");
	return live_rest::internal::IsAllowedSocketAddress(reinterpret_cast<const sockaddr *>(&address), sizeof(address),
	                                                   profile);
}

bool AllowedIpv6(const char *text, DestinationProfile profile) {
	sockaddr_in6 address = {};
	address.sin6_family = AF_INET6;
	Require(inet_pton(AF_INET6, text, &address.sin6_addr) == 1, "invalid IPv6 test input");
	return live_rest::internal::IsAllowedSocketAddress(reinterpret_cast<const sockaddr *>(&address), sizeof(address),
	                                                   profile);
}

void TestPublicProfile() {
	Require(AllowedIpv4("140.82.112.5", DestinationProfile::PUBLIC_API), "public IPv4 was rejected");
	Require(AllowedIpv6("2606:50c0:8000::154", DestinationProfile::PUBLIC_API), "public IPv6 was rejected");

	const char *forbidden_ipv4[] = {"0.0.0.0",       "10.0.0.1",      "100.64.0.1",    "127.0.0.1",
	                                "169.254.169.254", "172.16.0.1",    "192.0.0.1",     "192.0.2.1",
	                                "192.88.99.1",    "192.168.0.1",   "198.18.0.1",    "198.51.100.1",
	                                "203.0.113.1",    "224.0.0.1",     "255.255.255.255"};
	for (const auto *address : forbidden_ipv4) {
		Require(!AllowedIpv4(address, DestinationProfile::PUBLIC_API),
		        std::string("special-purpose IPv4 was accepted: ") + address);
	}

	const char *forbidden_ipv6[] = {"::",         "::1",          "::ffff:127.0.0.1", "64:ff9b::7f00:1",
	                                "2001::1",    "2001:2::1",    "2001:20::1",       "2001:db8::1",
	                                "2002::1",    "3fff::1",      "fc00::1",           "fe80::1",
	                                "ff02::1"};
	for (const auto *address : forbidden_ipv6) {
		Require(!AllowedIpv6(address, DestinationProfile::PUBLIC_API),
		        std::string("special-purpose IPv6 was accepted: ") + address);
	}
}

void TestLoopbackOracleProfile() {
	Require(AllowedIpv4("127.0.0.1", DestinationProfile::LOOPBACK_ORACLE), "oracle loopback was rejected");
	Require(!AllowedIpv4("127.0.0.2", DestinationProfile::LOOPBACK_ORACLE), "alternate loopback was accepted");
	Require(!AllowedIpv4("140.82.112.5", DestinationProfile::LOOPBACK_ORACLE), "public address was accepted");
	Require(!AllowedIpv6("::1", DestinationProfile::LOOPBACK_ORACLE), "IPv6 loopback was accepted");
}

} // namespace

int main() {
	try {
		TestPublicProfile();
		TestLoopbackOracleProfile();
		std::cout << "live REST network policy tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "live REST network policy test failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
