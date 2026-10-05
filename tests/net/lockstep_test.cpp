#include "Kitsune/Net/LocalEvent.h"
#include "Kitsune/Net/Lockstep.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <variant>
#include <vector>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

// Captured from the Linux Base75689 stub that reached "Game has started".
constexpr std::uint8_t kIHave[] = {
    0x01, 0x00, 0x05, 0x33, 0x02, 0x01, 0x04, 0x0a, 0x00, 0x00, 0x12, 0x7a,
    0x09, 0x00, 0x12, 0x7a, 0x09, 0x00, 0x00, 0x00, 0x01, 0x09, 0x60,
};
constexpr std::uint8_t kShowMe[] = {
    0x02, 0x00, 0x05, 0x33, 0x02, 0x01, 0x04, 0x0a, 0x00, 0x00, 0x12,
    0x7a, 0x09, 0x00, 0x12, 0x7a, 0x09, 0x00, 0x00, 0x00, 0x01,
};
constexpr std::uint8_t kIDontHave[] = {
    0x03, 0x00, 0x05, 0x33, 0x02, 0x01, 0x04, 0x0a, 0x00, 0x00, 0x12,
    0x7a, 0x09, 0x00, 0x12, 0x7a, 0x09, 0x00, 0x00, 0x00, 0x01,
};
// Same session as kIHave. The two counters that differ are maxUsers and maxObservers.
constexpr std::uint8_t kIHaveLive[] = {
    0x01, 0x00, 0x05, 0x33, 0x02, 0x01, 0x04, 0x0a, 0x00, 0x00, 0x12, 0x7a,
    0x09, 0x00, 0x12, 0x7a, 0x09, 0x00, 0x00, 0x00, 0x01, 0x05, 0x70,
};

std::span<const std::byte> bytes(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const std::byte*>(data), size};
}

bool sameBytes(std::span<const std::byte> got, const std::uint8_t* want, std::size_t size) {
    if (got.size() != size) {
        return false;
    }
    for (std::size_t i = 0; i < size; ++i) {
        if (got[i] != static_cast<std::byte>(want[i])) {
            return false;
        }
    }
    return true;
}

kitsune::net::IHaveThisGameEvent fixtureHave(std::uint8_t maxUsers, std::uint8_t maxObservers) {
    kitsune::net::IHaveThisGameEvent event;
    event.header.programId = 0x5332;
    event.header.version.flags = 1;
    event.header.version.major = 4;
    event.header.version.minor = 10;
    event.header.version.revision = 0;
    event.header.version.build = 75689;
    event.header.version.baseBuild = 75689;
    event.sessionId = 1;
    event.numUsers = 1;
    event.maxUsers = maxUsers;
    event.numObservers = 0;
    event.maxObservers = maxObservers;
    return event;
}

} // namespace

