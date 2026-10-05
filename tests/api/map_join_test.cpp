#include "Kitsune/Api/MapJoin.h"

#include <cstdlib>
#include <iostream>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

} // namespace

int main() {
    using kitsune::api::MapJoin;

    expect(!kitsune::api::roomCheckEnabled(0), "flag clear");
    expect(!kitsune::api::roomCheckEnabled(2), "value 2 is not bit 2");
    expect(kitsune::api::roomCheckEnabled(kitsune::api::kApiJoinRoomCheckBit), "bit 2");

    expect(!kitsune::api::mapJoinFull(0x0200), "limit 2, count 0");
    expect(!kitsune::api::mapJoinFull(0x0201), "limit 2, count 1");
    expect(kitsune::api::mapJoinFull(0x0202), "limit 2, count 2");
    expect(kitsune::api::mapJoinFull(0x0000), "a zero word compares full");

    const auto skipped = kitsune::api::checkMapJoin(0, 0x0202);
    expect(skipped.result == MapJoin::Skipped && skipped.detail.empty(), "pump skips the check");

    const auto open = kitsune::api::checkMapJoin(kitsune::api::kApiJoinRoomCheckBit, 0x0201);
    expect(open.result == MapJoin::Room && open.detail.empty(), "room remains");

    const auto full = kitsune::api::checkMapJoin(kitsune::api::kApiJoinRoomCheckBit, 0x0202);
    expect(full.result == MapJoin::Full && full.detail == kitsune::api::kNoRoomDetail, "no room");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
