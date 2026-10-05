#include "Kitsune/Net/Datagram.h"

#include <cstdlib>
#include <iostream>
#include <vector>

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
    using kitsune::net::SocketSlot;

    expect(kitsune::net::DatagramOpen::kDomain == 10, "IPv6 domain on Linux");
    expect(kitsune::net::DatagramOpen::kType == 2, "datagram type");
    expect(kitsune::net::DatagramOpen::kProtocol == 0x11, "UDP protocol");
    expect(kitsune::net::bindRejected(2), "bind status 2 rejects");
    expect(!kitsune::net::bindRejected(1), "bind status 1 is not the reject");

    SocketSlot slot;
    slot.fd = 7;
    expect(slot.init() == 7, "init returns the fd it did not close");
    expect(slot.fd == kitsune::net::kEmptySocketFd, "init stores -1");
    expect(!slot.close(), "close skips -1");
    slot.fd = 7;
    expect(slot.close() && slot.fd == kitsune::net::kEmptySocketFd, "close drops a live fd");
    slot.fd = 7;
    expect(slot.applyBind(1) && slot.fd == 7, "bind status 1 keeps the fd");
    expect(!slot.applyBind(2) && slot.fd == kitsune::net::kEmptySocketFd, "bind status 2 closes");

    const auto fallback = kitsune::net::fallbackBindRange();
    expect(fallback.begin == 6135 && fallback.end == 6143, "fallback bind range");
    expect(fallback.end - fallback.begin == 8, "fallback is eight ports");

    std::vector<std::uint16_t> tried;
    const auto first =
        kitsune::net::firstSuccessfulPort(kitsune::net::singlePort(27150), [&](std::uint16_t port) {
            tried.push_back(port);
            return true;
        });
    expect(first == 27150 && tried.size() == 1, "one-port bind stops on success");

    tried.clear();
    const kitsune::net::PortRange two{27150, 27152};
    const auto second = kitsune::net::firstSuccessfulPort(two, [&](std::uint16_t port) {
        tried.push_back(port);
        return port == 27151;
    });
    expect(second == 27151 && tried.size() == 2 && tried[0] == 27150,
           "failed port is retried once");

    const auto none = kitsune::net::firstSuccessfulPort(two, [](std::uint16_t) { return false; });
    expect(!none.has_value(), "no open port");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
