#include "Kitsune/Net/BaseConn.h"

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
    using kitsune::net::BaseConn;
    using kitsune::net::BaseState;

    expect(!BaseConn::accept(false).has_value(), "null listener closes");
    const auto accepted = BaseConn::accept(true);
    expect(accepted.has_value() && accepted->state() == BaseState::Accepted,
           "listener receives a state-2 connection");

    BaseConn conn{BaseState::Accepted};
    const auto up = conn.tick();
    expect(up.notifyListener && up.notifyFlag == 1, "state 2 notifies with flag 1");
    expect(conn.state() == BaseState::Up && up.state == BaseState::Up && !up.closeSocket,
           "flag 1 stores state 4");
    expect(BaseConn::stateFromNotifyFlag(1) == BaseState::Up, "state is flag times 4");

    const auto steady = conn.tick();
    expect(!steady.closeSocket && !steady.notifyListener && conn.state() == BaseState::Up,
           "state 4 stays up");

    for (const BaseState closing :
         {BaseState::State0, BaseState::ConnectFailed, BaseState::RecvFailed}) {
        BaseConn doomed{closing};
        const auto tick = doomed.tick();
        expect(tick.closeSocket && !tick.notifyListener && doomed.state() == closing,
               "closing states");
    }

    BaseConn live{BaseState::Up};
    live.onRecv(1);
    expect(live.state() == BaseState::Up, "recv status 1 keeps state");
    live.onRecv(0);
    expect(live.state() == BaseState::RecvFailed, "recv without bytes");
    expect(live.tick().closeSocket, "recv failure closes on the next tick");

    BaseConn dialing{BaseState::Accepted};
    dialing.onConnect(1);
    expect(dialing.state() == BaseState::Accepted, "other connect statuses stay");
    dialing.onConnect(2);
    expect(dialing.state() == BaseState::ConnectFailed, "connect status 2");
    expect(dialing.tick().closeSocket, "connect failure closes on the next tick");

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
