#pragma once

#include <cstdint>
#include <optional>

namespace kitsune::api {

// First byte of a lobby message selects the encoder.
// Byte 3 is the start family. Byte 9 is the lobby family.
enum class LobbyFamily {
    Start,
    Lobby,
};

inline constexpr std::uint8_t kLobbyStartByte = 3;
inline constexpr std::uint8_t kLobbyLobbyByte = 9;
inline constexpr std::uint8_t kStartOfflineGame = 0x77;
inline constexpr std::uint8_t kLobbyAddPlayer = 0x8a;

class LobbyDispatch {
public:
    [[nodiscard]] static std::optional<LobbyFamily> family(std::uint8_t firstByte) noexcept;

    // EncodeStartFamily case 10 queues type 0x77. Other cases are not recorded.
    [[nodiscard]] static std::optional<std::uint8_t> startEvent(int which) noexcept;

    // EncodeLobbyFamily outer 0, inner 1 queues type 0x8a.
    [[nodiscard]] static std::optional<std::uint8_t> lobbyEvent(int outer, int inner) noexcept;
};

} // namespace kitsune::api
