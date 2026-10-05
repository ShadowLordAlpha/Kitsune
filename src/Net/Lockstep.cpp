#include "Kitsune/Net/Lockstep.h"

#include <sstream>

namespace kitsune::net {

std::uint32_t localSessionId(const LocalEvent& event) noexcept {
    return std::visit(
        [](const auto& body) -> std::uint32_t {
            if constexpr (requires { body.sessionId; }) {
                return body.sessionId;
            } else {
                return 0;
            }
        },
        event);
}

bool sameSession(const LocalEvent& left, const LocalEvent& right) noexcept {
    return localHeader(left) == localHeader(right) && localSessionId(left) == localSessionId(right);
}

std::string describeLocalEvent(const LocalEvent& event) {
    if (const auto* here = std::get_if<HereIsThisGameEvent>(&event)) {
        return describeHereIs(*here);
    }
    std::ostringstream line;
    line << "udp " << localEventName(localEventId(event)) << " session=" << localSessionId(event)
         << " build=" << localHeader(event).version.build;
    if (const auto* have = std::get_if<IHaveThisGameEvent>(&event)) {
        line << " users=" << +have->numUsers << '/' << +have->maxUsers << " observers=" << +have->numObservers
             << '/' << +have->maxObservers;
    }
    return line.str();
}

std::optional<std::size_t> encodeHereIs(const HereIsThisGameEvent& event, std::span<std::byte> out) {
    return encodeLocalEvent(event, out);
}

std::optional<HereIsThisGameEvent> decodeHereIs(std::span<const std::byte> datagram) {
    const auto event = decodeLocalEvent(datagram);
    if (!event) {
        return std::nullopt;
    }
    if (const auto* here = std::get_if<HereIsThisGameEvent>(&*event)) {
        return *here;
    }
    return std::nullopt;
}

std::string describeHereIs(const HereIsThisGameEvent& event) {
    std::ostringstream line;
    line << "udp e_hereIsThisGame session=" << event.sessionId << " users=" << +event.numUsers << '/'
         << +event.maxUsers << " observers=" << +event.numObservers << '/' << +event.maxObservers
         << " map=" << event.gameDescription.mapFileName;
    return line.str();
}

std::optional<ShowMeThisGameEvent> GuestLockstep::onDatagram(std::span<const std::byte> datagram) const {
    const auto decoded = decodeLocalEvent(datagram);
    if (!decoded) {
        return std::nullopt;
    }
    const auto* have = std::get_if<IHaveThisGameEvent>(&*decoded);
    if (have == nullptr) {
        return std::nullopt;
    }
    ShowMeThisGameEvent show;
    show.header = have->header;
    show.sessionId = have->sessionId;
    return show;
}

std::optional<LocalEvent> HostLockstep::onDatagram(std::span<const std::byte>) const {
    // ShowMe is answered with HereIsThisGame, which the join builds itself.
    return std::nullopt;
}

} // namespace kitsune::net
