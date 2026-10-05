#pragma once

// Helpers around the generated LocalEvent (docs/udp-join.md): which datagrams
// belong to one session, a line for the log, and the two small state machines
// of the game-port join.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "Kitsune/Net/LocalEvent.h"

namespace kitsune::net {

inline constexpr std::size_t kDoesAnyoneBytes = 17;
inline constexpr std::size_t kIHaveBytes = 23;
inline constexpr std::size_t kSessionEventBytes = 21;

// Zero for DoesAnyoneHaveAGame, which names no session.
[[nodiscard]] std::uint32_t localSessionId(const LocalEvent& event) noexcept;

// The same program, version, and session.
[[nodiscard]] bool sameSession(const LocalEvent& left, const LocalEvent& right) noexcept;

[[nodiscard]] std::string describeLocalEvent(const LocalEvent& event);

// The here-is alone, for callers that already know what they hold.
[[nodiscard]] std::optional<std::size_t> encodeHereIs(const HereIsThisGameEvent& event,
                                                      std::span<std::byte> out);
[[nodiscard]] std::optional<HereIsThisGameEvent> decodeHereIs(std::span<const std::byte> datagram);
[[nodiscard]] std::string describeHereIs(const HereIsThisGameEvent& event);

// A guest sends nothing until IHaveThisGame, then answers with ShowMeThisGame.
class GuestLockstep {
public:
    [[nodiscard]] std::optional<ShowMeThisGameEvent> onDatagram(std::span<const std::byte> datagram) const;
};

// ShowMe is not answered with another small local event. The join sends HereIs.
class HostLockstep {
public:
    [[nodiscard]] std::optional<LocalEvent> onDatagram(std::span<const std::byte> datagram) const;
};

} // namespace kitsune::net
