#pragma once

// One line per System event, for the log of a live run.

#include <cstddef>
#include <span>
#include <string>

#include "Kitsune/Net/SystemEvent.h"

namespace kitsune::net {

[[nodiscard]] std::string describeSystemEvent(const SystemEvent& event);

// Decoded system event, or "tcp undecoded bytes=N" when the span is not one record.
[[nodiscard]] std::string describeTcpPayload(std::span<const std::byte> bytes);

} // namespace kitsune::net
