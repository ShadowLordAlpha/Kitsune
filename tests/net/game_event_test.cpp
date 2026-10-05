#include "../fixtures/game_event_capture.h"
#include "../fixtures/game_event_vectors.h"
#include "Kitsune/Net/GameEvent.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

namespace game = kitsune::net::game;
namespace fx = kitsune::fixtures;

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

template <std::size_t N>
std::span<const std::byte> view(const std::uint8_t (&data)[N]) {
    return {reinterpret_cast<const std::byte*>(data), N};
}

std::span<const std::byte> view(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const std::byte*>(data), size};
}

bool same(const std::vector<std::byte>& got, std::span<const std::byte> want) {
    return got.size() == want.size() && std::memcmp(got.data(), want.data(), want.size()) == 0;
}

// Every event id decodes as exactly one event and encodes back to the same bytes.
void everyEventRoundTrips() {
    std::vector<bool> seen(120, false);
    for (const auto& vector : fx::kEventVectors) {
        const auto bytes = view(vector.data, vector.size);
        const auto got = game::decodeEvents(bytes);
        expect(got.complete && got.events.size() == 1 && got.consumed == vector.size,
               "vector decodes");
        if (got.events.size() != 1) {
            continue;
        }
        expect(static_cast<std::uint8_t>(game::eventId(got.events[0])) == vector.id, "vector id");
        std::vector<std::byte> again;
        expect(game::encodeEvents(got.events, again) && same(again, bytes), "vector re-encodes");
        seen[vector.id] = true;
    }
    std::size_t covered = 0;
    for (const bool flag : seen) {
        covered += flag ? 1 : 0;
    }
    expect(covered == 120, "a vector for each of the 120 ids");
}

// Real blobs from two official clients come back byte for byte.
template <std::size_t N>
game::EventsDecode captured(const std::uint8_t (&data)[N], const char* what) {
    const auto got = game::decodeEvents(view(data));
    expect(got.complete && got.consumed == N, what);
    std::vector<std::byte> again;
    expect(game::encodeEvents(got.events, again) && same(again, view(data)), what);
    return got;
}

void capturedBlobs() {
    // The guest takes slot 1 and picks its race, as the capture shows.
    const auto slot = captured(fx::kSetLobbySlot, "set lobby slot");
    const auto* event =
        slot.events.size() == 1 ? std::get_if<game::SetLobbySlotEvent>(&slot.events[0]) : nullptr;
    expect(event != nullptr && event->slotId == 1, "slot 1");
    const auto* race = event != nullptr
                           ? std::get_if<game::LobbySlotChangeArmRacePref>(&event->slotChange)
                           : nullptr;
    expect(race != nullptr && race->value.race == std::uint8_t{1}, "race preference 1");
    const auto start = captured(fx::kStartGame, "start game");
    expect(start.events.size() == 1 && game::eventId(start.events[0]) == game::EventId::StartGame,
           "start game is one event");

    const auto host = captured(fx::kHostLoadPackage, "host load package");
    const auto guest = captured(fx::kGuestLoadPackage, "guest load package");
    expect(host.events.size() == guest.events.size() + 1,
           "host sends one more event than the guest");
    if (!host.events.empty()) {
        expect(game::eventId(host.events[0]) == game::EventId::PeerSetSyncLoadingTime,
               "host opens with its sync loading time");
        expect(game::eventId(host.events.back()) == game::EventId::UserFinishedLoading,
               "the package ends with finished loading");
    }

    (void)captured(fx::kUserFinishedLoading, "user finished loading");
    (void)captured(fx::kHostFirstCommands, "host first commands");
    (void)captured(fx::kGuestFirstCommands, "guest first commands");
    (void)captured(fx::kCameraUpdate, "camera update");
    (void)captured(fx::kGuestSmall, "guest small");
    (void)captured(fx::kHostTargets, "host targets");
}

void rejectsBadInput() {
    // A cut-off event is not complete and keeps what decoded before it.
    const auto& vector = fx::kEventVectors[0];
    const auto cut = game::decodeEvents(view(vector.data, vector.size - 1));
    expect(!cut.complete && cut.consumed == 0, "a truncated event is not complete");

    // Padding bits must be zero.
    std::vector<std::uint8_t> dirty(fx::kStartGame, fx::kStartGame + sizeof fx::kStartGame);
    dirty.back() |= 0x80;
    expect(!game::decodeEvents(view(dirty.data(), dirty.size())).complete,
           "dirty padding is refused");

    // An empty blob is a valid, empty stream.
    const auto none = game::decodeEvents({});
    expect(none.complete && none.events.empty(), "no bytes is no events");

    // A field out of range does not encode.
    game::SetLobbySlotEvent bad;
    bad.slotId = 16;
    std::vector<std::byte> out;
    const std::vector<game::Event> events{bad};
    expect(!game::encodeEvents(events, out), "slot id 16 is out of range");

    expect(std::string(game::eventName(game::EventId::Cmd)) == "e_cmd",
           "event names are the client's");
}

} // namespace

int main() {
    everyEventRoundTrips();
    capturedBlobs();
    rejectsBadInput();
    if (gFailures == 0) {
        std::cout << "game_event ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
