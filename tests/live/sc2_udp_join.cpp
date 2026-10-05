// One Base75689 client. Default ctest skips this. The argument selects the
// role Kitsune takes: "host" (default) or "guest". Either way the other role
// is the official client. A host answers ShowMeThisGame with the here-is
// saved by a guest run. On the base port Kitsune runs the handshake for its
// role (connect, reply, join, joined) and then only logs what arrives. Every
// datagram and every base-port record is logged with its description and the
// full hex. The lockstep and the game are not played.

#include "Kitsune/Net/BaseHandshake.h"
#include "Kitsune/Net/Describe.h"
#include "Kitsune/Net/GameSetup.h"
#include "Kitsune/Net/LocalEvent.h"
#include "Kitsune/Net/Lockstep.h"
#include "Kitsune/Net/KeepAlive.h"
#include "Kitsune/Net/MessageChannel.h"
#include "Kitsune/Net/SystemEvent.h"
#include "Kitsune/Net/TurnChannel.h"
#include "Kitsune/Net/UdpJoin.h"
#include "Kitsune/Version.h"

#include "../fixtures/base_port_capture.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <random>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "s2clientprotocol/sc2api.pb.h"

namespace {

// An environment variable, if set and not empty.
std::optional<std::string> environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string text(value);
    std::free(value);
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    std::string text(value);
#endif
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

#ifdef _WIN32
// The install root of a Windows 4.10 client to start directly, if one is named.
std::optional<std::string> windowsClientRoot() {
    return environment("KITSUNE_SC2_WINDOWS");
}
#endif

// Where this run keeps its files. A Windows client shares nothing with /tmp.
std::string tempFile(const char* name) {
#ifdef _WIN32
    if (windowsClientRoot()) {
        return (std::filesystem::temp_directory_path() / name).string();
    }
#endif
    return std::string("/tmp/") + name;
}

const std::string kHereIsPath = tempFile("kitsune-hereis.bin");
constexpr std::uint16_t kWebsocket = 8167;
constexpr std::uint16_t kServerGame = 27150;
constexpr std::uint16_t kServerBase = 27151;
constexpr std::uint16_t kClientGame = 27152;
constexpr std::uint16_t kClientBase = 27153;
// The race the capture's guest took with e_setLobbySlot.
constexpr std::uint8_t kGuestRace = 1;
constexpr const char* kSc2Root = "/mnt/c/Users/Shadow/Downloads/SC2.4.10/StarCraftII";

std::string capture(const std::string& command) {
    std::string output;
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return output;
    }
    char block[256];
    while (std::fgets(block, sizeof block, pipe) != nullptr) {
        output += block;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

// Base75689 and this process have to share a network namespace. The client's
// 127.0.0.1 is not the Windows host, so the live binary runs inside WSL.
class BaseClient {
public:
    BaseClient() = default;
    BaseClient(const BaseClient&) = delete;
    BaseClient& operator=(const BaseClient&) = delete;
    ~BaseClient() { stop(); }

    bool start() {
#ifdef _WIN32
        if (const auto root = windowsClientRoot()) {
            return startNative(*root);
        }
        const std::string launch = std::string("wsl.exe -e bash -lc \"") + "cd " + kSc2Root +
                                    " && export LD_LIBRARY_PATH=" + kSc2Root + "/Libs" +
                                    " && echo $$ >/tmp/kitsune-sc2.pid" +
                                    " && exec ./Versions/Base75689/SC2_x64" + " -listen 127.0.0.1 -port " +
                                    std::to_string(kWebsocket) + " -displayMode 0 -dataDir " + kSc2Root + "/" +
                                    " -dataVersion " + std::string(kitsune::kDataHash) +
                                    " >/tmp/kitsune-sc2.log 2>&1\"";
        thread_ = std::thread([launch] { std::system(launch.c_str()); });
        return true;
#else
        pid_ = ::fork();
        if (pid_ < 0) {
            return false;
        }
        if (pid_ == 0) {
            const int log = ::open("/tmp/kitsune-sc2.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (log >= 0) {
                ::dup2(log, STDOUT_FILENO);
                ::dup2(log, STDERR_FILENO);
                if (log > STDERR_FILENO) {
                    ::close(log);
                }
            }
            const std::string libs = std::string(kSc2Root) + "/Libs";
            ::setenv("LD_LIBRARY_PATH", libs.c_str(), 1);
            if (::chdir(kSc2Root) != 0) {
                _exit(127);
            }
            const std::string port = std::to_string(kWebsocket);
            const std::string dataDir = std::string(kSc2Root) + "/";
            const std::string dataVersion(kitsune::kDataHash);
            ::execl("./Versions/Base75689/SC2_x64", "SC2_x64", "-listen", "127.0.0.1", "-port",
                    port.c_str(), "-displayMode", "0", "-dataDir", dataDir.c_str(), "-dataVersion",
                    dataVersion.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        return true;
#endif
    }

    void stop() {
#ifdef _WIN32
        if (native_.hProcess != nullptr) {
            TerminateProcess(native_.hProcess, 0);
            WaitForSingleObject(native_.hProcess, 5000);
            CloseHandle(native_.hProcess);
            CloseHandle(native_.hThread);
            native_ = {};
            return;
        }
        if (!thread_.joinable()) {
            return;
        }
        std::system("wsl.exe -e bash -lc \"if [ -f /tmp/kitsune-sc2.pid ]; then kill $(cat "
                    "/tmp/kitsune-sc2.pid) 2>/dev/null || true; fi\"");
        thread_.join();
#else
        if (pid_ <= 0) {
            return;
        }
        ::kill(pid_, SIGTERM);
        for (int attempt = 0; attempt < 30; ++attempt) {
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) != 0) {
                pid_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ::kill(pid_, SIGKILL);
        ::waitpid(pid_, nullptr, 0);
        pid_ = -1;
#endif
    }

    [[nodiscard]] std::string logTail() const {
#ifdef _WIN32
        if (windowsClientRoot()) {
            std::ifstream in(tempFile("kitsune-sc2.log"));
            std::vector<std::string> lines;
            for (std::string line; std::getline(in, line);) {
                lines.push_back(line);
            }
            std::string out;
            for (std::size_t i = lines.size() > 60 ? lines.size() - 60 : 0; i < lines.size(); ++i) {
                out += lines[i] + '\n';
            }
            return out;
        }
        return capture("wsl.exe -e bash -lc \"tail -n 60 /tmp/kitsune-sc2.log\"");
#else
        return capture("tail -n 60 /tmp/kitsune-sc2.log");
#endif
    }

private:
#ifdef _WIN32
    // The Windows 4.10 client started directly, so it shares this process's
    // loopback. The install root comes from KITSUNE_SC2_WINDOWS.
    bool startNative(const std::string& root) {
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        const auto logPath = tempFile("kitsune-sc2.log");
        HANDLE log = CreateFileA(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) {
            return false;
        }
        std::string forward = root;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        if (forward.back() != '/') {
            forward += '/';
        }
        std::string command = "\"" + root + "\\Versions\\Base75689\\SC2_x64.exe\" -listen 127.0.0.1 -port " +
                              std::to_string(kWebsocket) + " -displayMode 0 -dataDir \"" + forward +
                              "\" -dataVersion " + std::string(kitsune::kDataHash);
        STARTUPINFOA startup{};
        startup.cb = sizeof startup;
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = log;
        startup.hStdError = log;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        const std::string directory = root + "\\Support64";
        const BOOL made = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                         directory.c_str(), &startup, &native_);
        CloseHandle(log);
        return made != FALSE;
    }

    std::thread thread_;
    PROCESS_INFORMATION native_{};
#else
    pid_t pid_ = -1;
#endif
};

class ApiSocket {
public:
    bool connect(const std::string& host, std::uint16_t port) {
        std::error_code error;
        const auto address = asio::ip::make_address(host, error);
        if (error) {
            return false;
        }
        socket_.connect({address, port}, error);
        return !error;
    }

    bool openProtocol() {
        const std::string request =
            "GET /sc2api HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        std::error_code error;
        asio::write(socket_, asio::buffer(request), error);
        if (error) {
            return false;
        }
        std::string header;
        while (header.find("\r\n\r\n") == std::string::npos && header.size() < 4096) {
            char byte = 0;
            asio::read(socket_, asio::buffer(&byte, 1), error);
            if (error) {
                return false;
            }
            header.push_back(byte);
        }
        return header.find(" 101 ") != std::string::npos;
    }

    bool sendBinary(const std::string& payload) {
        std::vector<unsigned char> frame;
        frame.push_back(0x82);
        const auto size = payload.size();
        if (size < 126) {
            frame.push_back(static_cast<unsigned char>(0x80 | size));
        } else if (size <= 0xffff) {
            frame.push_back(0x80 | 126);
            frame.push_back(static_cast<unsigned char>((size >> 8) & 0xff));
            frame.push_back(static_cast<unsigned char>(size & 0xff));
        } else {
            return false;
        }
        const unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
        frame.insert(frame.end(), mask, mask + 4);
        for (std::size_t i = 0; i < size; ++i) {
            frame.push_back(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]);
        }
        std::error_code error;
        asio::write(socket_, asio::buffer(frame), error);
        return !error;
    }

    std::optional<std::string> recvBinary() {
        std::error_code error;
        unsigned char header[2] = {};
        asio::read(socket_, asio::buffer(header), error);
        if (error) {
            return std::nullopt;
        }
        const unsigned opcode = header[0] & 0x0f;
        std::uint64_t length = header[1] & 0x7f;
        if (length == 126) {
            unsigned char extended[2] = {};
            asio::read(socket_, asio::buffer(extended), error);
            if (error) {
                return std::nullopt;
            }
            length = (static_cast<std::uint64_t>(extended[0]) << 8) | extended[1];
        } else if (length == 127) {
            unsigned char extended[8] = {};
            asio::read(socket_, asio::buffer(extended), error);
            if (error) {
                return std::nullopt;
            }
            length = 0;
            for (const auto byte : extended) {
                length = (length << 8) | byte;
            }
        }
        std::string payload(static_cast<std::size_t>(length), '\0');
        if (length > 0) {
            asio::read(socket_, asio::buffer(payload), error);
            if (error) {
                return std::nullopt;
            }
        }
        if (opcode == 0x9) {
            return recvBinary();
        }
        if (opcode != 0x2 && opcode != 0x1) {
            return std::nullopt;
        }
        return payload;
    }

private:
    asio::io_context io_{};
    asio::ip::tcp::socket socket_{io_};
};

bool waitForPort(std::uint16_t port, std::chrono::seconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        ApiSocket probe;
        if (probe.connect("127.0.0.1", port)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    return false;
}

std::string hex(const std::vector<std::byte>& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const std::byte byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        if (!out.empty()) {
            out.push_back(' ');
        }
        out.push_back(kDigits[value >> 4]);
        out.push_back(kDigits[value & 0x0f]);
    }
    return out;
}

SC2APIProtocol::Request createGame() {
    SC2APIProtocol::Request request;
    auto* create = request.mutable_create_game();
    create->mutable_local_map()->set_map_path("Melee/Simple64.SC2Map");
    create->add_player_setup()->set_type(SC2APIProtocol::Participant);
    create->add_player_setup()->set_type(SC2APIProtocol::Participant);
    // A stepped game waits for the API client to call step. Nothing here does.
    create->set_realtime(true);
    return request;
}

SC2APIProtocol::Request joinGame(const std::string& hostIp) {
    SC2APIProtocol::Request request;
    auto* join = request.mutable_join_game();
    join->set_race(SC2APIProtocol::Terran);
    join->mutable_options()->set_raw(true);
    join->set_host_ip(hostIp);
    join->mutable_server_ports()->set_game_port(kServerGame);
    join->mutable_server_ports()->set_base_port(kServerBase);
    auto* client = join->add_client_ports();
    client->set_game_port(kClientGame);
    client->set_base_port(kClientBase);
    return request;
}

bool writeBytes(const char* path, const std::vector<std::byte>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::optional<std::vector<std::byte>> readBytes(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = static_cast<std::byte>(raw[i]);
    }
    return bytes;
}

void dumpHereIs(const kitsune::net::HereIsThisGameEvent& event) {
    const auto& game = event.gameDescription;
    const auto& options = game.gameOptions;
    std::cout << "here-is secret=" << event.sessionSecret << " creator=" << event.creatorName
              << " address version=" << static_cast<unsigned>(event.sessionAddress.version)
              << " port=" << event.sessionAddress.port << " words=";
    for (const auto word : event.sessionAddress.address) {
        std::cout << word << ',';
    }
    std::cout << "\nmap=" << game.mapFileName << " checksum=" << game.mapFileSyncChecksum
              << " author=" << game.mapAuthorName << " cache=" << game.gameCacheName
              << " size=" << static_cast<unsigned>(game.mapSizeX) << 'x'
              << static_cast<unsigned>(game.mapSizeY)
              << " speed=" << static_cast<unsigned>(game.gameSpeed)
              << " type=" << static_cast<unsigned>(game.gameType)
              << " descUsers=" << static_cast<unsigned>(game.maxUsers)
              << " descObservers=" << static_cast<unsigned>(game.maxObservers)
              << " players=" << static_cast<unsigned>(game.maxPlayers)
              << " teams=" << static_cast<unsigned>(game.maxTeams)
              << " colors=" << static_cast<unsigned>(game.maxColors)
              << " races=" << static_cast<unsigned>(game.maxRaces)
              << " controls=" << static_cast<unsigned>(game.maxControls) << " random=" << game.randomValue
              << " mod=" << game.modFileSyncChecksum << " slots=" << game.slotDescriptions.size()
              << " handles=" << game.cacheHandles.size()
              << " difficulty=" << static_cast<unsigned>(game.defaultDifficulty)
              << " ai=" << static_cast<unsigned>(game.defaultAIBuild) << " ext=" << game.hasExtensionMod
              << " nonblizz=" << game.hasNonBlizzardExtensionMod << " blizz=" << game.isBlizzardMap
              << " ffa=" << game.isPremadeFFA << " coop=" << game.isCoopMode
              << " realtime=" << game.isRealtimeMode << "\noptions lock=" << options.lockTeams
              << " together=" << options.teamsTogether << " shared=" << options.advancedSharedControl
              << " randomRaces=" << options.randomRaces << " bnet=" << options.battleNet
              << " amm=" << options.amm << " competitive=" << options.competitive
              << " practice=" << options.practice << " coop=" << options.cooperative
              << " noVictory=" << options.noVictoryOrDefeat << " heroes=" << options.heroDuplicatesAllowed
              << " fog=" << static_cast<unsigned>(options.fog)
              << " observers=" << static_cast<unsigned>(options.observers)
              << " userDifficulty=" << static_cast<unsigned>(options.userDifficulty)
              << " debug=" << options.clientDebugFlags << " coach=" << options.buildCoachEnabled
              << "\ninitial " << event.userInitialData.size() << '\n';
    for (const auto& slot : game.slotDescriptions) {
        std::cout << " slot colors=" << slot.allowedColors.size() << " races=" << slot.allowedRaces.size()
                  << " difficulty=" << slot.allowedDifficulty.size()
                  << " controls=" << slot.allowedControls.size()
                  << " observe=" << slot.allowedObserveTypes.size()
                  << " ai=" << slot.allowedAIBuilds.size() << '\n';
    }
    for (const auto& user : event.userInitialData) {
        std::cout << " user name=" << user.name << " observe=" << static_cast<unsigned>(user.observe)
                  << " hero=" << user.hero << " toon=" << user.toonHandle << '\n';
    }
}

// Listens on this role's base port. A guest also dials the host base port.
// Reads the stream with decodeSystemStream, runs the handshake for the role
// (docs/tcp-base.md, "Session order"), and writes the replies. Turns and
// everything after the join are only logged: the lockstep is not built yet.
class BaseProbe {
public:
    BaseProbe(std::uint16_t listenPort, std::uint16_t dialPort)
        : listenPort_(listenPort), dialPort_(dialPort) {}
    BaseProbe(const BaseProbe&) = delete;
    BaseProbe& operator=(const BaseProbe&) = delete;
    ~BaseProbe() { finish(); }

    void start() { thread_ = std::thread([this] { run(); }); }

    // Host role: answer a guest that connects.
    void useHost(kitsune::net::HostParams params) {
        std::lock_guard<std::mutex> lock(mu_);
        hostPeerId_ = params.peerId;
        hostJoined_ = params.joined;
        host_.emplace(std::move(params));
    }

    // Guest role: dial the host and connect. Waits for this call, because the
    // session id and secret come from the here-is.
    void useGuest(kitsune::net::GuestParams params) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            guestPeerId_ = params.peerId;
            guest_.emplace(std::move(params));
        }
        armed_.store(true);
    }

