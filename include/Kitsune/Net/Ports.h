#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace kitsune::net {

// One game port and its base port. JoinGame packs a pair as a dword,
// game in the low 16 bits and base in the high 16.
struct PortPair {
    std::uint16_t game = 0;
    std::uint16_t base = 0;
};

// Half-open. [begin, end).
struct PortRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

enum class JoinRole {
    Host,
    Guest,
};

// What this process binds, and the one remote the virgin broadcast aims at.
struct JoinPorts {
    PortPair local{};
    PortPair remote{};
};

// PortGet_gCx / PortGet_gCxPlus1 when the remote game port is still 0.
inline constexpr std::uint16_t kUnsetGuestBroadcastBegin = 6111;
inline constexpr std::uint16_t kUnsetGuestBroadcastEnd = 6119;

[[nodiscard]] constexpr std::uint32_t packPortPair(PortPair ports) noexcept {
    return static_cast<std::uint32_t>(ports.game) |
           (static_cast<std::uint32_t>(ports.base) << 16);
}

[[nodiscard]] constexpr PortPair unpackPortPair(std::uint32_t packed) noexcept {
    return PortPair{static_cast<std::uint16_t>(packed & 0xffffu),
                    static_cast<std::uint16_t>(packed >> 16)};
}

// One port. Peer bind walks [local game, local game + 1).
[[nodiscard]] constexpr PortRange singlePort(std::uint16_t port) noexcept {
    return PortRange{port, static_cast<std::uint32_t>(port) + 1u};
}

// 4.10's join consumer keeps the server pair and client_ports[0].
// The host binds the server pair and aims at that first client.
// A guest binds client_ports[0] and aims at the server.
// Further client ports are ignored. Empty client_ports yields nullopt.
[[nodiscard]] std::optional<JoinPorts> assignJoinPorts(JoinRole role, PortPair server,
                                                       std::span<const PortPair> clients);

// Virgin SendTo. A set remote game port is a one-port range.
// Zero selects [0x17df, 0x17e7).
[[nodiscard]] PortRange guestBroadcastRange(std::uint16_t remoteGame) noexcept;

} // namespace kitsune::net
