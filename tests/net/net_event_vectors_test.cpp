// Every base-port (System) and game-port (Local) event id, as a datagram made from the client's
// type table by an independent encoder. Each must decode and encode back to the same bytes.

#include "../fixtures/net_event_vectors.h"
#include "Kitsune/Net/LocalEvent.h"
#include "Kitsune/Net/SystemEvent.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <span>
#include <vector>

namespace {

namespace fx = kitsune::fixtures;

std::set<std::string> gFailed;

std::span<const std::byte> view(const fx::NetVector& vector) {
    return {reinterpret_cast<const std::byte*>(vector.data), vector.size};
}

bool same(std::span<const std::byte> got, std::span<const std::byte> want) {
    return got.size() == want.size() && std::memcmp(got.data(), want.data(), want.size()) == 0;
}

void fail(const char* family, const fx::NetVector& vector, const char* why) {
    if (gFailed.insert(std::string(family) + " " + std::to_string(vector.id) + " " + why).second) {
        std::cerr << "FAIL " << family << " id " << static_cast<int>(vector.id) << ": " << why
                  << '\n';
    }
}

void system() {
    for (const auto& vector : fx::kSystemVectors) {
        const auto bytes = view(vector);
        const auto message = kitsune::net::decodeSystemEvent(bytes);
        if (!message) {
            fail("System", vector, "does not decode");
            continue;
        }
        if (static_cast<std::uint8_t>(kitsune::net::systemEventId(*message)) != vector.id) {
            fail("System", vector, "wrong id");
            continue;
        }
        std::vector<std::byte> out(4096);
        const auto size = kitsune::net::encodeSystemEvent(*message, out);
        if (!size || !same({out.data(), *size}, bytes)) {
            fail("System", vector, "does not encode back to the same bytes");
        }
    }
}

void local() {
    for (const auto& vector : fx::kLocalVectors) {
        const auto bytes = view(vector);
        std::vector<std::byte> out(8192);
        const auto event = kitsune::net::decodeLocalEvent(bytes);
        if (!event) {
            fail("Local", vector, "does not decode");
            continue;
        }
        if (static_cast<std::uint8_t>(kitsune::net::localEventId(*event)) != vector.id) {
            fail("Local", vector, "wrong id");
            continue;
        }
        const auto size = kitsune::net::encodeLocalEvent(*event, out);
        if (!size || !same({out.data(), *size}, bytes)) {
            fail("Local", vector, "does not encode back to the same bytes");
        }
    }
}

} // namespace

int main() {
    system();
    local();
    if (gFailed.empty()) {
        std::cout << "net_event_vectors ok\n";
    }
    return gFailed.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}
