#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "Kitsune/Net/LocalEvent.h"
#include "Kitsune/Net/UdpSocket.h"

namespace kitsune::net {

// Loopback stand-in for the UDP half of a 1v1 join. The guest stays silent
// until IHaveThisGame, then answers with ShowMeThisGame. The host answers
// that show-me with HereIsThisGame. IDontHaveThisGame stays a peer-list
// datagram and does not complete the exchange.
enum class UdpJoinRole {
    Host,
    Guest,
};

enum class UdpJoinStatus {
    Completed,
    TimedOut,
    NeedIHave,
};

struct UdpInbound {
    UdpPeer from{};
    std::vector<std::byte> bytes;
    std::optional<LocalEvent> record;
};

class UdpJoin {
public:
    explicit UdpJoin(UdpJoinRole role);

    [[nodiscard]] bool bind(std::uint16_t port);
    [[nodiscard]] std::uint16_t localPort() const noexcept;

    // Host only. The event must be IHaveThisGame.
    void useIHave(IHaveThisGameEvent have);

    // Host only. A matching ShowMe is answered with these bytes, so the
    // description is a captured here-is rather than an empty one.
    void useHereIsDatagram(std::vector<std::byte> datagram);

    // Optional. Unit tests leave this empty so they stay quiet.
    void setLog(std::function<void(std::string_view)> log);

    // Host requires remote. Guest replies to whoever sent IHaveThisGame.
    [[nodiscard]] UdpJoinStatus exchange(const UdpPeer* remote, std::chrono::milliseconds budget);

    // Reads until the budget and does not send. Used after exchange so a
    // later datagram from the other role is still recorded.
    void collect(std::chrono::milliseconds budget);

    [[nodiscard]] const std::vector<UdpInbound>& inbound() const noexcept;

private:
    void note(std::string_view line) const;
    void observe(const UdpSocket::Packet& packet);

    UdpJoinRole role_;
    UdpSocket socket_{};
    std::optional<IHaveThisGameEvent> have_{};
    std::optional<std::vector<std::byte>> hereBytes_{};
    std::vector<UdpInbound> inbound_{};
    std::function<void(std::string_view)> log_{};
};

} // namespace kitsune::net
