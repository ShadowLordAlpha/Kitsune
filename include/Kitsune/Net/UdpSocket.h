#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kitsune::net {

// One peer. address is an IPv4-mapped IPv6 address (::ffff:a.b.c.d).
// The socket underneath is Asio IPv4. The Linux client calls socket() with
// domain 10, which is AF_INET6 there and the wrong number on Windows, so
// that 10 stays in DatagramOpen and is not passed to Asio. The join's
// host address is IPv4. Windows does not deliver those datagrams to a
// dual-stack IPv6 socket on loopback.
struct UdpPeer {
    std::array<std::uint8_t, 16> address{};
    std::uint16_t port = 0;
};

// 127.0.0.1 as an IPv4-mapped IPv6 address.
[[nodiscard]] UdpPeer udpLoopback(std::uint16_t port) noexcept;

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(UdpSocket&&) noexcept;
    UdpSocket& operator=(UdpSocket&&) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // IPv4 UDP.
    [[nodiscard]] bool open();
    [[nodiscard]] bool bind(std::uint16_t port);
    [[nodiscard]] std::uint16_t localPort() const noexcept;

    [[nodiscard]] bool sendTo(const UdpPeer& peer, std::span<const std::byte> payload);

    struct Packet {
        std::vector<std::byte> bytes;
        UdpPeer from{};
    };

    // Waits up to timeout. An empty optional is a timeout or a closed socket.
    [[nodiscard]] std::optional<Packet> recvFrom(std::chrono::milliseconds timeout);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kitsune::net
