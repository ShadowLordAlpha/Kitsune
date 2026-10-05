#include "Kitsune/Api/CreateGame.h"

namespace kitsune::api {
namespace {

[[nodiscard]] bool isAgent(PlayerType type) noexcept {
    // GCC on Base75689 wrote this as (type | 2) == 3.
    return type == PlayerType::Participant || type == PlayerType::Observer;
}

} // namespace

CreateGameDecision checkCreateGame(std::span<const PlayerType> slots) {
    int agents = 0;
    for (const PlayerType type : slots) {
        if (isAgent(type)) {
            ++agents;
        }
    }

    if (agents == 0) {
        return CreateGameDecision{CreateGameStatus::NoAgent, kMissingPlayerSetup, kNoAgentDetail};
    }
    if (agents == 1) {
        return CreateGameDecision{};
    }
    if (agents == 2 && slots.size() == 2) {
        return CreateGameDecision{};
    }
    return CreateGameDecision{CreateGameStatus::NotOneVersusOne, kInvalidPlayerSetup,
                              kNotOneVersusOneDetail};
}

} // namespace kitsune::api
