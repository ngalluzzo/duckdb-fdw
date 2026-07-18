#pragma once

#include <sys/socket.h>

namespace live_rest {
namespace internal {

enum class DestinationProfile { PUBLIC_API, LOOPBACK_ORACLE };

// Applies the trial's post-DNS destination policy to a socket address before
// libcurl is allowed to open it. The public profile accepts only globally
// routable unicast addresses; the controlled profile accepts only 127.0.0.1.
bool IsAllowedSocketAddress(const sockaddr *address, socklen_t address_length,
                            DestinationProfile profile) noexcept;

} // namespace internal
} // namespace live_rest
