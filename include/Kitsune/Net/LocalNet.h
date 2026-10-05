#pragma once

#include <optional>

#include "Kitsune/Net/PeerSession.h"

#include "s2clientprotocol/sc2api.pb.h"

namespace kitsune::net {

// The local net object the API join hands a PortSet to.
// One game socket, one base socket, and the ports from the last accepted join.
class LocalNet {
public:
    [[nodiscard]] PeerSession& session() noexcept;
    [[nodiscard]] const std::optional<JoinPorts>& ports() const noexcept;

    // server_ports plus client_ports[0]. A port outside uint16, or a missing
    // game/base field, is rejected. Further client_ports are ignored.
    [[nodiscard]] std::optional<JoinPorts> accept(JoinRole role,
                                                  const SC2APIProtocol::RequestJoinGame& join);

private:
    PeerSession session_{};
    std::optional<JoinPorts> ports_{};
};

} // namespace kitsune::net
