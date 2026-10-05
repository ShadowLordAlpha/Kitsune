#include "Kitsune/Net/PeerSession.h"

namespace kitsune::net {

void SendLane::setPeerCount(int count) noexcept {
    peerCount_ = count;
}

int SendLane::peerCount() const noexcept {
    return peerCount_;
}

bool SendLane::store(int connection) noexcept {
    if (peerCount_ != 0 || first_) {
        return false;
    }
    first_ = connection;
    return true;
}

std::optional<int> SendLane::target() const noexcept {
    return first_;
}

void StartGate::begin(int slotCount) noexcept {
    // Linux Base75689 StartFlag_set (0x00f02710). No peer-count test.
    open_ = true;
    singleSlot_ = slotCount == 1;
}

bool StartGate::open() const noexcept {
    return open_;
}

bool StartGate::singleSlot() const noexcept {
    return singleSlot_;
}

SocketSlot& PeerSession::game() noexcept {
    return game_;
}

SocketSlot& PeerSession::base() noexcept {
    return base_;
}

SendLane& PeerSession::lane() noexcept {
    return lane_;
}

StartGate& PeerSession::start() noexcept {
    return start_;
}

} // namespace kitsune::net
