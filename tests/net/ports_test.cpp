#include "Kitsune/Net/Ports.h"

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
    using kitsune::net::JoinRole;
    using kitsune::net::PortPair;

    // Host of the recorded 1v1 Simple64 join. Guest game is 27152.
    constexpr PortPair kServer{27150, 27151};
    constexpr PortPair kGuest{27152, 27153};
    constexpr PortPair kSecondGuest{27154, 27155};

    constexpr auto packedServer = kitsune::net::packPortPair(kServer);
    constexpr auto packedGuest = kitsune::net::packPortPair(kGuest);
    expect(packedServer == 0x6A0F6A0Eu, "server dword is game | (base << 16)");
    expect(packedGuest == 0x6A116A10u, "guest dword");
    expect(kitsune::net::unpackPortPair(packedServer).game == 27150, "unpack server game");
    expect(kitsune::net::unpackPortPair(packedServer).base == 27151, "unpack server base");
    expect(kitsune::net::packPortPair(kitsune::net::unpackPortPair(0x6A116A10u)) == 0x6A116A10u,
           "pack round trip");

    const PortPair clients[] = {kGuest, kSecondGuest};
    const auto host = kitsune::net::assignJoinPorts(JoinRole::Host, kServer, clients);
    expect(host.has_value(), "host assignment");
    if (host) {
        expect(host->local.game == 27150 && host->local.base == 27151, "host local");
        expect(host->remote.game == 27152 && host->remote.base == 27153, "host remote is client 0");
    }

    const auto guest = kitsune::net::assignJoinPorts(JoinRole::Guest, kServer, clients);
    expect(guest.has_value(), "guest assignment");
    if (guest) {
        expect(guest->local.game == 27152 && guest->local.base == 27153, "guest local is client 0");
        expect(guest->remote.game == 27150 && guest->remote.base == 27151,
               "guest remote is server");
    }

    expect(!kitsune::net::assignJoinPorts(JoinRole::Host, kServer, {}).has_value(),
           "no client ports");

    const auto aimed = kitsune::net::guestBroadcastRange(27152);
    expect(aimed.begin == 27152 && aimed.end == 27153, "virgin SendTo is one guest game port");

    const auto unset = kitsune::net::guestBroadcastRange(0);
    expect(unset.begin == kitsune::net::kUnsetGuestBroadcastBegin &&
               unset.end == kitsune::net::kUnsetGuestBroadcastEnd,
           "unset remote game uses the default eight-port range");
    expect(unset.end - unset.begin == 8, "default range length");

    const auto bind = kitsune::net::singlePort(27150);
    expect(bind.begin == 27150 && bind.end == 27151, "local bind is one port");

    const auto top = kitsune::net::singlePort(65535);
    expect(top.begin == 65535u && top.end == 65536u, "port increment does not wrap");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
