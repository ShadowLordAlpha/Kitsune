#include "Kitsune/Net/LocalNet.h"

namespace kitsune::net {
namespace {

std::optional<PortPair> pairFrom(const SC2APIProtocol::PortSet& ports) {
    if (!ports.has_game_port() || !ports.has_base_port()) {
        return std::nullopt;
    }
    const auto game = ports.game_port();
    const auto base = ports.base_port();
    if (game < 0 || game > 65535 || base < 0 || base > 65535) {
        return std::nullopt;
    }
    return PortPair{static_cast<std::uint16_t>(game), static_cast<std::uint16_t>(base)};
}

} // namespace

PeerSession& LocalNet::session() noexcept {
    return session_;
}

const std::optional<JoinPorts>& LocalNet::ports() const noexcept {
    return ports_;
}

std::optional<JoinPorts> LocalNet::accept(JoinRole role, const SC2APIProtocol::RequestJoinGame& join) {
    if (!join.has_server_ports() || join.client_ports_size() == 0) {
        return std::nullopt;
    }
    const auto server = pairFrom(join.server_ports());
    if (!server) {
        return std::nullopt;
    }
    PortPair clients[1];
    const auto client = pairFrom(join.client_ports(0));
    if (!client) {
        return std::nullopt;
    }
    clients[0] = *client;
    auto assigned = assignJoinPorts(role, *server, clients);
    if (!assigned) {
        return std::nullopt;
    }
    ports_ = assigned;
    return assigned;
}

} // namespace kitsune::net
