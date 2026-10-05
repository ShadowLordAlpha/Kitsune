#include "Kitsune/Api/Protocol.h"
#include "Kitsune/Version.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int gFailures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++gFailures;
    }
}

void setPorts(SC2APIProtocol::PortSet* ports, int game, int base) {
    ports->set_game_port(game);
    ports->set_base_port(base);
}

SC2APIProtocol::Request mapCreate(const char* path, bool realtime) {
    SC2APIProtocol::Request request;
    auto* create = request.mutable_create_game();
    create->mutable_local_map()->set_map_path(path);
    create->set_realtime(realtime);
    return request;
}

void addPlayer(SC2APIProtocol::Request* request, SC2APIProtocol::PlayerType type) {
    request->mutable_create_game()->add_player_setup()->set_type(type);
}

SC2APIProtocol::Request joinAs(SC2APIProtocol::Race race, bool withPorts) {
    SC2APIProtocol::Request request;
    request.mutable_join_game()->set_race(race);
    if (withPorts) {
        auto* join = request.mutable_join_game();
        setPorts(join->mutable_server_ports(), 27150, 27151);
        setPorts(join->add_client_ports(), 27152, 27153);
    }
    return request;
}

} // namespace

int main() {
    using kitsune::api::Protocol;

    {
        Protocol api;
        SC2APIProtocol::Request request;
        request.set_id(19);
        request.mutable_ping();
        const std::string bytes = request.SerializeAsString();
        SC2APIProtocol::Request parsed;
        expect(parsed.ParseFromString(bytes), "ping request round trip");
        const auto response = api.handle(parsed);
        expect(response.id() == 19, "ping keeps the request id");
        expect(response.status() == SC2APIProtocol::launched, "ping stays launched");
        expect(api.status() == SC2APIProtocol::launched, "ping does not change status");
        expect(response.ping().game_version() == std::string(kitsune::targetLabel()), "ping version");
        expect(response.ping().data_version() == std::string(kitsune::kDataHash), "ping data hash");
        expect(response.ping().base_build() == 75689, "ping base build");
        expect(response.ping().data_build() == 75689, "ping data build");
    }

    {
        Protocol api;
        auto request = mapCreate("Maps/Simple64.SC2Map", false);
        addPlayer(&request, SC2APIProtocol::Participant);
        addPlayer(&request, SC2APIProtocol::Computer);
        const auto created = api.handle(request);
        expect(!created.create_game().has_error(), "single player create");
        expect(created.status() == SC2APIProtocol::init_game, "create moves to init_game");
        expect(api.status() == SC2APIProtocol::init_game, "status is init_game");

        const auto joined = api.handle(joinAs(SC2APIProtocol::Terran, false));
        expect(!joined.join_game().has_error(), "single player join");
        expect(joined.join_game().player_id() == 1, "participant is player 1");
        expect(joined.status() == SC2APIProtocol::in_game, "join moves to in_game");

        SC2APIProtocol::Request info;
        info.mutable_game_info();
        const auto game = api.handle(info);
        expect(game.game_info().local_map_path() == "Maps/Simple64.SC2Map", "game info map path");
        expect(game.game_info().player_info_size() == 2, "game info has both slots");
        expect(game.game_info().player_info(0).type() == SC2APIProtocol::Participant, "slot 1 type");
        expect(game.game_info().player_info(1).type() == SC2APIProtocol::Computer, "slot 2 type");
        expect(game.game_info().player_info(1).player_id() == 2, "computer is player 2");
    }

    {
        Protocol api;
        auto request = mapCreate("Maps/Simple64.SC2Map", false);
        addPlayer(&request, SC2APIProtocol::Computer);
        addPlayer(&request, SC2APIProtocol::Participant);
        expect(api.handle(request).status() == SC2APIProtocol::init_game, "computer-first create");
        const auto joined = api.handle(joinAs(SC2APIProtocol::Terran, false));
        expect(joined.join_game().player_id() == 2, "human after a computer is player 2");
    }

    {
        Protocol api;
        auto request = mapCreate("Maps/Simple64.SC2Map", false);
        const auto created = api.handle(request);
        expect(created.create_game().error() == SC2APIProtocol::ResponseCreateGame::MissingPlayerSetup,
               "empty setup is MissingPlayerSetup");
        expect(api.status() == SC2APIProtocol::launched, "failed create stays launched");
    }

    {
        Protocol host;
        auto request = mapCreate("Maps/Simple64.SC2Map", false);
        addPlayer(&request, SC2APIProtocol::Participant);
        addPlayer(&request, SC2APIProtocol::Participant);
        expect(host.handle(request).status() == SC2APIProtocol::init_game, "1v1 create");
        const auto missing = host.handle(joinAs(SC2APIProtocol::Terran, false));
        expect(missing.join_game().error() == SC2APIProtocol::ResponseJoinGame::MissingPorts,
               "1v1 join without ports");
        expect(host.status() == SC2APIProtocol::init_game, "missing ports stays init_game");

        const auto joined = host.handle(joinAs(SC2APIProtocol::Protoss, true));
        expect(!joined.join_game().has_error(), "1v1 host join");
        expect(joined.join_game().player_id() == 1, "host is player 1");
        const auto& ports = host.localNet().ports();
        expect(ports.has_value(), "host stored join ports");
        if (ports) {
            expect(ports->local.game == 27150 && ports->local.base == 27151, "host binds server");
            expect(ports->remote.game == 27152 && ports->remote.base == 27153, "host aims at client");
        }
    }

    {
        Protocol guest(1);
        const auto joined = guest.handle(joinAs(SC2APIProtocol::Zerg, true));
        expect(!joined.join_game().has_error(), "guest joins from launched");
        expect(joined.status() == SC2APIProtocol::in_game, "guest is in_game");
        expect(joined.join_game().player_id() == 2, "guest is player 2");
        const auto& ports = guest.localNet().ports();
        expect(ports.has_value(), "guest stored join ports");
        if (ports) {
            expect(ports->local.game == 27152 && ports->local.base == 27153, "guest binds client");
            expect(ports->remote.game == 27150 && ports->remote.base == 27151, "guest aims at server");
        }
    }

    {
        Protocol extra(2);
        const auto joined = extra.handle(joinAs(SC2APIProtocol::Terran, true));
        expect(joined.join_game().error() == SC2APIProtocol::ResponseJoinGame::NetworkError,
               "third client is NetworkError");
        expect(extra.status() == SC2APIProtocol::launched, "third client stays launched");
        expect(!extra.localNet().ports().has_value(), "rejected client stores no ports");
    }

    {
        Protocol api;
        SC2APIProtocol::Request request;
        request.mutable_observation();
        const auto response = api.handle(request);
        expect(response.error_size() == 1, "observation before a game is an error");
        expect(api.status() == SC2APIProtocol::launched, "rejected observation stays launched");
    }

    {
        Protocol api;
        auto request = mapCreate("Maps/Simple64.SC2Map", true);
        addPlayer(&request, SC2APIProtocol::Participant);
        expect(api.handle(request).status() == SC2APIProtocol::init_game, "realtime create");
        expect(api.handle(joinAs(SC2APIProtocol::Terran, false)).status() == SC2APIProtocol::in_game,
               "realtime join");
        SC2APIProtocol::Request step;
        step.mutable_step()->set_count(1);
        const auto response = api.handle(step);
        expect(response.error_size() == 1, "step is rejected in realtime");
        expect(api.status() == SC2APIProtocol::in_game, "rejected step stays in_game");
    }

    {
        Protocol api;
        SC2APIProtocol::Request request;
        request.mutable_quit();
        const auto response = api.handle(request);
        expect(response.has_quit(), "quit has a body");
        expect(response.status() == SC2APIProtocol::quit, "quit response status");
        expect(api.status() == SC2APIProtocol::quit, "quit status");
    }

    {
        Protocol api;
        SC2APIProtocol::Request request;
        request.mutable_save_map();
        const auto response = api.handle(request);
        expect(response.save_map().error() == SC2APIProtocol::ResponseSaveMap::InvalidMapData,
               "empty save_map");
        expect(api.status() == SC2APIProtocol::launched, "save_map does not change status");
    }

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
