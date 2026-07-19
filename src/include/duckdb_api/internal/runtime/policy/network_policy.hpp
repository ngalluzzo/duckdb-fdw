#pragma once

#include <sys/socket.h>

namespace duckdb_api {
namespace internal {

// Applies the installed public-authority policy to a DNS result immediately
// before libcurl opens a socket. Only globally routable unicast IPv4/IPv6 is
// admitted; loopback test authority is deliberately absent from product code.
bool IsPublicSocketAddress(const sockaddr *address, socklen_t address_length) noexcept;

} // namespace internal
} // namespace duckdb_api