    void finish() {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void report() const {
        std::lock_guard<std::mutex> lock(mu_);
        std::cout << "base " << status_ << '\n';
        if (host_) {
            std::cout << "handshake host " << stateName(host_->state()) << '\n';
        }
        if (guest_) {
            std::cout << "handshake guest " << stateName(guest_->state()) << '\n';
        }
        if (turns_) {
            std::cout << "turns received " << turns_->turnsReceived() << ", next expected "
                      << turns_->nextExpected() << ", next to send " << turns_->nextToSend()
                      << ", peer event bytes " << turns_->peerEventBytes() << ", events "
                      << turns_->eventsReceived() << ", undecoded turns " << turns_->undecodedTurns() << '\n';
        }
        if (messages_) {
            std::cout << "messages received " << messages_->received().size() << '\n';
        }
        if (setup_) {
            std::cout << "setup " << setupName(setup_->phase()) << '\n';
        }
        for (const auto& line : lines_) {
            std::cout << line << '\n';
        }
    }

    [[nodiscard]] std::size_t turnsReceived() const {
        std::lock_guard<std::mutex> lock(mu_);
        return turns_ ? turns_->turnsReceived() : 0;
    }

    [[nodiscard]] bool joined() const {
        std::lock_guard<std::mutex> lock(mu_);
        return (host_ && host_->state() == kitsune::net::HandshakeState::Joined) ||
               (guest_ && guest_->state() == kitsune::net::HandshakeState::Joined);
    }

private:
    static const char* stateName(kitsune::net::HandshakeState state) {
        using kitsune::net::HandshakeState;
        switch (state) {
        case HandshakeState::Idle: return "idle";
        case HandshakeState::AwaitReply: return "awaiting reply";
        case HandshakeState::AwaitJoined: return "awaiting join result";
        case HandshakeState::AwaitConnect: return "awaiting connect";
        case HandshakeState::AwaitJoin: return "awaiting join";
        case HandshakeState::Joined: return "joined";
        case HandshakeState::Failed: return "failed";
        }
        return "?";
    }

    void note(std::string text) {
        std::lock_guard<std::mutex> lock(mu_);
        status_ += text;
    }

    void log(const std::string& line) {
        std::cout << line << '\n';
        std::lock_guard<std::mutex> lock(mu_);
        if (lines_.size() < 400) {
            lines_.push_back(line);
        }
    }

    void send(asio::ip::tcp::socket& socket, const std::vector<std::byte>& out) {
        if (out.empty()) {
            return;
        }
        std::error_code error;
        std::size_t done = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (done < out.size() && std::chrono::steady_clock::now() < deadline) {
            done += socket.write_some(asio::buffer(out.data() + done, out.size() - done), error);
            if (error && error != asio::error::would_block) {
                break;
            }
        }
        log("sent " + kitsune::net::describeTcpPayload(out) + " bytes " + hex(out));
    }

    // Called with mu_ held, as the channel's handler runs inside onMessage.
    void armSetup(kitsune::net::GameSetup setup) {
        setup_.emplace(std::move(setup));
        turns_->setEventHandler([this](std::uint16_t sequence, std::span<const kitsune::net::game::Event> events) {
            for (const auto& event : events) {
                const std::string line = "turn " + std::to_string(sequence) + " event " +
                                         kitsune::net::game::eventName(kitsune::net::game::eventId(event));
                std::cout << line << '\n';
                if (lines_.size() < 400) {
                    lines_.push_back(line);
                }
            }
            const auto before = setup_->phase();
            auto out = setup_->onTurn(sequence, events);
            if (setup_->phase() != before || !out.empty()) {
                const std::string line = "setup " + std::string(setupName(setup_->phase())) + ", sending " +
                                         std::to_string(out.size()) + " events";
                std::cout << line << '\n';
                if (lines_.size() < 400) {
                    lines_.push_back(line);
                }
            }
            return out;
        });
    }

    static const char* setupName(kitsune::net::SetupPhase phase) {
        using kitsune::net::SetupPhase;
        switch (phase) {
        case SetupPhase::Lobby: return "lobby";
        case SetupPhase::Starting: return "starting";
        case SetupPhase::Loading: return "loading";
        case SetupPhase::Playing: return "playing";
        }
        return "?";
    }

    void handle(asio::ip::tcp::socket& socket, const kitsune::net::SystemEvent& message) {
        using kitsune::net::HandshakeState;
        std::vector<std::byte> out;
        std::vector<std::vector<std::byte>> more;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (turns_) {
                // Past the join: turns are answered by the turn channel, messages by theirs.
                more = turns_->onMessage(message);
                if (auto pong = kitsune::net::answerPing(message, host_ ? hostPeerId_ : guestPeerId_); !pong.empty()) {
                    more.push_back(std::move(pong));
                }
                const auto before = messages_->received().size();
                for (auto& ack : messages_->onMessage(message)) {
                    more.push_back(std::move(ack));
                }
                // Experiment: an official peer sends the same blobs at the same steps.
                if (messages_->received().size() > before) {
                    more.push_back(messages_->send(messages_->received().back()));
                }
            } else if (host_) {
                std::error_code error;
                const auto peer = socket.remote_endpoint(error);
                const auto octets = !error && peer.address().is_v4()
                                        ? peer.address().to_v4().to_bytes()
                                        : std::array<unsigned char, 4>{127, 0, 0, 1};
                out = host_->onMessage(message, {octets[0], octets[1], octets[2], octets[3]}).out;
            } else if (guest_) {
                out = guest_->onMessage(message).out;
            }
        }
        send(socket, out);
        // The handshake answer goes first. Then the turn channel takes over.
        if (!out.empty()) {
            std::lock_guard<std::mutex> lock(mu_);
            if (host_ && host_->state() == HandshakeState::Joined && host_->guest()) {
                turns_.emplace(kitsune::net::TurnChannel::host(hostPeerId_, hostJoined_, *host_->guest()));
                messages_.emplace(hostPeerId_);
                armSetup(kitsune::net::GameSetup::host({}));
                more = turns_->start();
            } else if (guest_ && guest_->state() == HandshakeState::Joined && guest_->joined()) {
                turns_.emplace(kitsune::net::TurnChannel::guest(guestPeerId_, *guest_->joined()));
                messages_.emplace(guestPeerId_);
                kitsune::net::SetupParams params;
                params.firstTurn = guest_->joined()->joinSequence;
                params.race = kGuestRace;
                armSetup(kitsune::net::GameSetup::guest(params));
            }
        }
        for (const auto& record : more) {
            send(socket, record);
        }
    }

