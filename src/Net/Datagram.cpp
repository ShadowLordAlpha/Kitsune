#include "Kitsune/Net/Datagram.h"

namespace kitsune::net {

PortRange fallbackBindRange() noexcept {
    return PortRange{kFallbackBindBegin, static_cast<std::uint32_t>(kFallbackBindLast) + 1u};
}

int SocketSlot::init() noexcept {
    const int abandoned = fd;
    fd = kEmptySocketFd;
    return abandoned;
}

bool SocketSlot::close() noexcept {
    if (fd == kEmptySocketFd) {
        return false;
    }
    fd = kEmptySocketFd;
    return true;
}

bool SocketSlot::applyBind(int status) noexcept {
    if (!bindRejected(status)) {
        return true;
    }
    close();
    return false;
}

} // namespace kitsune::net
