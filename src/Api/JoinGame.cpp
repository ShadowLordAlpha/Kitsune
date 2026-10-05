#include "Kitsune/Api/JoinGame.h"

namespace kitsune::api {

void JoinGame::gameStarted() noexcept {
    started_ = true;
}

bool JoinGame::started() const noexcept {
    return started_;
}

JoinDecision JoinGame::client(int clientIndex) const noexcept {
    // Linux Base75689 sends ResponseJoinGame from 0x0103b776 after
    // "Game has started." A third participant comes back as error 12.
    if (clientIndex < 0 || clientIndex >= kCompletingClients) {
        return JoinDecision{JoinStatus::NetworkError, kNetworkError};
    }
    if (!started_) {
        return JoinDecision{};
    }
    return JoinDecision{JoinStatus::Response, 0};
}

} // namespace kitsune::api
