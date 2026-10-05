#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace kitsune::api {

// sc2api.proto PlayerType. Values are the protobuf numbers.
enum class PlayerType : std::int32_t {
    Participant = 1,
    Computer = 2,
    Observer = 3,
};

enum class CreateGameStatus {
    Ok,
    NoAgent,
    NotOneVersusOne,
};

// ResponseCreateGame.Error. The zero-agent path does not store InvalidPlayerSetup.
inline constexpr int kMissingPlayerSetup = 6;
inline constexpr int kInvalidPlayerSetup = 7;

inline constexpr std::string_view kNoAgentDetail =
    "There must be at least one participant or observer";

inline constexpr std::string_view kNotOneVersusOneDetail =
    "Only 1v1 is supported when using multiple agents";

struct CreateGameDecision {
    CreateGameStatus status = CreateGameStatus::Ok;
    int error = 0;
    std::string_view detail{};
};

// Linux Base75689 rejects a CreateGame whose agent slots are not 1v1.
// An agent is a Participant or an Observer. Computers do not count.
// One agent is accepted with any number of Computers. Two agents are
// accepted only when those two slots are the entire setup.
[[nodiscard]] CreateGameDecision checkCreateGame(std::span<const PlayerType> slots);

} // namespace kitsune::api
