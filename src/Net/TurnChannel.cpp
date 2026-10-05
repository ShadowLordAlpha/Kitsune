#include "Kitsune/Net/TurnChannel.h"

#include <span>
#include <utility>

namespace kitsune::net {

TurnChannel TurnChannel::guest(std::uint32_t peerId, const JoinSucceededEvent& joined) {
    TurnChannel channel;
    channel.role_ = Role::Guest;
    channel.peerId_ = peerId;
    channel.first_ = joined.syncSequence;
    channel.expected_ = joined.syncSequence; // the host's first turn
    channel.ack_ = joined.syncSequence;
    channel.next_ = joined.joinSequence; // where this guest starts
    return channel;
}

TurnChannel TurnChannel::host(std::uint32_t peerId, const JoinSucceededEvent& joined,
                              const UserInfo& guestInfo) {
    TurnChannel channel;
    channel.role_ = Role::Host;
    channel.peerId_ = peerId;
    channel.first_ = joined.syncSequence;
    channel.next_ = joined.syncSequence;
    // Nothing has come from the guest yet, so the first acks name the sync
    // sequence. The guest's own turns start at the join sequence.
    channel.ack_ = joined.syncSequence;
    channel.expected_ = joined.joinSequence;
    channel.joinerTag_ = joined.joinerTag;
    channel.joiner_ = guestInfo;
    return channel;
}

std::vector<std::byte> TurnChannel::turn(std::optional<std::vector<TurnJoiner>> joiners) {
    TurnEvent body;
    body.header.sequence = next_;
    body.header.ackSequence = ack_;
    body.header.originatingPeer = peerId_;
    body.syncValue = kNoSyncValue;
    body.joiners = std::move(joiners);
    while (!pending_.empty()) {
        std::vector<std::byte> one;
        const game::Event& event = pending_.front();
        if (!game::encodeEvents(std::span(&event, 1), one)) {
            pending_.pop_front(); // cannot be written, so it can never be sent
            continue;
        }
        if (body.gameEventData.size() + one.size() > game::kEventDataMax) {
            break;
        }
        body.gameEventData.insert(body.gameEventData.end(), one.begin(), one.end());
        pending_.pop_front();
    }
    ++next_;

    std::vector<std::byte> buffer(kStreamBufferBytes);
    const auto size = encodeSystemEvent(body, buffer);
    if (!size) {
        return {};
    }
    buffer.resize(*size);
    return buffer;
}

void TurnChannel::queue(game::Event event) {
    pending_.push_back(std::move(event));
}

void TurnChannel::queue(std::vector<game::Event> events) {
    for (auto& event : events) {
        pending_.push_back(std::move(event));
    }
}

// Counts the peer's events and lets the handler answer them. The events that
// did decode are still handed over when the rest of the blob does not.
void TurnChannel::take(const TurnEvent& peer, std::uint16_t sequence) {
    eventBytes_ += peer.gameEventData.size();
    if (peer.gameEventData.empty() && !handler_) {
        return;
    }
    const auto decoded = game::decodeEvents(peer.gameEventData);
    eventsReceived_ += decoded.events.size();
    if (!decoded.complete) {
        ++undecoded_;
    }
    if (handler_) {
        queue(handler_(sequence, decoded.events));
    }
}

std::vector<std::vector<std::byte>> TurnChannel::start() {
    std::vector<std::vector<std::byte>> out;
    if (role_ != Role::Host || !joiner_ || next_ != first_) {
        return out;
    }
    out.push_back(turn(std::nullopt));
    TurnJoiner joiner;
    joiner.tag = joinerTag_;
    joiner.info = *joiner_;
    joiner.syncSequence = first_;
    joiner.ready = true;
    out.push_back(turn(std::vector<TurnJoiner>{std::move(joiner)}));
    return out;
}

std::vector<std::vector<std::byte>> TurnChannel::onMessage(const SystemEvent& message) {
    std::vector<std::vector<std::byte>> out;
    const auto* peer = std::get_if<TurnEvent>(&message);
    if (!peer) {
        return out;
    }
    const auto sequence = peer->header.sequence;
    if (role_ == Role::Guest) {
        if (sequence != expected_) {
            return out;
        }
        ++expected_;
        ack_ = expected_;
        ++received_;
        take(*peer, sequence);
        // Answer with the turn after theirs, once we have caught up to our start.
        if (expected_ == next_) {
            out.push_back(turn(std::nullopt));
        }
        return out;
    }
    // Host: the guest's turns start at the join sequence and are answered one for one.
    if (sequence != expected_) {
        return out;
    }
    ++expected_;
    ack_ = expected_;
    ++received_;
    take(*peer, sequence);
    if (next_ == sequence) {
        out.push_back(turn(std::nullopt));
    }
    return out;
}

} // namespace kitsune::net
