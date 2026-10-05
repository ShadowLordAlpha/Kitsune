#include "Kitsune/Net/PeerSession.h"

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
    kitsune::net::PeerSession session;
    int gameTries = 0;
    const auto game = session.bindGame(27150, [&](std::uint16_t port) {
        ++gameTries;
        return port == 27150;
    });
    expect(game == 27150 && gameTries == 1, "game channel binds one local port");

    const auto again = session.bindGame(27150, [&](std::uint16_t) {
        ++gameTries;
        return true;
    });
    expect(again == 27150 && gameTries == 1, "a second tick does not rebind");

    int baseTries = 0;
    const auto base = session.bindBase(27150, [&](std::uint16_t) {
        ++baseTries;
        return false;
    });
    expect(!base.has_value() && baseTries == 1, "base channel can fail its one port");
    const auto baseRetry = session.bindBase(27150, [&](std::uint16_t port) {
        ++baseTries;
        return port == 27150;
    });
    expect(baseRetry == 27150 && baseTries == 2, "a failed bind is retried on the next tick");

    expect(session.lane().store(4), "lane stores while the peer count is 0");
    expect(session.lane().target() == 4, "sends use the first connection");
    expect(!session.lane().store(5) && session.lane().target() == 4, "the first connection stays");
    session.lane().setPeerCount(1);
    expect(!session.lane().store(9), "a non-zero peer count refuses a store");

    session.start().begin(2);
    expect(session.start().open() && !session.start().singleSlot(),
           "two slots still open the gate");
    session.start().begin(1);
    expect(session.start().open() && session.start().singleSlot(),
           "one slot sets the single-slot byte");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
