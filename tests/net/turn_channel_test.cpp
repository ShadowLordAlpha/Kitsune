#include "../fixtures/base_port_capture.h"
#include "Kitsune/Net/GameEvent.h"
#include "Kitsune/Net/TurnChannel.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <variant>

namespace {

using namespace kitsune::net;
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
SystemEvent decode(const std::uint8_t (&data)[N]) {
    auto message = decodeSystemEvent({reinterpret_cast<const std::byte*>(data), N});
    expect(message.has_value(), "fixture decodes");
    return message.value_or(SystemEvent{});
}

SystemEvent decodeBytes(const std::vector<std::byte>& bytes) {
    auto message = decodeSystemEvent(bytes);
    expect(message.has_value(), "channel output decodes");
    return message.value_or(SystemEvent{});
}

template <std::size_t N>
bool sameBytes(const std::vector<std::byte>& got, const std::uint8_t (&want)[N]) {
    return got.size() == N && std::memcmp(got.data(), want, N) == 0;
}

const TurnEvent& turnBody(const SystemEvent& message) {
    static const TurnEvent none;
    const auto* body = std::get_if<TurnEvent>(&message);
    expect(body != nullptr, "is a turn");
    return body ? *body : none;
}

bool emptyTurn(const SystemEvent& message, std::uint16_t sequence, std::uint16_t ack) {
    const auto& turn = turnBody(message);
    return systemEventId(message) == SystemEventId::Turn && turn.header.sequence == sequence &&
           turn.header.ackSequence == ack && turn.syncValue == kNoSyncValue &&
           turn.gameEventData.empty() && !turn.joiners && !turn.readies && !turn.requestRecycle;
}

JoinSucceededEvent joined() {
    return std::get<JoinSucceededEvent>(decode(fx::kJoinSucceeded));
}

void guestFollowsTheHost() {
    const auto self = systemHeader(decode(fx::kGuestTurnFirst)).originatingPeer;
    auto guest = TurnChannel::guest(self, joined());
    expect(guest.start().empty(), "a guest opens with nothing");
    expect(guest.nextExpected() == 18 && guest.nextToSend() == 20,
           "guest starts at the join numbers");

    // The host's first turn, then the one announcing us.
    expect(guest.onMessage(decode(fx::kTurnEmpty)).empty(), "no answer before our start");
    auto out = guest.onMessage(decode(fx::kTurnLobby));
    expect(out.size() == 1 && sameBytes(out[0], fx::kGuestTurnFirst),
           "first guest turn is byte for byte the official one");

    // Host turn N is answered with N+1.
    out = guest.onMessage(decode(fx::kHostTurn20));
    expect(out.size() == 1 && emptyTurn(decodeBytes(out[0]), 21, 21),
           "turn 20 is answered with 21");
    expect(guest.nextExpected() == 21 && guest.nextToSend() == 22, "guest counters advance");
    expect(guest.turnsReceived() == 3, "guest counted three turns");

    // A repeat or a gap is ignored.
    expect(guest.onMessage(decode(fx::kHostTurn20)).empty(), "repeat ignored");
    expect(guest.onMessage(decode(fx::kTurnOneEvent)).empty(), "gap ignored");
    expect(guest.onMessage(decode(fx::kAckSystem)).empty(), "a non-turn is ignored");
}

void hostLeadsThenAnswers() {
    const auto self = systemHeader(decode(fx::kTurnEmpty)).originatingPeer;
    const auto info = std::get<JoinEvent>(decode(fx::kJoin)).userInfo;
    auto host = TurnChannel::host(self, joined(), info);

    auto out = host.start();
    expect(out.size() == 2, "a host opens with two turns");
    if (out.size() == 2) {
        const auto first = decodeBytes(out[0]);
        expect(emptyTurn(first, 18, 18) && systemHeader(first).originatingPeer == self,
               "opening turn 18");

        const auto second = decodeBytes(out[1]);
        const auto official = decode(fx::kTurnLobby);
        const auto& sent = turnBody(second);
        const auto& captured = turnBody(official);
        expect(systemHeader(second).sequence == 19 && systemHeader(second).ackSequence == 18,
               "opening turn 19 numbers");
        expect(sent.joiners && captured.joiners && *sent.joiners == *captured.joiners,
               "turn 19 announces the joiner as the official host did");
        expect(sent.syncValue == kNoSyncValue && sent.gameEventData.empty(),
               "turn 19 is otherwise empty");
    }
    expect(host.start().empty(), "opening turns are sent once");

    // Guest turn N is answered with N.
    out = host.onMessage(decode(fx::kGuestTurnFirst));
    expect(out.size() == 1 && emptyTurn(decodeBytes(out[0]), 20, 21),
           "guest 20 is answered with 20");
    out = host.onMessage(decode(fx::kGuestTurnWithEvents));
    expect(out.size() == 1 && emptyTurn(decodeBytes(out[0]), 21, 22),
           "guest 21 is answered with 21");
    expect(host.peerEventBytes() == 4 && host.turnsReceived() == 2, "peer events are counted");
    expect(host.onMessage(decode(fx::kGuestTurnWithEvents)).empty(), "repeat ignored");
}

// Two of our channels run each other for a hundred turns.
void ourOwnPair() {
    const auto info = std::get<JoinEvent>(decode(fx::kJoin)).userInfo;
    auto host = TurnChannel::host(1, joined(), info);
    auto guest = TurnChannel::guest(2, joined());

    std::vector<std::vector<std::byte>> toGuest = host.start();
    std::vector<std::vector<std::byte>> toHost;
    for (int step = 0; step < 400 && host.nextToSend() < 120; ++step) {
        std::vector<std::vector<std::byte>> nextToGuest;
        for (const auto& bytes : toGuest) {
            for (auto& reply : guest.onMessage(decodeBytes(bytes))) {
                toHost.push_back(std::move(reply));
            }
        }
        toGuest.clear();
        for (const auto& bytes : toHost) {
            for (auto& reply : host.onMessage(decodeBytes(bytes))) {
                nextToGuest.push_back(std::move(reply));
            }
        }
        toHost.clear();
        toGuest = std::move(nextToGuest);
    }
    expect(host.nextToSend() >= 120 && guest.nextToSend() >= 120, "pair reaches turn 120");
    expect(host.nextExpected() >= 120 && guest.nextExpected() >= 119,
           "pair keeps its acks current");
}

// Queued events go out in the next turns and arrive decoded; a handler's events ride the answer.
void eventsRideTheTurns() {
    const auto info = std::get<JoinEvent>(decode(fx::kJoin)).userInfo;
    auto host = TurnChannel::host(1, joined(), info);
    auto guest = TurnChannel::guest(2, joined());

    std::vector<game::Event> seen;
    std::uint16_t seenAt = 0;
    host.setEventHandler([&](std::uint16_t sequence, std::span<const game::Event> events) {
        seen.insert(seen.end(), events.begin(), events.end());
        seenAt = sequence;
        std::vector<game::Event> answer;
        if (!events.empty()) {
            answer.push_back(game::StartGameEvent{});
        }
        return answer;
    });

    // Three events of about 650 bytes do not fit in one turn: two go, one waits.
    game::TriggerSoundLengthSyncEvent big;
    big.syncInfo.soundHash.assign(80, 0x12345678u);
    big.syncInfo.length.assign(80, 0u);
    guest.queue(game::Event{big});
    guest.queue(std::vector<game::Event>{game::Event{big}, game::Event{big}});
    expect(guest.eventsPending() == 3, "three events queued");

    for (const auto& bytes : host.start()) {
        for (const auto& reply : guest.onMessage(decodeBytes(bytes))) {
            const auto message = decodeBytes(reply);
            expect(turnBody(message).gameEventData.size() <= game::kEventDataMax,
                   "a turn stays in budget");
            for (const auto& answer : host.onMessage(message)) {
                expect(turnBody(decodeBytes(answer)).gameEventData.size() == 1,
                       "the handler's event rides the answer");
            }
        }
    }
    expect(seen.size() == 2 && seenAt == 20 && guest.eventsPending() == 1,
           "two events arrive, the third waits for the next turn");
    expect(host.eventsReceived() == 2 && host.undecodedTurns() == 0, "counted");
}

} // namespace

int main() {
    guestFollowsTheHost();
    hostLeadsThenAnswers();
    ourOwnPair();
    eventsRideTheTurns();
    if (gFailures == 0) {
        std::cout << "turn_channel ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
