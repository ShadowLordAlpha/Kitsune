#include "Kitsune/Net/MessageChannel.h"

#include <utility>

namespace kitsune::net {

std::vector<std::byte> MessageChannel::send(std::vector<std::byte> blob) {
    MessageEvent message;
    message.header.sequence = next_;
    message.header.ackSequence = expected_;
    message.header.originatingPeer = peerId_;
    message.messageData = std::move(blob);
    std::vector<std::byte> buffer(kStreamBufferBytes);
    const auto size = encodeSystemEvent(message, buffer);
    if (!size) {
        return {};
    }
    ++next_;
    buffer.resize(*size);
    return buffer;
}

std::vector<std::vector<std::byte>> MessageChannel::onMessage(const SystemEvent& message) {
    std::vector<std::vector<std::byte>> out;
    const auto* body = std::get_if<MessageEvent>(&message);
    if (!body) {
        return out;
    }
    const auto sequence = body->header.sequence;
    if (sequence == expected_) {
        received_.push_back(body->messageData);
        ++expected_;
    } else if (static_cast<std::int16_t>(sequence - expected_) > 0) {
        return out; // a gap
    }

    AckMessageEvent ack;
    ack.header.sequence = expected_;
    ack.header.ackSequence = expected_;
    ack.header.originatingPeer = peerId_;
    std::vector<std::byte> buffer(kStreamBufferBytes);
    if (const auto size = encodeSystemEvent(ack, buffer)) {
        buffer.resize(*size);
        out.push_back(std::move(buffer));
    }
    return out;
}

} // namespace kitsune::net
