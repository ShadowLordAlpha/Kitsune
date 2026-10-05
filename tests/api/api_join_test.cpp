#include "Kitsune/Api/JoinGame.h"
#include "Kitsune/Api/Lobby.h"

#include <cstdlib>
#include <iostream>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

} // namespace

int main() {
    using kitsune::api::JoinStatus;
    using kitsune::api::LobbyDispatch;
    using kitsune::api::LobbyFamily;

    kitsune::api::JoinGame join;
    const auto waiting = join.client(0);
    expect(waiting.status == JoinStatus::Waiting && waiting.error == 0, "join waits for the start");
    expect(join.client(1).status == JoinStatus::Waiting, "the first guest also waits");
    expect(join.client(2).status == JoinStatus::NetworkError && join.client(2).error == 12,
           "a third client is NetworkError");

    join.gameStarted();
    expect(join.client(0).status == JoinStatus::Response, "host response follows Game has started");
    expect(join.client(1).status == JoinStatus::Response && join.client(1).error == 0,
           "first guest response");
    expect(join.client(2).error == kitsune::api::kNetworkError, "start does not admit a third client");

    expect(LobbyDispatch::family(3) == LobbyFamily::Start, "byte 3 is the start family");
    expect(LobbyDispatch::family(9) == LobbyFamily::Lobby, "byte 9 is the lobby family");
    expect(!LobbyDispatch::family(1).has_value(), "an unknown first byte");
    expect(LobbyDispatch::startEvent(10) == kitsune::api::kStartOfflineGame, "case 10 queues 0x77");
    expect(!LobbyDispatch::startEvent(9).has_value(), "other start cases are not recorded");
    expect(LobbyDispatch::lobbyEvent(0, 1) == kitsune::api::kLobbyAddPlayer, "outer 0 inner 1 queues 0x8a");
    expect(!LobbyDispatch::lobbyEvent(0, 0).has_value(), "other lobby cases are not recorded");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
