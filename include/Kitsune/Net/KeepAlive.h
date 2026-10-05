#pragma once

// e_ping and e_pingReply (docs/tcp-base.md, "Keepalive"). When no turn is
// flowing the official peers ping each other and expect a reply. The reply
// numbers itself with the ping's ack and acks the ping's sequence plus one.

#include "Kitsune/Net/SystemEvent.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kitsune::net {

// The e_pingReply for `message`, or nothing when it is not a ping.
[[nodiscard]] std::vector<std::byte> answerPing(const SystemEvent& message, std::uint32_t peerId);

} // namespace kitsune::net
