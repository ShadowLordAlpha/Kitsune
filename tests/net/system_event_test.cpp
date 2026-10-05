#include "Kitsune/Net/Describe.h"
#include "Kitsune/Net/SystemEvent.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <variant>
#include <vector>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

// e_ackTurn, both sequences 0x1a, no user, recycle 0, peer 0.
// The first six bytes are the captured base-port prefix.
constexpr std::uint8_t kAckTurn[] = {
    0x03, 0x00, 0x1a, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Same header with originatingPeer 0xA1B2C3D4.
constexpr std::uint8_t kAckTurnPeer[] = {
    0x03, 0x00, 0x1a, 0x00, 0x1a, 0x00, 0xa0, 0x36, 0x58, 0x7a, 0x04,
};

// userId 7 set. Still 11 bytes; the presence bit lands in the second 0x1a.
constexpr std::uint8_t kAckTurnUser[] = {
    0x03, 0x00, 0x1a, 0x00, 0x5a, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// e_resendMessage with a zero header. The notes' kind-0x11 reply starts this way.
constexpr std::uint8_t kResend[] = {
    0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::span<const std::byte> bytes(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const std::byte*>(data), size};
}

bool sameBytes(std::span<const std::byte> got, const std::uint8_t* want, std::size_t size) {
    if (got.size() != size) {
        return false;
    }
    for (std::size_t i = 0; i < size; ++i) {
        if (got[i] != static_cast<std::byte>(want[i])) {
            return false;
        }
    }
    return true;
}

kitsune::net::AckTurnEvent ackTurn(std::uint32_t peer) {
    kitsune::net::AckTurnEvent message;
    message.header.sequence = 0x1a;
    message.header.ackSequence = 0x1a;
    message.header.originatingPeer = peer;
    return message;
}

} // namespace

int main() {
    using kitsune::net::SystemEvent;
    using kitsune::net::SystemEventId;

    const auto turned = kitsune::net::decodeSystemEvent(bytes(kAckTurn, sizeof kAckTurn));
    expect(turned.has_value(), "decode ack turn");
    if (turned) {
        const auto& header = kitsune::net::systemHeader(*turned);
        expect(kitsune::net::systemEventId(*turned) == SystemEventId::AckTurn, "ack turn id");
        expect(header.sequence == 0x1a && header.ackSequence == 0x1a, "sequences");
        expect(!header.originalSenderTag.userId.has_value() &&
                   header.originalSenderTag.recycle == 0,
               "empty user tag");
        expect(header.originatingPeer == 0, "peer 0");
    }

    std::array<std::byte, 48> out{};
    const auto size = kitsune::net::encodeSystemEvent(ackTurn(0), out);
    expect(size == sizeof kAckTurn && sameBytes({out.data(), *size}, kAckTurn, sizeof kAckTurn),
           "ack turn matches the captured prefix");

    const auto peerSize = kitsune::net::encodeSystemEvent(ackTurn(0xA1B2C3D4), out);
    expect(peerSize == sizeof kAckTurnPeer &&
               sameBytes({out.data(), *peerSize}, kAckTurnPeer, sizeof kAckTurnPeer),
           "originating peer");

    auto withUser = ackTurn(0);
    withUser.header.originalSenderTag.userId = 7;
    const auto userSize = kitsune::net::encodeSystemEvent(withUser, out);
    expect(userSize == sizeof kAckTurnUser &&
               sameBytes({out.data(), *userSize}, kAckTurnUser, sizeof kAckTurnUser),
           "user id is 4 bits");
    const auto userBack = kitsune::net::decodeSystemEvent(bytes(kAckTurnUser, sizeof kAckTurnUser));
    expect(userBack && kitsune::net::systemHeader(*userBack).originalSenderTag.userId == 7,
           "user id round trip");

    const auto resend = kitsune::net::decodeSystemEvent(bytes(kResend, sizeof kResend));
    expect(resend && kitsune::net::systemEventId(*resend) == SystemEventId::ResendMessage &&
               kitsune::net::systemHeader(*resend).sequence == 0,
           "resend message is the 0x11 record");

    kitsune::net::ConnectEvent connect;
    connect.header = ackTurn(0).header;
    connect.programId = 0x5332;
    connect.version.flags = 1;
    connect.version.major = 4;
    connect.version.minor = 10;
    connect.version.build = 75689;
    connect.version.baseBuild = 75689;
    connect.sessionId = 1;
    connect.sessionSecret = 0x11223344;
    connect.listenPort = 27153;
    const auto connectSize = kitsune::net::encodeSystemEvent(connect, out);
    expect(connectSize == 37, "connect is 37 bytes");
    const auto connectBack =
        connectSize ? kitsune::net::decodeSystemEvent({out.data(), *connectSize}) : std::nullopt;
    expect(connectBack && *connectBack == SystemEvent{connect}, "connect round trip");

    auto badUser = ackTurn(0);
    badUser.header.originalSenderTag.userId = 16;
    expect(!kitsune::net::encodeSystemEvent(badUser, out).has_value(), "user id above 15");
    auto badRecycle = ackTurn(0);
    badRecycle.header.originalSenderTag.recycle = 4096;
    expect(!kitsune::net::encodeSystemEvent(badRecycle, out).has_value(), "recycle above 4095");

    expect(!kitsune::net::decodeSystemEvent(bytes(kAckTurn, 6)).has_value(), "short ack");
    auto padded = std::array<std::uint8_t, sizeof kAckTurn>{};
    for (std::size_t i = 0; i < sizeof kAckTurn; ++i) {
        padded[i] = kAckTurn[i];
    }
    padded.back() = static_cast<std::uint8_t>(padded.back() | 0x80);
    expect(!kitsune::net::decodeSystemEvent(bytes(padded.data(), padded.size())).has_value(),
           "nonzero padding");

    auto roundTrip = [&](const SystemEvent& message, const char* what) {
        std::vector<std::byte> buffer(4096);
        const auto size = kitsune::net::encodeSystemEvent(message, buffer);
        const auto back =
            size ? kitsune::net::decodeSystemEvent({buffer.data(), *size}) : std::nullopt;
        expect(size.has_value() && back && *back == message, what);
    };

    roundTrip(kitsune::net::SessionSetHostEvent{}, "session set host");
    roundTrip(kitsune::net::SessionUserReadyEvent{}, "session user ready");
    roundTrip(kitsune::net::SessionUserDetachEvent{}, "session user detach");

    constexpr std::uint8_t kMessage[] = {
        0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xa5,
    };
    const auto message = kitsune::net::decodeSystemEvent(bytes(kMessage, sizeof kMessage));
    expect(message && kitsune::net::systemEventId(*message) == SystemEventId::Message,
           "message id");
    if (message) {
        const auto* body = std::get_if<kitsune::net::MessageEvent>(&*message);
        expect(body && body->messageData.size() == 1 && body->messageData[0] == std::byte{0xa5},
               "message blob");
        const auto encoded = kitsune::net::encodeSystemEvent(*message, out);
        expect(encoded && sameBytes({out.data(), *encoded}, kMessage, sizeof kMessage),
               "message bytes");
    }

    kitsune::net::SessionUserTransferEvent transfer;
    transfer.path = std::string(512, 'x');
    std::vector<std::byte> wide(2048);
    const auto wideSize = kitsune::net::encodeSystemEvent(transfer, wide);
    expect(wideSize.has_value(), "string length uses schema bits plus 2");
    if (wideSize) {
        expect(wide[14] == std::byte{0x50} && wide[15] == std::byte{0x00} &&
                   wide[16] == std::byte{'x'},
               "512-byte string length field");
        const auto back = kitsune::net::decodeSystemEvent({wide.data(), *wideSize});
        expect(back && *back == SystemEvent{transfer}, "long path round trip");
    }
    transfer.path = std::string(1041, 'y');
    expect(!kitsune::net::encodeSystemEvent(transfer, wide).has_value(),
           "string above the widened max");

    kitsune::net::ConnectReplyEvent reply;
    reply.header.sequence = 1;
    reply.address.version = 4;
    reply.address.address = {0x007f, 0x0000, 0x0000, 0x0100};
    reply.address.port = 27151;
    reply.errorId = kitsune::net::ErrorId::Success;
    roundTrip(reply, "connect reply");

    kitsune::net::JoinSucceededEvent joined;
    joined.syncRecycle = 9;
    kitsune::net::JoinSucceededEventUsers user;
    user.tag.userId = 1;
    user.info.userData.name = "Kit";
    user.info.userData.scaledRating = -2;
    user.info.externalAddress.port = 27152;
    user.incomingSequence = 4;
    joined.users.push_back(user);
    joined.readyMask = {1, 0};
    kitsune::net::LobbySlot slot;
    slot.control = 2;
    slot.handicap = 100;
    slot.hero = "Zagara";
    kitsune::net::CRewardOverride reward;
    reward.key = 7;
    reward.rewards = {1, 2};
    slot.rewardOverrides.push_back(reward);
    joined.lobbyState.slots.push_back(slot);
    joined.lobbyState.phase = kitsune::net::Phase::Lobby;
    roundTrip(joined, "join succeeded");

    kitsune::net::TurnEvent turn;
    turn.syncValue = 8;
    turn.requestRecycle = true;
    turn.gameEventData = {std::byte{1}, std::byte{2}, std::byte{3}};
    kitsune::net::TurnJoiner joiner;
    joiner.ready = true;
    joiner.info.userData.clanLogo = std::vector<std::byte>(40, std::byte{0x11});
    turn.joiners = std::vector<kitsune::net::TurnJoiner>{joiner};
    turn.readies = std::vector<std::uint8_t>{1, 1, 0};
    roundTrip(turn, "turn");

    kitsune::net::SessionJoinSyncingEvent syncing;
    slot.handicap = 101;
    syncing.lobbyState.slots = {slot};
    expect(!kitsune::net::encodeSystemEvent(syncing, wide).has_value(), "handicap above 100");

    expect(kitsune::net::describeSystemEvent(ackTurn(0)).find("e_ackTurn") != std::string::npos,
           "describe ack turn");
    expect(kitsune::net::describeTcpPayload(bytes(kAckTurn, sizeof kAckTurn)).find("e_ackTurn") !=
               std::string::npos,
           "tcp log names ack turn");
    expect(kitsune::net::describeTcpPayload(bytes(kAckTurn, 4)).find("tcp undecoded bytes=4") !=
               std::string::npos,
           "tcp log names a short read");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
