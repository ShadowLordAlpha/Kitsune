#pragma once

#include <cstdint>
#include <optional>

namespace kitsune::net {

// Linux Base75689 base-port connection. Numbers are the state dword at
// connection+8. Names that are still a guess are the state number.
enum class BaseState : std::int32_t {
    State0 = 0,
    Accepted = 2,
    ConnectFailed = 3,
    Up = 4,
    RecvFailed = 6,
};

struct BaseTick {
    BaseState state = BaseState::State0;
    bool closeSocket = false;
    bool notifyListener = false;
    int notifyFlag = 0;
};

// One accepted base connection. A null listener does not produce an object.
class BaseConn {
public:
    explicit BaseConn(BaseState state) noexcept;

    [[nodiscard]] static std::optional<BaseConn> accept(bool hasListener);

    // State = flag * 4. The accepted tick notifies with flag 1, which stores Up.
    [[nodiscard]] static constexpr BaseState stateFromNotifyFlag(int flag) noexcept {
        return static_cast<BaseState>(flag * 4);
    }

    [[nodiscard]] BaseState state() const noexcept;

    // State 0, 3, and 6 close. State 2 notifies with flag 1 and becomes Up.
    [[nodiscard]] BaseTick tick() noexcept;

    // Status 1 leaves the state. Any other status stores RecvFailed.
    void onRecv(int status) noexcept;

    // Connect status 2 stores ConnectFailed. Other statuses are not specified.
    void onConnect(int status) noexcept;

private:
    BaseState state_;
};

} // namespace kitsune::net
