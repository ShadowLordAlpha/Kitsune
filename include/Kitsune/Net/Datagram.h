#pragma once

#include <cstdint>
#include <optional>

#include "Kitsune/Net/Ports.h"

namespace kitsune::net {

// Arguments PeerConnect passes to socket() on Linux Base75689.
// Domain 10 is AF_INET6 on Linux. Winsock uses a different value.
struct DatagramOpen {
    static constexpr int kDomain = 10;
    static constexpr int kType = 2;
    static constexpr int kProtocol = 0x11;
};

inline constexpr int kEmptySocketFd = -1;
inline constexpr int kBindStatusReject = 2;

// PeerConnect_defaultPorts. Inclusive 0x17f7..0x17fe, eight ports.
inline constexpr std::uint16_t kFallbackBindBegin = 6135;
inline constexpr std::uint16_t kFallbackBindLast = 6142;

[[nodiscard]] PortRange fallbackBindRange() noexcept;

// Socket_init stores -1 and does not close the previous fd.
// Socket_close skips -1.
struct SocketSlot {
    int fd = kEmptySocketFd;

    int init() noexcept;
    bool close() noexcept;

    // Bind status 2 closes the fd and fails. Any other status keeps it.
    bool applyBind(int status) noexcept;
};

// True when Socket_bind_status's return is the reject value.
[[nodiscard]] constexpr bool bindRejected(int status) noexcept {
    return status == kBindStatusReject;
}

// PeerLoopA / PeerLoopB. Walk [begin, end) and stop at the first port
// `accepts` allows. The success path does not try the next port.
template <class Accepts>
[[nodiscard]] std::optional<std::uint16_t> firstSuccessfulPort(PortRange range, Accepts accepts) {
    for (std::uint32_t port = range.begin; port < range.end; ++port) {
        if (port > 0xffffu) {
            break;
        }
        if (accepts(static_cast<std::uint16_t>(port))) {
            return static_cast<std::uint16_t>(port);
        }
    }
    return std::nullopt;
}

} // namespace kitsune::net
