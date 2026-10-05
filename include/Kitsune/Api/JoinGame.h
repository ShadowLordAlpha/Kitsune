#pragma once

#include <cstdint>

namespace kitsune::api {

// ResponseJoinGame.Error.NetworkError. Virgin 4.10 completes two joins.
inline constexpr int kNetworkError = 12;

enum class JoinStatus {
    Waiting,
    Response,
    NetworkError,
};

struct JoinDecision {
    JoinStatus status = JoinStatus::Waiting;
    int error = 0;
};

// The host and client_ports[0] emit ResponseJoinGame after "Game has started."
// Any later client is NetworkError. The JoinGame handler itself returns earlier.
class JoinGame {
public:
    static constexpr int kCompletingClients = 2;

    void gameStarted() noexcept;
    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] JoinDecision client(int clientIndex) const noexcept;

private:
    bool started_ = false;
};

} // namespace kitsune::api
