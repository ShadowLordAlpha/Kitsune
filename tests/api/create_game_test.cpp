#include "Kitsune/Api/CreateGame.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

kitsune::api::CreateGameDecision decide(std::initializer_list<kitsune::api::PlayerType> slots) {
    const std::vector<kitsune::api::PlayerType> setup(slots);
    return kitsune::api::checkCreateGame(setup);
}

} // namespace

int main() {
    using kitsune::api::CreateGameStatus;
    using kitsune::api::PlayerType;

    const auto empty = decide({});
    expect(empty.status == CreateGameStatus::NoAgent, "empty setup");
    expect(empty.error == kitsune::api::kMissingPlayerSetup, "empty setup is MissingPlayerSetup");
    expect(empty.detail == kitsune::api::kNoAgentDetail, "empty detail");

    const auto computers = decide({PlayerType::Computer, PlayerType::Computer, PlayerType::Computer});
    expect(computers.status == CreateGameStatus::NoAgent, "computers are not agents");

    const auto oneBot = decide({PlayerType::Participant, PlayerType::Computer, PlayerType::Computer,
                                PlayerType::Computer});
    expect(oneBot.status == CreateGameStatus::Ok, "one participant plus computers");
    expect(oneBot.error == 0, "ok has no error");
    expect(oneBot.detail.empty(), "ok has no detail");

    const auto oneObserver = decide({PlayerType::Observer});
    expect(oneObserver.status == CreateGameStatus::Ok, "one observer");

    const auto twoBots = decide({PlayerType::Participant, PlayerType::Participant});
    expect(twoBots.status == CreateGameStatus::Ok, "exactly two participants");

    const auto twoObservers = decide({PlayerType::Observer, PlayerType::Observer});
    expect(twoObservers.status == CreateGameStatus::Ok, "exactly two observers");

    const auto mixedPair = decide({PlayerType::Participant, PlayerType::Observer});
    expect(mixedPair.status == CreateGameStatus::Ok, "participant plus observer");

    const auto twoPlusComputer =
        decide({PlayerType::Participant, PlayerType::Participant, PlayerType::Computer});
    expect(twoPlusComputer.status == CreateGameStatus::NotOneVersusOne, "two agents plus a computer");
    expect(twoPlusComputer.error == kitsune::api::kInvalidPlayerSetup, "error 7");
    expect(twoPlusComputer.detail == kitsune::api::kNotOneVersusOneDetail, "1v1 detail");

    const auto threeBots =
        decide({PlayerType::Participant, PlayerType::Participant, PlayerType::Participant});
    expect(threeBots.status == CreateGameStatus::NotOneVersusOne, "three participants");
    expect(threeBots.error == 7, "three participants is InvalidPlayerSetup");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
