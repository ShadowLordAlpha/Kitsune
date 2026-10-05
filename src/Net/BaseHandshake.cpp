#include "Kitsune/Net/BaseHandshake.h"

#include <utility>

namespace kitsune::net {

namespace {

std::vector<std::byte> pack(const SystemEvent& event) {
    std::vector<std::byte> buffer(kStreamBufferBytes);
    const auto size = encodeSystemEvent(event, buffer);
    if (!size) {
        return {};
    }
    buffer.resize(*size);
    return buffer;
}

// An event with the header numbers filled in.
template <typename Event>
Event make(std::uint16_t sequence, std::uint16_t ack, std::uint32_t peer, Event event = {}) {
    event.header.sequence = sequence;
    event.header.ackSequence = ack;
    event.header.originatingPeer = peer;
    return event;
}

// The next sequence a peer expects after one it just received.
std::uint16_t after(std::uint16_t sequence) {
    return static_cast<std::uint16_t>(sequence + 1);
}

} // namespace

NetAddress ipv4Address(const std::array<std::uint8_t, 4>& octets, std::uint16_t port) {
    NetAddress address;
    address.version = 4;
    address.address.assign(8, 0);
    address.address[0] = static_cast<std::uint16_t>(octets[0] << 8 | octets[1]);
    address.address[1] = static_cast<std::uint16_t>(octets[2] << 8 | octets[3]);
    address.port = port;
    return address;
}

HandshakeStep GuestHandshake::start() {
    if (state_ != HandshakeState::Idle) {
        return {{}, state_};
    }
    ConnectEvent connect;
    connect.programId = params_.programId;
    connect.version = params_.version;
    connect.sessionId = params_.sessionId;
    connect.sessionSecret = params_.hostSecret;
    connect.listenPort = params_.listenPort;
    state_ = HandshakeState::AwaitReply;
    return {pack(make(0, 0, params_.peerId, std::move(connect))), state_};
}

HandshakeStep GuestHandshake::onMessage(const SystemEvent& message) {
    const auto& received = systemHeader(message);
    switch (systemEventId(message)) {
    case SystemEventId::ConnectReply: {
        const auto* reply = std::get_if<ConnectReplyEvent>(&message);
        if (state_ != HandshakeState::AwaitReply || !reply) {
            break;
        }
        if (reply->errorId != ErrorId::Success) {
            state_ = HandshakeState::Failed;
            return {{}, state_};
        }
        JoinEvent join;
        join.userInfo.sessionId = params_.sessionId;
        join.userInfo.peerId = params_.peerId;
        join.userInfo.sessionSecret = params_.secret;
        join.userInfo.externalAddress = reply->address; // as the host saw us
        join.userInfo.userData = params_.user;
        join.ready = true;
        state_ = HandshakeState::AwaitJoined;
        return {pack(make(1, after(received.sequence), params_.peerId, std::move(join))), state_};
    }
    case SystemEventId::JoinSucceeded: {
        const auto* joined = std::get_if<JoinSucceededEvent>(&message);
        if (state_ != HandshakeState::AwaitJoined || !joined) {
            break;
        }
        joined_ = *joined;
        state_ = HandshakeState::Joined;
        // An ack carries the next expected sequence in both fields.
        const auto next = after(received.sequence);
        return {pack(make<AckSystemEvent>(next, next, params_.peerId)), state_};
    }
    case SystemEventId::JoinFailed:
        if (state_ == HandshakeState::AwaitJoined) {
            state_ = HandshakeState::Failed;
            return {{}, state_};
        }
        break;
    default:
        break;
    }
    return {{}, state_};
}

HandshakeStep HostHandshake::onMessage(const SystemEvent& message,
                                       const std::array<std::uint8_t, 4>& remote) {
    const auto& received = systemHeader(message);
    switch (systemEventId(message)) {
    case SystemEventId::Connect: {
        const auto* connect = std::get_if<ConnectEvent>(&message);
        if (state_ != HandshakeState::AwaitConnect || !connect) {
            break;
        }
        if (connect->programId != params_.programId ||
            connect->version.build != params_.version.build ||
            connect->sessionId != params_.sessionId ||
            connect->sessionSecret != params_.sessionSecret) {
            state_ = HandshakeState::Failed;
            return {{}, state_};
        }
        ConnectReplyEvent reply;
        reply.address = ipv4Address(remote, connect->listenPort);
        state_ = HandshakeState::AwaitJoin;
        return {pack(make(nextSequence_++, after(received.sequence), params_.peerId, std::move(reply))), state_};
    }
    case SystemEventId::Join: {
        const auto* join = std::get_if<JoinEvent>(&message);
        if (state_ != HandshakeState::AwaitJoin || !join) {
            break;
        }
        if (join->userInfo.sessionId != params_.sessionId) {
            state_ = HandshakeState::Failed;
            return {{}, state_};
        }
        guest_ = join->userInfo;
        state_ = HandshakeState::Joined;
        return {pack(make(nextSequence_++, after(received.sequence), params_.peerId, params_.joined)), state_};
    }
    default:
        break;
    }
    return {{}, state_};
}

} // namespace kitsune::net
