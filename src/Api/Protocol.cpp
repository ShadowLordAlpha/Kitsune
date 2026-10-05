#include "Kitsune/Api/Protocol.h"

#include "Kitsune/Api/CreateGame.h"
#include "Kitsune/Version.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace kitsune::api {
namespace {

void stamp(SC2APIProtocol::Response& response, const SC2APIProtocol::Request& request,
           SC2APIProtocol::Status status) {
    if (request.has_id()) {
        response.set_id(request.id());
    }
    response.set_status(status);
}

SC2APIProtocol::Response rejected(const SC2APIProtocol::Request& request, SC2APIProtocol::Status status,
                                  std::string_view why) {
    SC2APIProtocol::Response response;
    stamp(response, request, status);
    response.add_error(std::string(why));
    return response;
}

// Player ids follow create-game setup order, computers included. The joining
// client is the Nth participant or observer. A guest that never saw the
// create falls back to clientIndex + 1, which is player 2 for the one guest
// 4.10 accepts.
[[nodiscard]] std::uint32_t joiningPlayerId(int clientIndex,
                                            const std::vector<SC2APIProtocol::PlayerType>& setup,
                                            const SC2APIProtocol::RequestJoinGame& join) {
    if (join.has_observed_player_id()) {
        return join.observed_player_id();
    }
    int seen = 0;
    for (std::size_t i = 0; i < setup.size(); ++i) {
        const auto type = setup[i];
        if (type != SC2APIProtocol::Participant && type != SC2APIProtocol::Observer) {
            continue;
        }
        if (seen == clientIndex) {
            return static_cast<std::uint32_t>(i + 1);
        }
        ++seen;
    }
    return static_cast<std::uint32_t>(clientIndex + 1);
}

} // namespace

Protocol::Protocol(int clientIndex) : clientIndex_(clientIndex) {}

SC2APIProtocol::Status Protocol::status() const noexcept {
    return status_;
}

net::LocalNet& Protocol::localNet() noexcept {
    return net_;
}

