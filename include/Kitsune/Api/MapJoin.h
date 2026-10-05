#pragma once

#include <cstdint>
#include <string_view>

namespace kitsune::api {

// Bit 2 of g_apiJoinFlags. ApiJoin_pump calls the room check only when it is set.
inline constexpr std::uint32_t kApiJoinRoomCheckBit = 1u << 2;

inline constexpr std::string_view kNoRoomDetail =
    "There is no room for another user in this game. This may not be a multiplayer map";

enum class MapJoin {
    Skipped,
    Room,
    Full,
};

struct MapJoinDecision {
    MapJoin result = MapJoin::Skipped;
    std::string_view detail{};
};

// The ushort at the map record +0x24. Full when its high byte is <= its low byte.
[[nodiscard]] constexpr bool mapJoinFull(std::uint16_t slotsWord) noexcept {
    const auto low = static_cast<std::uint8_t>(slotsWord);
    const auto high = static_cast<std::uint8_t>(slotsWord >> 8);
    return high <= low;
}

[[nodiscard]] constexpr bool roomCheckEnabled(std::uint32_t apiJoinFlags) noexcept {
    return (apiJoinFlags & kApiJoinRoomCheckBit) != 0;
}

// Runs the room check when bit 2 is set. A clear bit skips it.
[[nodiscard]] MapJoinDecision checkMapJoin(std::uint32_t apiJoinFlags, std::uint16_t slotsWord);

} // namespace kitsune::api
