#pragma once

// The turn channel after a base-port join (docs/tcp-base.md, "Session order").
//
// Both peers send one e_turn per step and number them with the same running
// sequence. After the join the guest drives: when the host's turn N arrives the
// guest answers with N+1, and the host answers each guest turn N with its own
// N. `ack` is the next turn expected from the other side.
//
// This peer does not simulate, so every turn it sends has a sync value of 0.
// The receiver ignores a sync value of 0 when it checks for a desync, which is
// what the first turn of any joiner already sends. Game events (docs/game-events.md)
// ride in m_gameEventData: `queue` them and the next turn carries as many as fit.

#include "Kitsune/Net/GameEvent.h"
#include "Kitsune/Net/SystemEvent.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace kitsune::net {

inline constexpr std::uint16_t kNoSyncValue = 0;

class TurnChannel {
public:
    // A guest, once the host has answered e_join. `joined` carries the first
    // turn the host will send and the turn the guest starts on.
    [[nodiscard]] static TurnChannel guest(std::uint32_t peerId, const JoinSucceededEvent& joined);

    // A host, once it has sent e_joinSucceeded to `guestInfo`.
    [[nodiscard]] static TurnChannel host(std::uint32_t peerId, const JoinSucceededEvent& joined,
                                          const UserInfo& guestInfo);

    // Records to send straight away. A host opens with two turns, the second
    // announcing the joiner. A guest has none.
    [[nodiscard]] std::vector<std::vector<std::byte>> start();

    // One record from the other side. Returns the records to send in answer.
    // Anything that is not the next expected turn is ignored: a repeat means
    // our ack has not arrived, and our next turn carries it.
    [[nodiscard]] std::vector<std::vector<std::byte>> onMessage(const SystemEvent& message);

    // Events for a coming turn, oldest first. One that cannot encode is dropped.
    void queue(game::Event event);
    void queue(std::vector<game::Event> events);

    // Called with the events of each accepted turn before our answer is built,
    // so what it returns goes out in that answer.
    using EventHandler =
        std::function<std::vector<game::Event>(std::uint16_t sequence, std::span<const game::Event>)>;
    void setEventHandler(EventHandler handler) { handler_ = std::move(handler); }

    [[nodiscard]] std::size_t eventsPending() const noexcept { return pending_.size(); }
    [[nodiscard]] std::size_t eventsReceived() const noexcept { return eventsReceived_; }
    // Turns whose m_gameEventData did not decode in full.
    [[nodiscard]] std::size_t undecodedTurns() const noexcept { return undecoded_; }

    [[nodiscard]] std::uint16_t nextExpected() const noexcept { return expected_; }
    [[nodiscard]] std::uint16_t nextToSend() const noexcept { return next_; }
    [[nodiscard]] std::size_t turnsReceived() const noexcept { return received_; }
    [[nodiscard]] std::size_t peerEventBytes() const noexcept { return eventBytes_; }

private:
    enum class Role { Host, Guest };

    [[nodiscard]] std::vector<std::byte> turn(std::optional<std::vector<TurnJoiner>> joiners);
    void take(const TurnEvent& peer, std::uint16_t sequence);

    Role role_ = Role::Guest;
    std::uint32_t peerId_ = 0;
    std::uint16_t expected_ = 0; // next turn we accept from the other side
    std::uint16_t ack_ = 0;      // what our turns name as the next expected
    std::uint16_t next_ = 0;     // next turn we send
    std::uint16_t first_ = 0;    // the host's first turn, the sync sequence
    UserTag joinerTag_{};
    std::optional<UserInfo> joiner_;
    std::size_t received_ = 0;
    std::size_t eventBytes_ = 0;
    std::deque<game::Event> pending_;
    EventHandler handler_;
    std::size_t eventsReceived_ = 0;
    std::size_t undecoded_ = 0;
};

} // namespace kitsune::net