SC2APIProtocol::Response Protocol::handle(const SC2APIProtocol::Request& request) {
    using Case = SC2APIProtocol::Request::RequestCase;
    const auto state = status_;

    switch (request.request_case()) {
    case Case::kCreateGame: {
        const bool endedSingle = state == SC2APIProtocol::ended && singlePlayer_;
        if (state != SC2APIProtocol::launched && !endedSingle) {
            return rejected(request, state, "invalid in the current status");
        }
        const auto& create = request.create_game();
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* body = response.mutable_create_game();
        const bool hasLocal = create.has_local_map();
        const bool hasName = create.has_battlenet_map_name() && !create.battlenet_map_name().empty();
        const bool localEmpty = !hasLocal || (create.local_map().map_path().empty() &&
                                               create.local_map().map_data().empty());
        if (localEmpty && !hasName) {
            body->set_error(SC2APIProtocol::ResponseCreateGame::MissingMap);
            return response;
        }
        std::vector<PlayerType> types;
        types.reserve(static_cast<std::size_t>(create.player_setup_size()));
        for (const auto& player : create.player_setup()) {
            if (!player.has_type()) {
                body->set_error(SC2APIProtocol::ResponseCreateGame::InvalidPlayerSetup);
                body->set_error_details("PlayerSetup missing type");
                return response;
            }
            types.push_back(static_cast<PlayerType>(player.type()));
        }
        const auto decision = checkCreateGame(types);
        if (decision.status != CreateGameStatus::Ok) {
            body->set_error(static_cast<SC2APIProtocol::ResponseCreateGame::Error>(decision.error));
            body->set_error_details(std::string(decision.detail));
            return response;
        }
        setup_ = {};
        int agents = 0;
        for (const auto type : types) {
            setup_.push_back(static_cast<SC2APIProtocol::PlayerType>(type));
            if (type == PlayerType::Participant || type == PlayerType::Observer) {
                ++agents;
            }
        }
        singlePlayer_ = agents == 1;
        realtime_ = create.realtime();
        mapPath_.clear();
        mapName_.clear();
        if (hasLocal) {
            mapPath_ = create.local_map().map_path();
        }
        if (hasName) {
            mapName_ = create.battlenet_map_name();
        }
        status_ = SC2APIProtocol::init_game;
        response.set_status(status_);
        return response;
    }
    case Case::kJoinGame: {
        const auto& join = request.join_game();
        const bool hostSide = state == SC2APIProtocol::init_game;
        // The other process never called create_game. It is still launched.
        const bool guestSide = state == SC2APIProtocol::launched && clientIndex_ != 0;
        if (!hostSide && !guestSide) {
            return rejected(request, state, "invalid in the current status");
        }
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* body = response.mutable_join_game();
        if (clientIndex_ >= 2) {
            body->set_error(SC2APIProtocol::ResponseJoinGame::NetworkError);
            return response;
        }
        if (!join.has_race() && !join.has_observed_player_id()) {
            body->set_error(SC2APIProtocol::ResponseJoinGame::MissingParticipation);
            return response;
        }
        if (join.has_observed_player_id() && join.observed_player_id() == 0) {
            body->set_error(SC2APIProtocol::ResponseJoinGame::InvalidObservedPlayerId);
            return response;
        }
        const bool multi = !singlePlayer_ || guestSide;
        if (multi) {
            if (!join.has_server_ports() || join.client_ports_size() == 0) {
                body->set_error(SC2APIProtocol::ResponseJoinGame::MissingPorts);
                return response;
            }
            const auto role = clientIndex_ == 0 ? net::JoinRole::Host : net::JoinRole::Guest;
            if (!net_.accept(role, join)) {
                body->set_error(SC2APIProtocol::ResponseJoinGame::MissingPorts);
                return response;
            }
        }
        body->set_player_id(joiningPlayerId(clientIndex_, setup_, join));
        status_ = SC2APIProtocol::in_game;
        response.set_status(status_);
        return response;
    }
    case Case::kRestartGame: {
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        if (state != SC2APIProtocol::ended || !singlePlayer_) {
            response.mutable_restart_game()->set_error(SC2APIProtocol::ResponseRestartGame::LaunchError);
            return response;
        }
        status_ = SC2APIProtocol::in_game;
        response.set_status(status_);
        response.mutable_restart_game();
        return response;
    }
    case Case::kStartReplay: {
        const bool endedSingle = state == SC2APIProtocol::ended && singlePlayer_;
        if (state != SC2APIProtocol::launched && !endedSingle) {
            return rejected(request, state, "invalid in the current status");
        }
        const auto& start = request.start_replay();
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* body = response.mutable_start_replay();
        if (!start.has_replay_path() && !start.has_replay_data()) {
            body->set_error(SC2APIProtocol::ResponseStartReplay::MissingReplay);
            return response;
        }
        body->set_error(SC2APIProtocol::ResponseStartReplay::LaunchError);
        body->set_error_details("replay playback is not available");
        return response;
    }
    case Case::kLeaveGame: {
        if (state != SC2APIProtocol::in_game) {
            return rejected(request, state, "invalid in the current status");
        }
        status_ = SC2APIProtocol::launched;
        SC2APIProtocol::Response response;
        stamp(response, request, status_);
        response.mutable_leave_game();
        return response;
    }
    case Case::kQuickSave: {
        if (state != SC2APIProtocol::in_game) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_quick_save();
        return response;
    }
    case Case::kQuickLoad: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::ended) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_quick_load();
        return response;
    }
    case Case::kQuit: {
        status_ = SC2APIProtocol::quit;
        SC2APIProtocol::Response response;
        stamp(response, request, status_);
        response.mutable_quit();
        return response;
    }
    case Case::kGameInfo: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::in_replay &&
            state != SC2APIProtocol::ended) {
            return rejected(request, state, "invalid in the current status");
        }
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* info = response.mutable_game_info();
        if (!mapName_.empty()) {
            info->set_map_name(mapName_);
        }
        if (!mapPath_.empty()) {
            info->set_local_map_path(mapPath_);
        }
        for (std::size_t i = 0; i < setup_.size(); ++i) {
            auto* player = info->add_player_info();
            player->set_player_id(static_cast<std::uint32_t>(i + 1));
            player->set_type(setup_[i]);
        }
        return response;
    }
    case Case::kObservation: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::in_replay &&
            state != SC2APIProtocol::ended) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_observation();
        return response;
    }
    case Case::kAction: {
        if (state != SC2APIProtocol::in_game) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_action();
        return response;
    }
    case Case::kObsAction: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::in_replay) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_obs_action();
        return response;
    }
    case Case::kStep: {
        const bool stepOk = (state == SC2APIProtocol::in_game && !realtime_) || state == SC2APIProtocol::in_replay;
        if (!stepOk) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_step();
        return response;
    }
    case Case::kData: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::in_replay &&
            state != SC2APIProtocol::ended) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_data();
        return response;
    }
    case Case::kQuery: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::in_replay &&
            state != SC2APIProtocol::ended) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_query();
        return response;
    }
    case Case::kSaveReplay: {
        if (state != SC2APIProtocol::in_game && state != SC2APIProtocol::ended) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "replay save is not available");
        response.mutable_save_replay();
        return response;
    }
    case Case::kMapCommand: {
        if (state != SC2APIProtocol::in_game) {
            return rejected(request, state, "invalid in the current status");
        }
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        response.mutable_map_command()->set_error(SC2APIProtocol::ResponseMapCommand::NoTriggerError);
        return response;
    }
    case Case::kReplayInfo: {
        const auto& info = request.replay_info();
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* body = response.mutable_replay_info();
        if (!info.has_replay_path() && !info.has_replay_data()) {
            body->set_error(SC2APIProtocol::ResponseReplayInfo::MissingReplay);
            return response;
        }
        body->set_error(SC2APIProtocol::ResponseReplayInfo::ParsingError);
        body->set_error_details("replay parsing is not available");
        return response;
    }
    case Case::kAvailableMaps: {
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        response.mutable_available_maps();
        return response;
    }
    case Case::kSaveMap: {
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* body = response.mutable_save_map();
        if (!request.save_map().has_map_data() || request.save_map().map_data().empty()) {
            body->set_error(SC2APIProtocol::ResponseSaveMap::InvalidMapData);
            return response;
        }
        response.add_error("map save is not available");
        return response;
    }
    case Case::kPing: {
        SC2APIProtocol::Response response;
        stamp(response, request, state);
        auto* ping = response.mutable_ping();
        ping->set_game_version(std::string(targetLabel()));
        ping->set_data_version(std::string(kDataHash));
        ping->set_data_build(static_cast<std::uint32_t>(kDataBuild));
        ping->set_base_build(static_cast<std::uint32_t>(kBaseBuild));
        return response;
    }
    case Case::kDebug: {
        if (state != SC2APIProtocol::in_game) {
            return rejected(request, state, "invalid in the current status");
        }
        auto response = rejected(request, state, "simulation is not available");
        response.mutable_debug();
        return response;
    }
    case Case::REQUEST_NOT_SET:
        return rejected(request, state, "missing request");
    }
    return rejected(request, state, "missing request");
}

} // namespace kitsune::api
