#include "Kitsune/Api/MapJoin.h"

namespace kitsune::api {

MapJoinDecision checkMapJoin(std::uint32_t apiJoinFlags, std::uint16_t slotsWord) {
    // Linux Base75689 ApiJoin_pump (0x01052ba0) calls MapJoin_roomCheck
    // (0x013a3ac0) when bit 2 of g_apiJoinFlags is set.
    if (!roomCheckEnabled(apiJoinFlags)) {
        return MapJoinDecision{};
    }
    if (!mapJoinFull(slotsWord)) {
        return MapJoinDecision{MapJoin::Room, {}};
    }
    return MapJoinDecision{MapJoin::Full, kNoRoomDetail};
}

} // namespace kitsune::api
