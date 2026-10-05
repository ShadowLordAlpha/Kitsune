#include "../fixtures/base_port_capture.h"
#include "Kitsune/Net/KeepAlive.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

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

} // namespace

int main() {
    const auto reply = decode(fx::kGuestPingReply);
    const auto answer = answerPing(decode(fx::kHostPing), systemHeader(reply).originatingPeer);
    expect(answer.size() == sizeof fx::kGuestPingReply &&
               std::memcmp(answer.data(), fx::kGuestPingReply, sizeof fx::kGuestPingReply) == 0,
           "the reply is the official guest's, byte for byte");
    expect(answerPing(decode(fx::kGuestPingReply), 1).empty(), "only a ping is answered");
    if (gFailures == 0) {
        std::cout << "keep_alive ok\n";
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
