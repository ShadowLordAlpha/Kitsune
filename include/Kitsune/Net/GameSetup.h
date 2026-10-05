#pragma once

// The game events a peer sends between the join and the first command
// (docs/game-events.md, "The setup exchange"). A GameSetup reads the other
// side's events and says which events to send next. TurnChannel carries them.
//
// Guest: take a slot, then answer the host's loading package with our own.
// Host: start the game, then send the loading package. The host path is only
// checked against the capture, not against an official guest.

#include "Kitsune/Net/GameEvent.h"

#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <vector>

namespace kitsune::net {

enum class SetupPhase {
    Lobby,    // waiting for the game to start
    Starting, // the game has started, our package is not out yet
    Loading,  // our package is out, waiting for the peer to finish
    Playing,  // both sides have finished loading
};

struct SetupParams {
    std::uint8_t slot = 1;           // the slot a guest takes
    std::optional<std::uint8_t> race; // the guest's race preference, unset for random
    std::uint16_t firstTurn = 0;     // the guest's first turn; slot is taken on its second
    std::uint32_t buildNum = 75689;
    std::uint32_t startDelay = 2;    // host: guest turns between taking the slot and e_startGame
    std::uint32_t playDelay = 4;     // host: seconds between the two sync times
    std::uint32_t loadingTime = 0;   // host: e_peerSetSyncLoadingTime, 0 means the wall clock
};

class GameSetup {
public:
    [[nodiscard]] static GameSetup guest(const SetupParams& params);
    [[nodiscard]] static GameSetup host(const SetupParams& params);

    // The peer's events in turn `sequence`. Returns what to send in our next turn.
    [[nodiscard]] std::vector<game::Event> onTurn(std::uint16_t sequence,
                                                  std::span<const game::Event> events);

    [[nodiscard]] SetupPhase phase() const noexcept { return phase_; }

private:
    enum class Role { Host, Guest };

    [[nodiscard]] std::vector<game::Event> loadingPackage(bool withSyncTime) const;

    Role role_ = Role::Guest;
    SetupParams params_;
    SetupPhase phase_ = SetupPhase::Lobby;
    bool slotSent_ = false;
    bool finishSent_ = false;                // our second e_userFinishedLoading
    std::optional<std::uint16_t> due_;       // the peer turn that triggers our next step
    std::size_t peerFinished_ = 0;           // e_userFinishedLoading seen
    std::vector<game::TriggerSoundLengthSyncEvent> peerSounds_;
    std::uint32_t loadingTime_ = 0;
};

} // namespace kitsune::net