int main() {
    using kitsune::net::DoesAnyoneHaveAGameEvent;
    using kitsune::net::HereIsThisGameEvent;
    using kitsune::net::IDontHaveThisGameEvent;
    using kitsune::net::IHaveThisGameEvent;
    using kitsune::net::LocalEventId;
    using kitsune::net::ShowMeThisGameEvent;

    const auto haveEvent = kitsune::net::decodeLocalEvent(bytes(kIHave, sizeof kIHave));
    expect(haveEvent && kitsune::net::localEventId(*haveEvent) == LocalEventId::IHaveThisGame,
           "decode IHave");
    const auto* have = haveEvent ? std::get_if<IHaveThisGameEvent>(&*haveEvent) : nullptr;
    if (have) {
        expect(have->header.programId == 0x5332, "program id");
        expect(have->header.version.major == 4 && have->header.version.minor == 10, "version");
        expect(have->header.version.revision == 0 && have->header.version.flags == 1, "revision");
        expect(have->header.version.build == 75689 && have->header.version.baseBuild == 75689,
               "builds");
        expect(have->sessionId == 1, "session");
        expect(have->numUsers == 1 && have->maxUsers == 4, "user counts");
        expect(have->numObservers == 0 && have->maxObservers == 12, "observer counts");
    }

    const auto liveEvent = kitsune::net::decodeLocalEvent(bytes(kIHaveLive, sizeof kIHaveLive));
    const auto* live = liveEvent ? std::get_if<IHaveThisGameEvent>(&*liveEvent) : nullptr;
    expect(live && live->maxUsers == 2 && live->maxObservers == 14, "live counters");
    if (haveEvent && liveEvent) {
        expect(kitsune::net::sameSession(*haveEvent, *liveEvent), "live hello is the same session");
    }

    const auto show = kitsune::net::decodeLocalEvent(bytes(kShowMe, sizeof kShowMe));
    const auto dont = kitsune::net::decodeLocalEvent(bytes(kIDontHave, sizeof kIDontHave));
    expect(show && std::holds_alternative<ShowMeThisGameEvent>(*show), "decode ShowMe");
    expect(dont && std::holds_alternative<IDontHaveThisGameEvent>(*dont), "decode IDontHave");
    if (haveEvent && show && dont) {
        expect(kitsune::net::sameSession(*haveEvent, *show) &&
                   kitsune::net::sameSession(*show, *dont),
               "shared session");
        expect(kitsune::net::localSessionId(*show) == 1, "session events carry the session");
    }

    std::array<std::byte, 32> out{};
    const auto built = fixtureHave(4, 12);
    const auto builtSize = kitsune::net::encodeLocalEvent(built, out);
    expect(builtSize == sizeof kIHave && sameBytes({out.data(), *builtSize}, kIHave, sizeof kIHave),
           "named fields encode the fixture");

    const auto liveBuilt = fixtureHave(2, 14);
    const auto liveSize = kitsune::net::encodeLocalEvent(liveBuilt, out);
    expect(liveSize == sizeof kIHaveLive &&
               sameBytes({out.data(), *liveSize}, kIHaveLive, sizeof kIHaveLive),
           "named fields encode the live tail");

    const auto again = haveEvent ? kitsune::net::encodeLocalEvent(*haveEvent, out) : std::nullopt;
    expect(again == sizeof kIHave && sameBytes({out.data(), *again}, kIHave, sizeof kIHave),
           "retransmitted IHave is the same datagram");

    DoesAnyoneHaveAGameEvent does;
    does.header = built.header;
    const auto doesSize = kitsune::net::encodeLocalEvent(does, out);
    expect(doesSize == kitsune::net::kDoesAnyoneBytes, "DoesAnyone is 17 bytes");
    const auto doesBack =
        doesSize ? kitsune::net::decodeLocalEvent({out.data(), *doesSize}) : std::nullopt;
    expect(doesBack && kitsune::net::localEventId(*doesBack) == LocalEventId::DoesAnyoneHaveAGame &&
               kitsune::net::localHeader(*doesBack).programId == 0x5332 &&
               kitsune::net::localHeader(*doesBack).version.build == 75689 &&
               kitsune::net::localSessionId(*doesBack) == 0,
           "DoesAnyone round trip");

    auto tooMany = built;
    tooMany.maxUsers = 17;
    expect(!kitsune::net::encodeLocalEvent(tooMany, out).has_value(), "count above 16 is rejected");

    HereIsThisGameEvent here;
    here.header = built.header;
    here.gameDescription.mapFileName = "Map.SC2Map";
    here.gameDescription.maxTeams = 2;
    here.gameDescription.maxRaces = 3;
    expect(!kitsune::net::encodeLocalEvent(here, out).has_value(),
           "HereIs does not fit the 32-byte buffer");

    kitsune::net::GuestLockstep guestSide;
    const auto guest = guestSide.onDatagram(bytes(kIHave, sizeof kIHave));
    expect(guest.has_value(), "guest answers IHave");
    const auto guestSize = guest ? kitsune::net::encodeLocalEvent(*guest, out) : std::nullopt;
    expect(guestSize == sizeof kShowMe &&
               sameBytes({out.data(), *guestSize}, kShowMe, sizeof kShowMe),
           "ShowMe drops the counters");

    expect(!guestSide.onDatagram({}).has_value(), "guest is silent with no datagram");
    expect(!guestSide.onDatagram(bytes(kShowMe, sizeof kShowMe)).has_value(),
           "guest does not answer ShowMe");

    kitsune::net::HostLockstep hostSide;
    expect(!hostSide.onDatagram(bytes(kShowMe, sizeof kShowMe)).has_value(),
           "ShowMe is not answered with IDontHave");
    expect(!hostSide.onDatagram(bytes(kIHave, sizeof kIHave)).has_value(),
           "IHave does not produce IDontHave");

    expect(!kitsune::net::decodeLocalEvent(bytes(kIHave, sizeof kIHave - 2)).has_value(),
           "a 21-byte IHave is rejected");
    expect(!kitsune::net::decodeLocalEvent(bytes(kShowMe, 4)).has_value(),
           "a short ShowMe is rejected");

    auto padded = std::array<std::uint8_t, sizeof kShowMe>{};
    for (std::size_t i = 0; i < sizeof kShowMe; ++i) {
        padded[i] = kShowMe[i];
    }
    padded.back() = static_cast<std::uint8_t>(padded.back() | 0x80);
    expect(!kitsune::net::decodeLocalEvent(bytes(padded.data(), padded.size())).has_value(),
           "nonzero padding is rejected");

    std::array<std::byte, 4> tiny{};
    expect(!kitsune::net::encodeLocalEvent(built, tiny).has_value(), "short buffer");

    kitsune::net::HereIsThisGameEvent hereIs;
    hereIs.header = built.header;
    hereIs.sessionId = 1;
    hereIs.sessionSecret = 0x42;
    hereIs.sessionAddress.version = 4;
    hereIs.sessionAddress.address = {0x007f, 0x0001};
    hereIs.sessionAddress.port = 27150;
    hereIs.creatorName = "Kit";
    hereIs.gameDescription.mapFileName = "Map.SC2Map";
    hereIs.gameDescription.mapAuthorName = "Author";
    hereIs.gameDescription.maxTeams = 2;
    hereIs.gameDescription.maxRaces = 3;
    hereIs.gameDescription.gameOptions.clientDebugFlags = 0xffffffffffffffffull;
    kitsune::net::SlotDescription slot;
    slot.allowedRaces = {1, 0, 1};
    hereIs.gameDescription.slotDescriptions.push_back(slot);
    const std::vector<std::byte> handle(40, std::byte{0x5a});
    hereIs.gameDescription.cacheHandles.push_back(handle);
    hereIs.numUsers = 1;
    hereIs.maxUsers = 2;
    kitsune::net::UserInitialData player;
    player.name = "Kit";
    player.clanLogo = handle;
    player.scaledRating = -1;
    hereIs.userInitialData.push_back(player);
    std::vector<std::byte> hereBuf(8192);
    const auto hereSize = kitsune::net::encodeHereIs(hereIs, hereBuf);
    expect(hereSize.has_value() && hereBuf[0] == std::byte{0x04}, "here-is starts with event 4");
    const auto hereBack =
        hereSize ? kitsune::net::decodeHereIs({hereBuf.data(), *hereSize}) : std::nullopt;
    expect(hereBack && *hereBack == hereIs, "here-is round trip");
    const auto hereEvent =
        kitsune::net::decodeLocalEvent({hereBuf.data(), hereSize ? *hereSize : 0});
    expect(hereEvent && std::holds_alternative<HereIsThisGameEvent>(*hereEvent) &&
               kitsune::net::localEventId(*hereEvent) == LocalEventId::HereIsThisGame,
           "the local decoder returns here-is as event 4");
    hereIs.gameDescription.maxTeams = 0;
    expect(!kitsune::net::encodeHereIs(hereIs, hereBuf).has_value(), "maxTeams below 1");
    expect(kitsune::net::describeLocalEvent(built).find("e_IHaveThisGame") != std::string::npos,
           "describe IHave");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
