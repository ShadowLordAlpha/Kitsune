#pragma once

#include <string>
#include <vector>

#include "Kitsune/Net/LocalNet.h"

#include "s2clientprotocol/sc2api.pb.h"

namespace kitsune::api {

// One SC2APIProtocol connection. Status follows the table in sc2api.proto.
// clientIndex 0 is the host and joins from init_game. clientIndex 1 is the
// guest and joins from launched, without a local create_game. A later index
// is the client the 4.10 peer list cannot hold.
class Protocol {
public:
    explicit Protocol(int clientIndex = 0);

    [[nodiscard]] SC2APIProtocol::Status status() const noexcept;
    [[nodiscard]] net::LocalNet& localNet() noexcept;

    [[nodiscard]] SC2APIProtocol::Response handle(const SC2APIProtocol::Request& request);

private:
    int clientIndex_ = 0;
    SC2APIProtocol::Status status_ = SC2APIProtocol::launched;
    bool singlePlayer_ = true;
    bool realtime_ = false;
    std::string mapPath_{};
    std::string mapName_{};
    std::vector<SC2APIProtocol::PlayerType> setup_{};
    net::LocalNet net_{};
};

} // namespace kitsune::api