    // Read what is there, decode every whole record, keep the rest.
    void pull(asio::ip::tcp::socket& socket, std::vector<std::byte>& buffer, const char* tag) {
        std::error_code error;
        std::byte chunk[1460];
        const auto count = socket.read_some(asio::buffer(chunk), error);
        if (error || count == 0) {
            return;
        }
        buffer.insert(buffer.end(), chunk, chunk + count);
        for (;;) {
            const auto got = kitsune::net::decodeSystemStream(buffer);
            if (got.status == kitsune::net::StreamStatus::Truncated) {
                if (buffer.size() >= kitsune::net::kStreamBufferBytes) {
                    log(std::string(tag) + "buffer full without a record; the client would close");
                    buffer.clear();
                }
                return;
            }
            if (got.status == kitsune::net::StreamStatus::Bad) {
                log(std::string(tag) + "bad record, " + std::to_string(buffer.size()) + " bytes dropped");
                buffer.clear();
                return;
            }
            const std::vector<std::byte> record(buffer.begin(),
                                                buffer.begin() + static_cast<std::ptrdiff_t>(got.consumed));
            log(std::string(tag) + kitsune::net::describeSystemEvent(*got.event) + " bytes " +
                hex(record));
            handle(socket, *got.event);
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got.consumed));
        }
    }

    void run() {
        asio::io_context io;
        std::error_code error;
        asio::ip::tcp::acceptor acceptor(io);
        acceptor.open(asio::ip::tcp::v4(), error);
        if (!error) {
            acceptor.set_option(asio::socket_base::reuse_address(true), error);
        }
        if (!error) {
            acceptor.bind({asio::ip::tcp::v4(), listenPort_}, error);
        }
        if (!error) {
            acceptor.listen(1, error);
            acceptor.non_blocking(true);
        }
        if (error) {
            note("listen failed " + error.message() + "; ");
        } else {
            note("listening " + std::to_string(listenPort_) + "; ");
        }

        asio::ip::tcp::socket inbound(io);
        asio::ip::tcp::socket outbound(io);
        std::vector<std::byte> inboundBuffer;
        std::vector<std::byte> outboundBuffer;
        bool inboundUp = false;
        bool outboundUp = dialPort_ == 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!stop_.load() && std::chrono::steady_clock::now() < deadline) {
            if (!outboundUp && armed_.load()) {
                outbound.close(error);
                outbound = asio::ip::tcp::socket(io);
                outbound.connect({asio::ip::make_address("127.0.0.1"), dialPort_}, error);
                if (!error) {
                    outbound.non_blocking(true);
                    outboundUp = true;
                    note("dialed " + std::to_string(dialPort_) + "; ");
                    std::vector<std::byte> first;
                    {
                        std::lock_guard<std::mutex> lock(mu_);
                        first = guest_->start().out;
                    }
                    send(outbound, first);
                }
            } else if (outboundUp && dialPort_ != 0) {
                pull(outbound, outboundBuffer, "dialed ");
            }
            if (!inboundUp && acceptor.is_open()) {
                acceptor.accept(inbound, error);
                if (!error) {
                    inbound.non_blocking(true);
                    inboundUp = true;
                    note("accepted; ");
                }
            } else if (inboundUp) {
                pull(inbound, inboundBuffer, "accepted ");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    std::uint16_t listenPort_ = 0;
    std::uint16_t dialPort_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> armed_{false};
    mutable std::mutex mu_;
    std::string status_;
    std::vector<std::string> lines_;
    std::optional<kitsune::net::HostHandshake> host_;
    std::optional<kitsune::net::GuestHandshake> guest_;
    std::optional<kitsune::net::TurnChannel> turns_;
    std::optional<kitsune::net::GameSetup> setup_;
    std::optional<kitsune::net::MessageChannel> messages_;
    std::uint32_t hostPeerId_ = 0;
    std::uint32_t guestPeerId_ = 0;
    kitsune::net::JoinSucceededEvent hostJoined_{};
};

std::uint32_t randomWord() {
    static std::mt19937 generator{std::random_device{}()};
    return generator();
}

// The guest side of the join: the host's here-is names the session and the
// secret to present. Everything else is ours.
kitsune::net::GuestParams guestParamsFrom(const kitsune::net::HereIsThisGameEvent& here) {
    kitsune::net::GuestParams params;
    params.peerId = randomWord();
    params.secret = randomWord();
    params.sessionId = here.sessionId;
    params.hostSecret = here.sessionSecret;
    params.listenPort = kClientBase;
    params.programId = here.header.programId;
    params.version = here.header.version;
    params.user.name = "kitsune";
    params.user.randomSeed = randomWord();
    return params;
}

// The host side. The lobby the guest is told about is the one an official host
// sent in the capture, with this session's identity put in. The slots are not
// taken from the here-is, so this only suits a 1v1 melee map.
kitsune::net::HostParams hostParamsFrom(const kitsune::net::HereIsThisGameEvent& here) {
    const auto captured = kitsune::net::decodeSystemEvent(
        {reinterpret_cast<const std::byte*>(kitsune::fixtures::kJoinSucceeded),
         sizeof kitsune::fixtures::kJoinSucceeded});
    kitsune::net::HostParams params;
    params.peerId = randomWord();
    params.sessionId = here.sessionId;
    params.sessionSecret = here.sessionSecret;
    params.programId = here.header.programId;
    params.version = here.header.version;
    if (captured) {
        params.joined = std::get<kitsune::net::JoinSucceededEvent>(*captured);
    }
    for (auto& user : params.joined.users) {
        user.info.sessionId = here.sessionId;
        user.info.peerId = params.peerId;
        user.info.sessionSecret = here.sessionSecret;
        user.info.externalAddress.port = kServerBase;
        user.info.userData.name = "kitsune";
    }
    params.joined.lobbyState.maxUsers = here.gameDescription.maxUsers;
    params.joined.lobbyState.maxObservers = here.gameDescription.maxObservers;
    return params;
}

bool roundTrip(ApiSocket& api, const SC2APIProtocol::Request& request, SC2APIProtocol::Response& response) {
    if (!api.sendBinary(request.SerializeAsString())) {
        return false;
    }
    const auto payload = api.recvBinary();
    return payload.has_value() && response.ParseFromString(*payload);
}

void dumpUdp(const kitsune::net::UdpJoin& join) {
    for (const auto& inbound : join.inbound()) {
        const auto& address = inbound.from.address;
        std::cout << "udp " << static_cast<int>(address[12]) << '.' << static_cast<int>(address[13]) << '.'
                  << static_cast<int>(address[14]) << '.' << static_cast<int>(address[15]) << ':'
                  << inbound.from.port << ' ' << inbound.bytes.size();
        if (inbound.record) {
            std::cout << ' ' << kitsune::net::describeLocalEvent(*inbound.record);
        } else if (const auto here = kitsune::net::decodeHereIs(inbound.bytes)) {
            std::cout << ' ' << kitsune::net::describeHereIs(*here);
        }
        std::cout << " bytes " << hex(inbound.bytes) << '\n';
    }
}

// Prefer a later copy once sessionAddress is filled in. The first here-is
// from a 4.10 host has those words zero and a later copy carries the host.
const std::vector<std::byte>* newestHereIs(const kitsune::net::UdpJoin& join) {
    const std::vector<std::byte>* chosen = nullptr;
    bool chosenHasAddress = false;
    for (const auto& inbound : join.inbound()) {
        const auto here = kitsune::net::decodeHereIs(inbound.bytes);
        if (!here) {
            continue;
        }
        const bool hasAddress = !here->sessionAddress.address.empty() || here->sessionAddress.port != 0;
        if (chosen == nullptr || hasAddress == chosenHasAddress || (hasAddress && !chosenHasAddress)) {
            chosen = &inbound.bytes;
            chosenHasAddress = hasAddress;
        }
    }
    return chosen;
}

} // namespace

