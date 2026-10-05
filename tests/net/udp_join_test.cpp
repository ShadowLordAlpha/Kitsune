#include "Kitsune/Net/LocalEvent.h"
#include "Kitsune/Net/Lockstep.h"
#include "Kitsune/Net/UdpJoin.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

constexpr std::uint8_t kIHave[] = {
    0x01, 0x00, 0x05, 0x33, 0x02, 0x01, 0x04, 0x0a, 0x00, 0x00, 0x12, 0x7a,
    0x09, 0x00, 0x12, 0x7a, 0x09, 0x00, 0x00, 0x00, 0x01, 0x09, 0x60,
};
constexpr std::uint8_t kShowMe[] = {
    0x02, 0x00, 0x05, 0x33, 0x02, 0x01, 0x04, 0x0a, 0x00, 0x00, 0x12,
    0x7a, 0x09, 0x00, 0x12, 0x7a, 0x09, 0x00, 0x00, 0x00, 0x01,
};
bool same(const std::vector<std::byte>& got, const std::uint8_t* want, std::size_t size) {
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

} // namespace

int main() {
    using kitsune::net::UdpJoin;
    using kitsune::net::UdpJoinRole;
    using kitsune::net::UdpJoinStatus;

    const auto decodedHave =
        kitsune::net::decodeLocalEvent({reinterpret_cast<const std::byte*>(kIHave), sizeof kIHave});
    std::optional<kitsune::net::IHaveThisGameEvent> have;
    if (decodedHave) {
        if (const auto* event = std::get_if<kitsune::net::IHaveThisGameEvent>(&*decodedHave)) {
            have = *event;
        }
    }
    expect(have.has_value(), "fixture IHave decodes");
    if (!have) {
        return EXIT_FAILURE;
    }

    UdpJoin host(UdpJoinRole::Host);
    UdpJoin guest(UdpJoinRole::Guest);
    expect(host.bind(0), "host bind");
    expect(guest.bind(0), "guest bind");
    host.useIHave(*have);
    std::mutex logMu;
    std::string hostLog;
    host.setLog([&](std::string_view line) {
        std::lock_guard lock(logMu);
        hostLog.append(line);
        hostLog.push_back('\n');
    });
    const auto guestPeer = kitsune::net::udpLoopback(guest.localPort());

    UdpJoinStatus guestStatus = UdpJoinStatus::TimedOut;
    std::thread guestThread(
        [&] { guestStatus = guest.exchange(nullptr, std::chrono::seconds(2)); });
    const auto hostStatus = host.exchange(&guestPeer, std::chrono::seconds(2));
    guestThread.join();

    expect(hostStatus == UdpJoinStatus::Completed, "host completed");
    expect(guestStatus == UdpJoinStatus::Completed, "guest completed");
    expect(guest.inbound().size() >= 2, "guest saw IHave and HereIs");
    expect(!host.inbound().empty(), "host saw ShowMe");
    if (guest.inbound().size() >= 2 && !host.inbound().empty()) {
        expect(same(guest.inbound().front().bytes, kIHave, sizeof kIHave), "IHave bytes");
        expect(same(host.inbound().front().bytes, kShowMe, sizeof kShowMe), "ShowMe bytes");
        const auto here = kitsune::net::decodeHereIs(guest.inbound().back().bytes);
        expect(here.has_value() && here->sessionId == have->sessionId &&
                   here->header.programId == have->header.programId &&
                   here->numUsers == have->numUsers && here->maxUsers == have->maxUsers,
               "HereIs follows ShowMe");
    }
    expect(hostLog.find("e_IHaveThisGame") != std::string::npos, "host log has IHave");
    expect(hostLog.find("e_showMeThisGame") != std::string::npos, "host log has ShowMe");
    expect(hostLog.find("e_hereIsThisGame") != std::string::npos, "host log has HereIs");

    // A guest that never answers must not stick the host inside recv.
    UdpJoin lonely(UdpJoinRole::Host);
    UdpJoin sink(UdpJoinRole::Guest);
    expect(lonely.bind(0), "lonely bind");
    expect(sink.bind(0), "sink bind");
    lonely.useIHave(*have);
    const auto sinkPeer = kitsune::net::udpLoopback(sink.localPort());
    const auto began = std::chrono::steady_clock::now();
    const auto lonelyStatus = lonely.exchange(&sinkPeer, std::chrono::milliseconds(300));
    const auto elapsed = std::chrono::steady_clock::now() - began;
    expect(lonelyStatus == UdpJoinStatus::TimedOut, "unanswered host times out");
    expect(elapsed < std::chrono::seconds(2), "unanswered host returns");

    kitsune::net::HereIsThisGameEvent filled;
    filled.header = have->header;
    filled.sessionId = have->sessionId;
    filled.numUsers = have->numUsers;
    filled.maxUsers = have->maxUsers;
    filled.numObservers = have->numObservers;
    filled.maxObservers = have->maxObservers;
    filled.gameDescription.mapFileName = "Melee/Simple64.SC2Map";
    std::vector<std::byte> raw(8192);
    const auto filledSize = kitsune::net::encodeHereIs(filled, raw);
    expect(filledSize.has_value(), "filled here-is encodes");
    if (filledSize) {
        raw.resize(*filledSize);
        UdpJoin hostFill(UdpJoinRole::Host);
        UdpJoin guestFill(UdpJoinRole::Guest);
        expect(hostFill.bind(0) && guestFill.bind(0), "filled bind");
        hostFill.useIHave(*have);
        hostFill.useHereIsDatagram(std::move(raw));
        const auto fillPeer = kitsune::net::udpLoopback(guestFill.localPort());
        UdpJoinStatus fillGuest = UdpJoinStatus::TimedOut;
        std::thread fillThread(
            [&] { fillGuest = guestFill.exchange(nullptr, std::chrono::seconds(2)); });
        const auto fillHost = hostFill.exchange(&fillPeer, std::chrono::seconds(2));
        fillThread.join();
        expect(fillHost == UdpJoinStatus::Completed && fillGuest == UdpJoinStatus::Completed,
               "filled exchange completed");
        const auto got = kitsune::net::decodeHereIs(guestFill.inbound().back().bytes);
        expect(got.has_value() && got->gameDescription.mapFileName == "Melee/Simple64.SC2Map",
               "host sends the supplied description");
    }

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
