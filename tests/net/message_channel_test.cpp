#include "../fixtures/base_port_capture.h"
#include "Kitsune/Net/MessageChannel.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
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
SystemEvent decode(const std::uint8_t (&data)[N]) {
    auto message = decodeSystemEvent({reinterpret_cast<const std::byte*>(data), N});
    expect(message.has_value(), "fixture decodes");
    return message.value_or(SystemEvent{});
}

void acksInOrder() {
    const auto official = decode(fx::kGuestAckMessage);
    MessageChannel channel(systemHeader(official).originatingPeer);
    const auto message = decode(fx::kHostMessage);

    auto out = channel.onMessage(message);
    expect(out.size() == 1, "a message is acknowledged");
    if (out.size() == 1) {
        const auto ack = decodeSystemEvent(out[0]);
        const auto* ackBody = ack ? std::get_if<AckMessageEvent>(&*ack) : nullptr;
        expect(ackBody && ackBody->header.sequence == 1 && ackBody->header.ackSequence == 1,
               "the ack names the next message");
        expect(out[0].size() == sizeof fx::kGuestAckMessage &&
                   std::memcmp(out[0].data(), fx::kGuestAckMessage, sizeof fx::kGuestAckMessage) ==
                       0,
               "the ack is the official guest's, byte for byte");
    }
    expect(channel.received().size() == 1 && channel.received()[0].size() == 5, "the blob is kept");
    expect(channel.nextExpected() == 1, "next expected advances");

    // The host repeats message 0 when our ack is lost: ack again, keep one copy.
    out = channel.onMessage(message);
    expect(out.size() == 1 && channel.received().size() == 1,
           "a repeat is acked and not kept twice");

    // A gap is ignored.
    auto gap = message;
    systemHeader(gap).sequence = 5;
    expect(channel.onMessage(gap).empty() && channel.received().size() == 1, "a gap is ignored");

    expect(channel.onMessage(decode(fx::kAckSystem)).empty(), "a non-message is ignored");
}

void sendsOwnMessages() {
    const auto official = decode(fx::kGuestMessage);
    MessageChannel channel(systemHeader(official).originatingPeer);
    const auto& body = std::get<MessageEvent>(official);
    const auto sent = channel.send(body.messageData);
    expect(sent.size() == sizeof fx::kGuestMessage &&
               std::memcmp(sent.data(), fx::kGuestMessage, sizeof fx::kGuestMessage) == 0,
           "our first message is the official guest's, byte for byte");
    expect(channel.nextToSend() == 1, "our sequence advances");

    // Once theirs has arrived, ours names it in ack.
    (void)channel.onMessage(decode(fx::kHostMessage));
    const auto second = decodeSystemEvent(channel.send(body.messageData));
    expect(second && systemHeader(*second).sequence == 1 && systemHeader(*second).ackSequence == 1,
           "the second message numbers itself 1 and acks 1");
}

} // namespace

int main() {
    acksInOrder();
    sendsOwnMessages();
    if (gFailures == 0) {
        std::cout << "message_channel ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