int main(int argc, char** argv) {
    // The log is redirected while this runs. Flush each line so a stuck
    // client still leaves the bytes already received.
    std::cout << std::unitbuf;
    const bool guestRole = argc > 1 && std::string(argv[1]) == "guest";
    std::cout << "role " << (guestRole ? "guest" : "host") << '\n';
    // The official client looks for the host at this address inside its own
    // network namespace.
    const std::string hostIp = "127.0.0.1";
    std::cout << "host ip " << hostIp << '\n';

    std::optional<std::vector<std::byte>> captured;
    std::optional<kitsune::net::HereIsThisGameEvent> savedHere;
    kitsune::net::IHaveThisGameEvent have;
    if (!guestRole) {
        captured = readBytes(kHereIsPath.c_str());
        if (!captured) {
            std::cerr << "missing " << kHereIsPath << " from a guest run\n";
            return EXIT_FAILURE;
        }
        const auto here = kitsune::net::decodeHereIs(*captured);
        if (!here) {
            std::cerr << "saved here-is did not decode\n";
            return EXIT_FAILURE;
        }
        savedHere = *here;
        dumpHereIs(*here);
        std::vector<std::byte> again(8192);
        const auto encoded = kitsune::net::encodeHereIs(*here, again);
        const bool same = encoded && *encoded == captured->size() &&
                          std::equal(captured->begin(), captured->end(), again.begin());
        std::cout << "here-is round trip " << (same ? "match" : "differs") << '\n';
        have.header = here->header;
        have.sessionId = here->sessionId;
        have.numUsers = here->numUsers;
        have.maxUsers = here->maxUsers;
        have.numObservers = here->numObservers;
        have.maxObservers = here->maxObservers;
    }

    BaseClient client;
    if (!client.start()) {
        std::cerr << "could not start SC2\n";
        return EXIT_FAILURE;
    }

    if (!waitForPort(kWebsocket, std::chrono::seconds(90))) {
        std::cerr << "SC2 did not listen on " << kWebsocket << '\n';
        std::cerr << client.logTail() << '\n';
        return EXIT_FAILURE;
    }

    ApiSocket api;
    if (!api.connect("127.0.0.1", kWebsocket) || !api.openProtocol()) {
        std::cerr << "websocket upgrade failed\n";
        std::cerr << client.logTail() << '\n';
        return EXIT_FAILURE;
    }

    if (guestRole) {
        SC2APIProtocol::Response created;
        if (!roundTrip(api, createGame(), created) || created.create_game().has_error()) {
            std::cerr << "create_game failed\n" << created.DebugString() << '\n';
            std::cerr << client.logTail() << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "create_game status " << created.status() << '\n';
    }

    const auto role = guestRole ? kitsune::net::UdpJoinRole::Guest : kitsune::net::UdpJoinRole::Host;
    kitsune::net::UdpJoin join(role);
    join.setLog([](std::string_view line) { std::cout << line << '\n'; });
    const auto gamePort = guestRole ? kClientGame : kServerGame;
    if (!join.bind(gamePort)) {
        std::cerr << "could not bind game port " << gamePort << '\n';
        return EXIT_FAILURE;
    }
    if (!guestRole) {
        join.useIHave(have);
        join.useHereIsDatagram(*captured);
    }

    // Host accepts on its base port. Guest dials that port and listens on its own.
    BaseProbe base(guestRole ? kClientBase : kServerBase, guestRole ? kServerBase : 0);
    if (savedHere) {
        base.useHost(hostParamsFrom(*savedHere));
    }
    base.start();

    // Join blocks inside the client until the UDP exchange finishes.
    std::thread joinRequest([&] {
        if (!api.sendBinary(joinGame(hostIp).SerializeAsString())) {
            std::cerr << "join send failed\n";
        }
    });

    const auto remote = kitsune::net::udpLoopback(kClientGame);
    const auto status = join.exchange(guestRole ? nullptr : &remote, std::chrono::seconds(20));
    // A guest connects as soon as the host's here-is names the session. The
    // base thread dials after useGuest and then runs the handshake.
    bool armed = false;
    // Half-second slices after the exchange. KITSUNE_LIVE_SECONDS lets a game run longer.
    int slices = 30;
    if (const auto seconds = environment("KITSUNE_LIVE_SECONDS")) {
        slices = std::max(1, std::atoi(seconds->c_str()) * 2);
    }
    for (int slice = 0; slice < slices; ++slice) {
        join.collect(std::chrono::milliseconds(500));
        if (guestRole && !armed) {
            if (const auto* bytes = newestHereIs(join)) {
                if (const auto here = kitsune::net::decodeHereIs(*bytes)) {
                    base.useGuest(guestParamsFrom(*here));
                    armed = true;
                }
            }
        }
    }
    dumpUdp(join);
    if (guestRole) {
        if (const auto* bytes = newestHereIs(join)) {
            if (!writeBytes(kHereIsPath.c_str(), *bytes)) {
                std::cerr << "could not save here-is\n";
            } else if (const auto here = kitsune::net::decodeHereIs(*bytes)) {
                std::cout << "saved here-is " << bytes->size() << '\n';
                dumpHereIs(*here);
            }
        }
    }
    joinRequest.join();
    if (const auto out = environment("KITSUNE_REPLAY_OUT")) {
        // The join answer comes first. Then ask the official client to write the replay.
        (void)api.recvBinary();
        SC2APIProtocol::Request request;
        request.mutable_save_replay();
        SC2APIProtocol::Response saved;
        if (roundTrip(api, request, saved) && !saved.save_replay().data().empty()) {
            const auto& data = saved.save_replay().data();
            std::ofstream file(*out, std::ios::binary | std::ios::trunc);
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            std::cout << "replay saved " << data.size() << " bytes to " << *out << '\n';
        } else {
            std::cout << "save_replay failed " << saved.ShortDebugString() << '\n';
        }
    }
    base.finish();
    base.report();

    if (status != kitsune::net::UdpJoinStatus::Completed) {
        std::cerr << "UDP join did not complete\n";
        std::cerr << client.logTail() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "UDP join completed\n";
    std::cout << "base handshake " << (base.joined() ? "joined" : "not joined") << '\n';
    return EXIT_SUCCESS;
}
