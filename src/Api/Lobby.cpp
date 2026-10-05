#include "Kitsune/Api/Lobby.h"

namespace kitsune::api {

std::optional<LobbyFamily> LobbyDispatch::family(std::uint8_t firstByte) noexcept {
    if (firstByte == kLobbyStartByte) {
        return LobbyFamily::Start;
    }
    if (firstByte == kLobbyLobbyByte) {
        return LobbyFamily::Lobby;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> LobbyDispatch::startEvent(int which) noexcept {
    if (which == 10) {
        return kStartOfflineGame;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> LobbyDispatch::lobbyEvent(int outer, int inner) noexcept {
    if (outer == 0 && inner == 1) {
        return kLobbyAddPlayer;
    }
    return std::nullopt;
}

} // namespace kitsune::api
