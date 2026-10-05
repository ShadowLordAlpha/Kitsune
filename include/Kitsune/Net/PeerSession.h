#pragma once

#include <cstdint>
#include <optional>

#include "Kitsune/Net/Datagram.h"

namespace kitsune::net {

// SendOnly. A connection is stored only while the peer count is 0.
// Every send uses that first stored connection.
class SendLane {
public:
    void setPeerCount(int count) noexcept;
    [[nodiscard]] int peerCount() const noexcept;
    bool store(int connection) noexcept;
    [[nodiscard]] std::optional<int> target() const noexcept;

private:
    int peerCount_ = 0;
    std::optional<int> first_{};
};

// Session_begin sets the gate with no peer-count test.
// singleSlot is true only when the counted slots sum to exactly one.
class StartGate {
public:
    void begin(int slotCount) noexcept;
    [[nodiscard]] bool open() const noexcept;
    [[nodiscard]] bool singleSlot() const noexcept;

private:
    bool open_ = false;
    bool singleSlot_ = false;
};

// The two bind-once channels PeerTick calls, plus the lane and the start gate.
// game() is the socket SendTo uses. base() is the second channel.
// Both bind [localGame, localGame + 1) and keep the first success.
class PeerSession {
public:
    [[nodiscard]] SocketSlot& game() noexcept;
    [[nodiscard]] SocketSlot& base() noexcept;
    [[nodiscard]] SendLane& lane() noexcept;
    [[nodiscard]] StartGate& start() noexcept;

    template <class Accepts>
    std::optional<std::uint16_t> bindGame(std::uint16_t localGame, Accepts accepts) {
        return bindOne(gameBound_, gamePort_, localGame, accepts);
    }

    template <class Accepts>
    std::optional<std::uint16_t> bindBase(std::uint16_t localGame, Accepts accepts) {
        return bindOne(baseBound_, basePort_, localGame, accepts);
    }

private:
    template <class Accepts>
    std::optional<std::uint16_t> bindOne(bool& bound, std::optional<std::uint16_t>& stored,
                                         std::uint16_t localGame, Accepts accepts) {
        if (bound) {
            return stored;
        }
        const auto port = firstSuccessfulPort(singlePort(localGame), accepts);
        if (!port) {
            return std::nullopt;
        }
        bound = true;
        stored = port;
        return port;
    }

    SocketSlot game_{};
    SocketSlot base_{};
    bool gameBound_ = false;
    bool baseBound_ = false;
    std::optional<std::uint16_t> gamePort_{};
    std::optional<std::uint16_t> basePort_{};
    SendLane lane_{};
    StartGate start_{};
};

} // namespace kitsune::net
