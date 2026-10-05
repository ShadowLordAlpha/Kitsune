#pragma once

// The message channel (docs/tcp-base.md, "Message channel"). e_message carries
// an opaque blob and has its own sequence. The receiver answers each one with
// e_ackMessage, whose sequence and ack both name the next message expected,
// like the system channel's ack. An official peer repeats a message until it
// is acknowledged, and in the captures it stops sending turns while one is open.

#include "Kitsune/Net/SystemEvent.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kitsune::net {

class MessageChannel {
public:
    explicit MessageChannel(std::uint32_t peerId) : peerId_(peerId) {}

    // One record from the other side. Returns the records to send in answer.
    // A repeat is answered again, because our earlier ack was lost. A message
    // past the next expected one is ignored, and the repeat will fill the gap.
    [[nodiscard]] std::vector<std::vector<std::byte>> onMessage(const SystemEvent& message);

    // An e_message of our own: our next sequence, and the next one we expect from them.
    [[nodiscard]] std::vector<std::byte> send(std::vector<std::byte> blob);

    [[nodiscard]] std::uint16_t nextExpected() const noexcept { return expected_; }
    [[nodiscard]] std::uint16_t nextToSend() const noexcept { return next_; }
    [[nodiscard]] const std::vector<std::vector<std::byte>>& received() const noexcept { return received_; }

private:
    std::uint32_t peerId_ = 0;
    std::uint16_t expected_ = 0;
    std::uint16_t next_ = 0;
    std::vector<std::vector<std::byte>> received_;
};

} // namespace kitsune::net
