#pragma once

// Base-port handshake between a joining guest and the host, as two official
// 4.10 clients do it (docs/tcp-base.md, "Session order"):
//
//   guest  e_connect  ->  host
//   guest  <-  e_connectReply   host
//   guest  e_join     ->  host
//   guest  <-  e_joinSucceeded  host      guest answers with e_ackSystem
//
// Both classes are byte-in, byte-out state machines. They own no socket. The
// caller reads the stream with decodeSystemStream, passes each record in, and
// writes whatever comes back. Turns and everything after the join are not here.

#include "Kitsune/Net/SystemEvent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace kitsune::net {

enum class HandshakeState {
    Idle,
    AwaitReply,   // guest sent e_connect
    AwaitJoined,  // guest sent e_join
    AwaitConnect, // host is waiting for a guest
    AwaitJoin,    // host answered e_connect
    Joined,
    Failed,
};

struct HandshakeStep {
    std::vector<std::byte> out; // empty when there is nothing to send
    HandshakeState state = HandshakeState::Idle;
};

// An IPv4 address as the wire carries it: version 4, eight 16-bit words with
// the four octets in the first two, big endian.
[[nodiscard]] NetAddress ipv4Address(const std::array<std::uint8_t, 4>& octets, std::uint16_t port);

struct GuestParams {
    std::uint32_t peerId = 0;     // random per process, kept for the whole session
    std::uint32_t secret = 0;     // this guest's own session secret
    std::uint32_t sessionId = 0;  // from the host's here-is
    std::uint32_t hostSecret = 0; // here-is m_sessionSecret
    std::uint16_t listenPort = 0; // this guest's base port
    std::uint32_t programId = 0;
    Version version{};
    UserInitialData user{};
};

class GuestHandshake {
public:
    explicit GuestHandshake(GuestParams params) : params_(std::move(params)) {}

    // The e_connect record.
    [[nodiscard]] HandshakeStep start();

    // Records the host sends. Anything not expected in the current state is
    // ignored. An error reply or a refused join fails the handshake.
    [[nodiscard]] HandshakeStep onMessage(const SystemEvent& message);

    [[nodiscard]] HandshakeState state() const noexcept { return state_; }
    [[nodiscard]] const std::optional<JoinSucceededEvent>& joined() const noexcept {
        return joined_;
    }

private:
    GuestParams params_;
    HandshakeState state_ = HandshakeState::Idle;
    std::optional<JoinSucceededEvent> joined_;
};

struct HostParams {
    std::uint32_t peerId = 0;
    std::uint32_t sessionId = 0;
    std::uint32_t sessionSecret = 0; // the secret the here-is advertised
    std::uint32_t programId = 0;
    Version version{};
    // What the host tells a guest that joined: tags, users, lobby, sequences.
    // Only the header numbers are filled in here.
    JoinSucceededEvent joined{};
};

class HostHandshake {
public:
    explicit HostHandshake(HostParams params) : params_(std::move(params)) {}

    // `remote` is the address the guest's connection came from. The reply
    // gives it back with the guest's listen port.
    [[nodiscard]] HandshakeStep onMessage(const SystemEvent& message,
                                          const std::array<std::uint8_t, 4>& remote);

    [[nodiscard]] HandshakeState state() const noexcept { return state_; }
    [[nodiscard]] const std::optional<UserInfo>& guest() const noexcept { return guest_; }

private:
    HostParams params_;
    HandshakeState state_ = HandshakeState::AwaitConnect;
    std::uint16_t nextSequence_ = 1;
    std::optional<UserInfo> guest_;
};

} // namespace kitsune::net
