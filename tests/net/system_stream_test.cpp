#include "../fixtures/base_port_capture.h"
#include "Kitsune/Net/SystemEvent.h"

#include <array>
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

struct Record {
    const char* name;
    const std::uint8_t* data;
    std::size_t size;
    SystemEventId id;
};

template <std::size_t N>
Record record(const char* name, const std::uint8_t (&data)[N], SystemEventId id) {
    return {name, data, N, id};
}

std::span<const std::byte> view(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const std::byte*>(data), size};
}

const std::array<Record, 11>& records() {
    static const std::array<Record, 11> all = {
        record("connect", fx::kConnect, SystemEventId::Connect),
        record("connectReply", fx::kConnectReply, SystemEventId::ConnectReply),
        record("join", fx::kJoin, SystemEventId::Join),
        record("joinSucceeded", fx::kJoinSucceeded, SystemEventId::JoinSucceeded),
        record("ackSystem", fx::kAckSystem, SystemEventId::AckSystem),
        record("ackTurn", fx::kAckTurn, SystemEventId::AckTurn),
        record("ackMessage", fx::kAckMessage, SystemEventId::AckMessage),
        record("message", fx::kMessage, SystemEventId::Message),
        record("turnEmpty", fx::kTurnEmpty, SystemEventId::Turn),
        record("turnOneEvent", fx::kTurnOneEvent, SystemEventId::Turn),
        record("turnLobby", fx::kTurnLobby, SystemEventId::Turn),
    };
    return all;
}

void wholeRecords() {
    for (const auto& r : records()) {
        const auto got = decodeSystemStream(view(r.data, r.size));
        expect(got.status == StreamStatus::Complete, r.name);
        expect(got.consumed == r.size, r.name);
        expect(got.event && systemEventId(*got.event) == r.id, r.name);
        if (!got.event) {
            continue;
        }
        std::vector<std::byte> out(kStreamBufferBytes);
        const auto written = encodeSystemEvent(*got.event, out);
        expect(written && *written == r.size && std::memcmp(out.data(), r.data, r.size) == 0,
               r.name);
    }
}

void fields() {
    const auto connect = decodeSystemEvent(view(fx::kConnect, sizeof fx::kConnect));
    const auto* body = connect ? std::get_if<ConnectEvent>(&*connect) : nullptr;
    expect(body && body->header.sequence == 0 && body->header.ackSequence == 0 &&
               body->header.originatingPeer == 3424787007u,
           "connect header");
    expect(body && body->version.build == 75689 && body->listenPort == 27153, "connect body");

    const auto reply = decodeSystemEvent(view(fx::kConnectReply, sizeof fx::kConnectReply));
    expect(reply && systemHeader(*reply).sequence == 1 && systemHeader(*reply).ackSequence == 1,
           "connectReply header");

    const auto join = decodeSystemEvent(view(fx::kJoin, sizeof fx::kJoin));
    expect(join && systemHeader(*join).sequence == 1 && systemHeader(*join).ackSequence == 2,
           "join header");

    const auto joined = decodeSystemEvent(view(fx::kJoinSucceeded, sizeof fx::kJoinSucceeded));
    const auto* lobby = joined ? std::get_if<JoinSucceededEvent>(&*joined) : nullptr;
    expect(lobby && lobby->users.size() == 1, "joinSucceeded users");

    const auto one = decodeSystemEvent(view(fx::kTurnOneEvent, sizeof fx::kTurnOneEvent));
    const auto* turn = one ? std::get_if<TurnEvent>(&*one) : nullptr;
    expect(turn && turn->gameEventData.size() == 1, "turn game events");

    const auto text = decodeSystemEvent(view(fx::kMessage, sizeof fx::kMessage));
    const auto* msg = text ? std::get_if<MessageEvent>(&*text) : nullptr;
    expect(msg && msg->messageData.size() == 5, "message bytes");
}

// Every strict prefix of a record is incomplete, never bad and never a shorter record.
void prefixesWait() {
    for (const auto& r : records()) {
        bool allWait = true;
        for (std::size_t n = 0; n < r.size; ++n) {
            const auto got = decodeSystemStream(view(r.data, n));
            if (got.status != StreamStatus::Truncated || got.consumed != 0 || got.event) {
                allWait = false;
            }
        }
        expect(allWait, r.name);
    }
}

// Records back to back in one buffer come out in order, each at the next byte.
void backToBack() {
    std::vector<std::uint8_t> stream;
    for (const auto& r : records()) {
        stream.insert(stream.end(), r.data, r.data + r.size);
    }
    std::size_t at = 0;
    std::size_t index = 0;
    while (at < stream.size()) {
        const auto got = decodeSystemStream(view(stream.data() + at, stream.size() - at));
        if (got.status != StreamStatus::Complete || index >= records().size()) {
            break;
        }
        expect(systemEventId(*got.event) == records()[index].id, "back to back id");
        expect(got.consumed == records()[index].size, "back to back size");
        at += got.consumed;
        ++index;
    }
    expect(index == records().size() && at == stream.size(), "back to back all");
}

// A receive pump hands over arbitrary chunks. Keep the remainder, as the client does.
void chunked() {
    std::vector<std::uint8_t> stream;
    for (const auto& r : records()) {
        stream.insert(stream.end(), r.data, r.data + r.size);
    }
    for (const std::size_t chunk : {1u, 7u, 100u, 1460u}) {
        std::vector<std::uint8_t> buffer;
        std::size_t decoded = 0;
        for (std::size_t at = 0; at < stream.size(); at += chunk) {
            const auto end = at + chunk < stream.size() ? at + chunk : stream.size();
            buffer.insert(buffer.end(), stream.begin() + static_cast<std::ptrdiff_t>(at),
                          stream.begin() + static_cast<std::ptrdiff_t>(end));
            for (;;) {
                const auto got = decodeSystemStream(view(buffer.data(), buffer.size()));
                if (got.status != StreamStatus::Complete) {
                    expect(got.status == StreamStatus::Truncated, "chunked never bad");
                    break;
                }
                buffer.erase(buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(got.consumed));
                ++decoded;
            }
        }
        expect(decoded == records().size() && buffer.empty(), "chunked all");
    }
}

void bad() {
    // Id 20 is not an event, and that does not depend on what follows.
    std::array<std::uint8_t, 11> unknown{};
    unknown[0] = 20;
    expect(decodeSystemStream(view(unknown.data(), unknown.size())).status == StreamStatus::Bad,
           "unknown id");

    // The last byte of an 83-bit record has five pad bits. They must be zero.
    std::array<std::uint8_t, sizeof fx::kAckTurn> padded{};
    std::memcpy(padded.data(), fx::kAckTurn, padded.size());
    padded.back() |= 0x80;
    expect(decodeSystemStream(view(padded.data(), padded.size())).status == StreamStatus::Bad,
           "dirty pad bits");

    expect(decodeSystemStream({}).status == StreamStatus::Truncated, "empty waits");
    expect(kStreamBufferBytes == 2920, "buffer size");
}

} // namespace

int main() {
    wholeRecords();
    fields();
    prefixesWait();
    backToBack();
    chunked();
    bad();
    if (gFailures == 0) {
        std::cout << "system_stream ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
