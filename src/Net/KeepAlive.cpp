#include "Kitsune/Net/KeepAlive.h"

namespace kitsune::net {

std::vector<std::byte> answerPing(const SystemEvent& message, std::uint32_t peerId) {
    const auto* ping = std::get_if<PingEvent>(&message);
    if (ping == nullptr) {
        return {};
    }
    PingReplyEvent reply;
    reply.header.sequence = ping->header.ackSequence;
    reply.header.ackSequence = static_cast<std::uint16_t>(ping->header.sequence + 1);
    reply.header.originatingPeer = peerId;
    std::vector<std::byte> buffer(kStreamBufferBytes);
    const auto size = encodeSystemEvent(reply, buffer);
    if (!size) {
        return {};
    }
    buffer.resize(*size);
    return buffer;
}

} // namespace kitsune::net
