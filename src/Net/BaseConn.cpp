#include "Kitsune/Net/BaseConn.h"

namespace kitsune::net {

BaseConn::BaseConn(BaseState state) noexcept : state_(state) {}

std::optional<BaseConn> BaseConn::accept(bool hasListener) {
    if (!hasListener) {
        return std::nullopt;
    }
    return BaseConn{BaseState::Accepted};
}

BaseState BaseConn::state() const noexcept {
    return state_;
}

BaseTick BaseConn::tick() noexcept {
    // Linux Base75689 BaseConn_tick (0x02653c20) and BaseConn_notify (0x0265e7e0).
    BaseTick tick;
    tick.state = state_;
    if (state_ == BaseState::State0 || state_ == BaseState::ConnectFailed ||
        state_ == BaseState::RecvFailed) {
        tick.closeSocket = true;
        return tick;
    }
    if (state_ == BaseState::Accepted) {
        tick.notifyListener = true;
        tick.notifyFlag = 1;
        state_ = stateFromNotifyFlag(tick.notifyFlag);
        tick.state = state_;
    }
    return tick;
}

void BaseConn::onRecv(int status) noexcept {
    if (status != 1) {
        state_ = BaseState::RecvFailed;
    }
}

void BaseConn::onConnect(int status) noexcept {
    if (status == 2) {
        state_ = BaseState::ConnectFailed;
    }
}

} // namespace kitsune::net
