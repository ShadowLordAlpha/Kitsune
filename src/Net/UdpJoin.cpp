#include "Kitsune/Net/UdpJoin.h"

#include <array>
#include <string>

#include "Kitsune/Net/Lockstep.h"

namespace kitsune::net {
namespace {

constexpr std::chrono::milliseconds kRetransmit{100};

std::optional<std::size_t> writeRecord(const LocalEvent& event, std::span<std::byte> out) {
    return encodeLocalEvent(event, out);
}

} // namespace

UdpJoin::UdpJoin(UdpJoinRole role) : role_(role) {}

bool UdpJoin::bind(std::uint16_t port) {
    return socket_.bind(port);
}

std::uint16_t UdpJoin::localPort() const noexcept {
    return socket_.localPort();
}

void UdpJoin::useIHave(IHaveThisGameEvent have) {
    have_ = std::move(have);
}

void UdpJoin::useHereIsDatagram(std::vector<std::byte> datagram) {
    hereBytes_ = std::move(datagram);
}

void UdpJoin::setLog(std::function<void(std::string_view)> log) {
    log_ = std::move(log);
}

void UdpJoin::note(std::string_view line) const {
    if (log_) {
        log_(line);
    }
}

const std::vector<UdpInbound>& UdpJoin::inbound() const noexcept {
    return inbound_;
}

void UdpJoin::observe(const UdpSocket::Packet& packet) {
    UdpInbound inbound;
    inbound.from = packet.from;
    inbound.bytes = packet.bytes;
    inbound.record = decodeLocalEvent(packet.bytes);
    if (inbound.record) {
        note(describeLocalEvent(*inbound.record));
    } else {
        note("udp undecoded bytes=" + std::to_string(packet.bytes.size()));
    }
    inbound_.push_back(std::move(inbound));
}

void UdpJoin::collect(std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto slice = remaining < kRetransmit ? remaining : kRetransmit;
        auto packet = socket_.recvFrom(slice);
        if (!packet) {
            continue;
        }
        observe(*packet);
    }
}

UdpJoinStatus UdpJoin::exchange(const UdpPeer* remote, std::chrono::milliseconds budget) {
    if (role_ == UdpJoinRole::Host &&
        (!have_ || remote == nullptr)) {
        return UdpJoinStatus::NeedIHave;
    }

    std::array<std::byte, 64> encoded{};
    const auto helloSize = have_ ? writeRecord(*have_, encoded) : std::nullopt;
    if (role_ == UdpJoinRole::Host && !helloSize) {
        return UdpJoinStatus::NeedIHave;
    }

    const auto deadline = std::chrono::steady_clock::now() + budget;
    // now - time_point::min() overflows. Start one interval in the past so
    // the first hello goes out on the first pass.
    auto lastHello = std::chrono::steady_clock::now() - kRetransmit;
    bool replied = false;
    std::optional<LocalEvent> seen;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (role_ == UdpJoinRole::Host && now - lastHello >= kRetransmit) {
            if (!socket_.sendTo(*remote, {encoded.data(), *helloSize})) {
                return UdpJoinStatus::TimedOut;
            }
            note(describeLocalEvent(LocalEvent{*have_}));
            lastHello = now;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto slice = remaining < kRetransmit ? remaining : kRetransmit;
        auto packet = socket_.recvFrom(slice);
        if (!packet) {
            continue;
        }

        observe(*packet);
        const auto record = inbound_.back().record;
        if (!record) {
            continue;
        }

        if (std::holds_alternative<HereIsThisGameEvent>(*record)) {
            if (role_ == UdpJoinRole::Guest && replied && seen && sameSession(*record, *seen)) {
                return UdpJoinStatus::Completed;
            }
            continue;
        }

        if (role_ == UdpJoinRole::Guest && std::holds_alternative<IHaveThisGameEvent>(*record)) {
            GuestLockstep guest;
            const auto reply = guest.onDatagram(packet->bytes);
            if (!reply) {
                continue;
            }
            std::array<std::byte, 64> out{};
            const auto size = writeRecord(*reply, out);
            if (!size || !socket_.sendTo(packet->from, {out.data(), *size})) {
                return UdpJoinStatus::TimedOut;
            }
            note(describeLocalEvent(*reply));
            seen = *record;
            replied = true;
            continue;
        }

        if (role_ == UdpJoinRole::Host && std::holds_alternative<ShowMeThisGameEvent>(*record) && have_ &&
            sameSession(*record, *have_)) {
            if (hereBytes_ && !hereBytes_->empty()) {
                if (!socket_.sendTo(packet->from, *hereBytes_)) {
                    return UdpJoinStatus::TimedOut;
                }
                if (const auto captured = decodeHereIs(*hereBytes_)) {
                    note(describeHereIs(*captured));
                } else {
                    note("udp here-is bytes=" + std::to_string(hereBytes_->size()));
                }
                return UdpJoinStatus::Completed;
            }
            HereIsThisGameEvent here;
            here.header = have_->header;
            here.sessionId = have_->sessionId;
            here.numUsers = have_->numUsers;
            here.maxUsers = have_->maxUsers;
            here.numObservers = have_->numObservers;
            here.maxObservers = have_->maxObservers;
            std::vector<std::byte> out(8192);
            const auto size = encodeHereIs(here, out);
            if (!size || !socket_.sendTo(packet->from, {out.data(), *size})) {
                return UdpJoinStatus::TimedOut;
            }
            note(describeHereIs(here));
            return UdpJoinStatus::Completed;
        }
    }
    return UdpJoinStatus::TimedOut;
}

} // namespace kitsune::net
