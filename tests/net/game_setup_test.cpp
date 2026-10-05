#include "../fixtures/game_event_capture.h"
#include "Kitsune/Net/GameSetup.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <variant>
#include <vector>

namespace {

using namespace kitsune::net;
namespace fx = kitsune::fixtures;

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

template <std::size_t N>
std::vector<game::Event> events(const std::uint8_t (&data)[N]) {
    const auto got = game::decodeEvents({reinterpret_cast<const std::byte*>(data), N});
    expect(got.complete, "fixture decodes");
    return got.events;
}

template <std::size_t N>
bool sameBytes(const std::vector<game::Event>& sent, const std::uint8_t (&want)[N]) {
    std::vector<std::byte> bytes;
    return game::encodeEvents(sent, bytes) && bytes.size() == N &&
           std::memcmp(bytes.data(), want, N) == 0;
}

const std::vector<game::Event> kNothing;

// Our guest against the turns of an official host (numbers from the capture).
void guestAnswersTheHost() {
    SetupParams params;
    params.firstTurn = 20;
    params.race = 1;
    auto guest = GameSetup::guest(params);

    expect(guest.onTurn(18, kNothing).empty() && guest.onTurn(19, kNothing).empty(),
           "nothing before our first turn is out");
    auto out = guest.onTurn(20, kNothing);
    expect(sameBytes(out, fx::kSetLobbySlot), "the slot is the official guest's, byte for byte");
    expect(guest.onTurn(21, kNothing).empty(), "the slot is sent once");

    expect(guest.onTurn(23, events(fx::kStartGame)).empty(), "starting sends nothing yet");
    expect(guest.phase() == SetupPhase::Starting, "e_startGame starts the game");

    out = guest.onTurn(25, events(fx::kHostLoadPackage));
    expect(sameBytes(out, fx::kGuestLoadPackage),
           "the package is the official guest's, byte for byte");
    expect(guest.phase() == SetupPhase::Loading, "package sent");

    expect(guest.onTurn(26, kNothing).empty(), "second finish waits two turns");
    out = guest.onTurn(27, events(fx::kUserFinishedLoading));
    expect(sameBytes(out, fx::kUserFinishedLoading), "second finish is one event");
    expect(guest.phase() == SetupPhase::Playing, "both sides finished");
    expect(guest.onTurn(29, kNothing).empty(), "nothing more to send");
}

// Our host against the turns of an official guest.
void hostLeadsTheGuest() {
    SetupParams params;
    params.loadingTime = 1791188297; // the capture's
    auto host = GameSetup::host(params);

    expect(host.onTurn(20, kNothing).empty(), "nothing before the slot");
    expect(host.onTurn(21, events(fx::kSetLobbySlot)).empty(), "the slot is taken");
    expect(host.onTurn(22, kNothing).empty(), "start waits for its turn");
    auto out = host.onTurn(23, kNothing);
    expect(sameBytes(out, fx::kStartGame), "e_startGame, byte for byte");
    expect(host.phase() == SetupPhase::Starting, "starting");

    expect(host.onTurn(24, kNothing).empty(), "package waits");
    out = host.onTurn(25, events(fx::kGuestLoadPackage));
    expect(sameBytes(out, fx::kHostLoadPackage),
           "the package is the official host's, byte for byte");

    out = host.onTurn(27, events(fx::kUserFinishedLoading));
    expect(sameBytes(out, fx::kUserFinishedLoading), "second finish is one event");
    out = host.onTurn(29, kNothing);
    expect(out.size() == 1 && std::holds_alternative<game::PeerSetSyncPlayingTimeEvent>(out[0]) &&
               std::get<game::PeerSetSyncPlayingTimeEvent>(out[0]).syncTime == 1791188301,
           "playing time is four seconds after loading time, as captured");
    expect(host.phase() == SetupPhase::Playing, "playing");
}

} // namespace

int main() {
    guestAnswersTheHost();
    hostLeadsTheGuest();
    if (gFailures == 0) {
        std::cout << "game_setup ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
