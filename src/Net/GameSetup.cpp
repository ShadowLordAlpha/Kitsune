#include "Kitsune/Net/GameSetup.h"

#include <iterator>
#include <utility>
#include <variant>

namespace kitsune::net {

namespace {

// e_unpauseGame in every capture's loading package.
constexpr std::uint8_t kLoadingPauseType = 14;
// The package is sent two turns after the one that caused it, and so is the
// second e_userFinishedLoading after the package (docs/game-events.md).
constexpr std::uint16_t kStepTurns = 2;

game::Event slotEvent(const SetupParams& params) {
    game::SetLobbySlotEvent slot;
    slot.slotId = params.slot;
    game::LobbySlotChangeArmRacePref arm;
    arm.value.race = params.race;
    slot.slotChange = arm;
    return slot;
}

} // namespace

GameSetup GameSetup::guest(const SetupParams& params) {
    GameSetup setup;
    setup.role_ = Role::Guest;
    setup.params_ = params;
    return setup;
}

GameSetup GameSetup::host(const SetupParams& params) {
    GameSetup setup;
    setup.role_ = Role::Host;
    setup.params_ = params;
    setup.loadingTime_ = params.loadingTime != 0 ? params.loadingTime
                                                  : static_cast<std::uint32_t>(std::time(nullptr));
    return setup;
}

std::vector<game::Event> GameSetup::loadingPackage(bool withSyncTime) const {
    std::vector<game::Event> out;
    if (withSyncTime) {
        out.push_back(game::PeerSetSyncLoadingTimeEvent{loadingTime_});
    }
    out.push_back(game::UnpauseGameEvent{kLoadingPauseType});
    // The tables list the map's sounds; both sides must send the same two.
    for (const auto& sounds : peerSounds_) {
        out.push_back(sounds);
    }
    game::UserOptionsEvent options;
    options.testCheatsEnabled = true;
    options.multiplayerCheatsEnabled = true;
    options.baseBuildNum = params_.buildNum;
    options.buildNum = params_.buildNum;
    options.hotkeyProfile = "0_Default";
    out.push_back(std::move(options));
    out.push_back(game::UserFinishedLoadingEvent{});
    return out;
}

std::vector<game::Event> GameSetup::onTurn(std::uint16_t sequence, std::span<const game::Event> events) {
    bool startSeen = false;
    bool slotSeen = false;
    for (const auto& event : events) {
        if (std::holds_alternative<game::StartGameEvent>(event)) {
            startSeen = true;
        } else if (std::holds_alternative<game::SetLobbySlotEvent>(event)) {
            slotSeen = true;
        } else if (const auto* sounds = std::get_if<game::TriggerSoundLengthSyncEvent>(&event)) {
            peerSounds_.push_back(*sounds);
        } else if (std::holds_alternative<game::UserFinishedLoadingEvent>(event)) {
            ++peerFinished_;
        }
    }

    std::vector<game::Event> out;
    auto append = [&out](std::vector<game::Event> more) {
        std::move(more.begin(), more.end(), std::back_inserter(out));
    };

    if (role_ == Role::Guest) {
        if (!slotSent_ && sequence >= params_.firstTurn) {
            out.push_back(slotEvent(params_));
            slotSent_ = true;
        }
        if (startSeen && phase_ == SetupPhase::Lobby) {
            phase_ = SetupPhase::Starting;
        }
        // The host's package ends with its first e_userFinishedLoading.
        if (phase_ == SetupPhase::Starting && peerFinished_ >= 1) {
            append(loadingPackage(false));
            phase_ = SetupPhase::Loading;
            due_ = static_cast<std::uint16_t>(sequence + kStepTurns);
        }
        if (phase_ == SetupPhase::Loading && !finishSent_ && due_ && sequence >= *due_) {
            out.push_back(game::UserFinishedLoadingEvent{});
            finishSent_ = true;
        }
        if (phase_ == SetupPhase::Loading && peerFinished_ >= 2 && finishSent_) {
            phase_ = SetupPhase::Playing;
        }
        return out;
    }

    // Host: guest turns drive the steps.
    if (slotSeen && !due_) {
        due_ = static_cast<std::uint16_t>(sequence + params_.startDelay);
    }
    if (due_ && phase_ == SetupPhase::Lobby && sequence >= *due_) {
        out.push_back(game::StartGameEvent{});
        phase_ = SetupPhase::Starting;
        due_ = static_cast<std::uint16_t>(sequence + kStepTurns);
    } else if (due_ && phase_ == SetupPhase::Starting && sequence >= *due_) {
        append(loadingPackage(true));
        phase_ = SetupPhase::Loading;
        due_ = static_cast<std::uint16_t>(sequence + kStepTurns);
    } else if (due_ && phase_ == SetupPhase::Loading && !finishSent_ && sequence >= *due_) {
        out.push_back(game::UserFinishedLoadingEvent{});
        finishSent_ = true;
        due_ = static_cast<std::uint16_t>(sequence + kStepTurns);
    } else if (due_ && phase_ == SetupPhase::Loading && finishSent_ && sequence >= *due_) {
        out.push_back(game::PeerSetSyncPlayingTimeEvent{loadingTime_ + params_.playDelay});
        phase_ = SetupPhase::Playing;
    }
    return out;
}

} // namespace kitsune::net
