#include "../fixtures/base_port_capture.h"
#include "Kitsune/Net/BaseHandshake.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <variant>

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
std::span<const std::byte> view(const std::uint8_t (&data)[N]) {
    return {reinterpret_cast<const std::byte*>(data), N};
}

template <std::size_t N>
SystemEvent decode(const std::uint8_t (&data)[N]) {
    auto message = decodeSystemEvent(view(data));
    expect(message.has_value(), "fixture decodes");
    return message.value_or(SystemEvent{});
}

template <std::size_t N>
bool sameBytes(const std::vector<std::byte>& got, const std::uint8_t (&want)[N]) {
    return got.size() == N && std::memcmp(got.data(), want, N) == 0;
}

constexpr std::array<std::uint8_t, 4> kLoopback{127, 0, 0, 1};

// The values the official guest used, read back out of its own records.
GuestParams guestParams() {
    const auto connect = decode(fx::kConnect);
    const auto join = decode(fx::kJoin);
    const auto& c = std::get<ConnectEvent>(connect);
    const auto& j = std::get<JoinEvent>(join);
    GuestParams p;
    p.peerId = c.header.originatingPeer;
    p.secret = j.userInfo.sessionSecret;
    p.sessionId = c.sessionId;
    p.hostSecret = c.sessionSecret;
    p.listenPort = c.listenPort;
    p.programId = c.programId;
    p.version = c.version;
    p.user = j.userInfo.userData;
    return p;
}

// The values the official host used.
HostParams hostParams() {
    const auto connect = decode(fx::kConnect);
    const auto reply = decode(fx::kConnectReply);
    const auto joined = decode(fx::kJoinSucceeded);
    const auto& c = std::get<ConnectEvent>(connect);
    HostParams p;
    p.peerId = systemHeader(reply).originatingPeer;
    p.sessionId = c.sessionId;
    p.sessionSecret = c.sessionSecret;
    p.programId = c.programId;
    p.version = c.version;
    p.joined = std::get<JoinSucceededEvent>(joined);
    return p;
}

void guestMatchesOfficial() {
    GuestHandshake guest(guestParams());
    expect(guest.state() == HandshakeState::Idle, "guest idle");

    auto step = guest.start();
    expect(sameBytes(step.out, fx::kConnect), "guest connect bytes");
    expect(step.state == HandshakeState::AwaitReply, "guest awaits reply");

    step = guest.onMessage(decode(fx::kConnectReply));
    expect(sameBytes(step.out, fx::kJoin), "guest join bytes");
    expect(step.state == HandshakeState::AwaitJoined, "guest awaits joined");

    step = guest.onMessage(decode(fx::kJoinSucceeded));
    expect(sameBytes(step.out, fx::kAckSystem), "guest ack bytes");
    expect(step.state == HandshakeState::Joined, "guest joined");
    expect(guest.joined() && guest.joined()->users.size() == 1, "guest keeps lobby");
}

void hostMatchesOfficial() {
    HostHandshake host(hostParams());
    expect(host.state() == HandshakeState::AwaitConnect, "host waits");

    auto step = host.onMessage(decode(fx::kConnect), kLoopback);
    expect(sameBytes(step.out, fx::kConnectReply), "host reply bytes");
    expect(step.state == HandshakeState::AwaitJoin, "host awaits join");

    step = host.onMessage(decode(fx::kJoin), kLoopback);
    expect(sameBytes(step.out, fx::kJoinSucceeded), "host joined bytes");
    expect(step.state == HandshakeState::Joined, "host joined");
    expect(host.guest() && host.guest()->peerId == systemHeader(decode(fx::kJoin)).originatingPeer,
           "host keeps guest info");
}

void ourOwnPairCompletes() {
    GuestHandshake guest(guestParams());
    HostHandshake host(hostParams());

    auto fromGuest = guest.start();
    auto fromHost = host.onMessage(*decodeSystemEvent(fromGuest.out), kLoopback);
    fromGuest = guest.onMessage(*decodeSystemEvent(fromHost.out));
    fromHost = host.onMessage(*decodeSystemEvent(fromGuest.out), kLoopback);
    fromGuest = guest.onMessage(*decodeSystemEvent(fromHost.out));
    expect(guest.state() == HandshakeState::Joined && host.state() == HandshakeState::Joined,
           "pair reaches joined");
}

void refusals() {
    // The guest named the wrong host secret.
    auto wrong = hostParams();
    wrong.sessionSecret += 1;
    HostHandshake host(wrong);
    auto step = host.onMessage(decode(fx::kConnect), kLoopback);
    expect(step.out.empty() && step.state == HandshakeState::Failed, "host refuses a bad secret");

    auto otherSession = hostParams();
    otherSession.sessionId += 1;
    HostHandshake host2(otherSession);
    step = host2.onMessage(decode(fx::kConnect), kLoopback);
    expect(step.out.empty() && step.state == HandshakeState::Failed,
           "host refuses another session");

    // A join before a connect is not accepted.
    HostHandshake early(hostParams());
    step = early.onMessage(decode(fx::kJoin), kLoopback);
    expect(step.out.empty() && step.state == HandshakeState::AwaitConnect,
           "host ignores early join");

    // The host's reply carries an error.
    GuestHandshake guest(guestParams());
    (void)guest.start();
    auto bad = decode(fx::kConnectReply);
    std::get<ConnectReplyEvent>(bad).errorId = ErrorId::Disconnected;
    step = guest.onMessage(bad);
    expect(step.out.empty() && step.state == HandshakeState::Failed,
           "guest fails on an error reply");

    // The host refuses the join.
    GuestHandshake refused(guestParams());
    (void)refused.start();
    (void)refused.onMessage(decode(fx::kConnectReply));
    JoinFailedEvent failed;
    failed.errorId = ErrorId::GameIsFull;
    step = refused.onMessage(failed);
    expect(step.out.empty() && step.state == HandshakeState::Failed, "guest fails on join failed");

    // Records out of order are ignored, not answered.
    GuestHandshake idle(guestParams());
    step = idle.onMessage(decode(fx::kJoinSucceeded));
    expect(step.out.empty() && step.state == HandshakeState::Idle, "guest ignores a stray join");
}

void address() {
    const auto a = ipv4Address({127, 0, 0, 1}, 27153);
    expect(a.version == 4 && a.port == 27153 && a.address.size() == 8 && a.address[0] == 0x7f00 &&
               a.address[1] == 0x0001 && a.address[2] == 0,
           "ipv4 address words");
}

} // namespace

int main() {
    guestMatchesOfficial();
    hostMatchesOfficial();
    ourOwnPairCompletes();
    refusals();
    address();
    if (gFailures == 0) {
        std::cout << "base_handshake ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
