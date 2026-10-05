#include "Kitsune/Net/Ports.h"

namespace kitsune::net {

std::optional<JoinPorts> assignJoinPorts(JoinRole role, PortPair server,
                                         std::span<const PortPair> clients) {
    // Linux Base75689 JoinGame port apply (0x01052ed0) reads the server
    // dword and the first packed client port. A 1v1 Simple64 run stored
    // host 27150/27151 local and 27152/27153 remote; the guest swapped them.
    if (clients.empty()) {
        return std::nullopt;
    }
    const PortPair client = clients.front();
    if (role == JoinRole::Host) {
        return JoinPorts{server, client};
    }
    return JoinPorts{client, server};
}

PortRange guestBroadcastRange(std::uint16_t remoteGame) noexcept {
    // Linux Base75689 SendTo (0x01b6ab70) walks [g_cx, g_cx+1).
    // g_cx == 0 selects [0x17df, 0x17e7).
    if (remoteGame == 0) {
        return PortRange{kUnsetGuestBroadcastBegin, kUnsetGuestBroadcastEnd};
    }
    return singlePort(remoteGame);
}

} // namespace kitsune::net
