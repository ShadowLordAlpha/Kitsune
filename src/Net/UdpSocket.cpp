#include "Kitsune/Net/UdpSocket.h"

#include <cstring>

#include <asio.hpp>

#ifndef _WIN32
#include <poll.h>
#include <sys/socket.h>
#endif

namespace kitsune::net {
namespace {

asio::ip::udp::endpoint toEndpoint(const UdpPeer& peer) {
    const asio::ip::address_v4::bytes_type bytes{peer.address[12], peer.address[13], peer.address[14],
                                                 peer.address[15]};
    return {asio::ip::address_v4(bytes), peer.port};
}

UdpPeer fromEndpoint(const asio::ip::udp::endpoint& endpoint) {
    UdpPeer peer;
    peer.address[10] = 0xff;
    peer.address[11] = 0xff;
    const auto bytes = endpoint.address().to_v4().to_bytes();
    std::memcpy(peer.address.data() + 12, bytes.data(), bytes.size());
    peer.port = endpoint.port();
    return peer;
}

} // namespace

UdpPeer udpLoopback(std::uint16_t port) noexcept {
    UdpPeer peer;
    peer.address[10] = 0xff;
    peer.address[11] = 0xff;
    peer.address[12] = 127;
    peer.address[15] = 1;
    peer.port = port;
    return peer;
}

struct UdpSocket::State {
    asio::io_context io;
    asio::ip::udp::socket socket;
    std::uint16_t port = 0;
    bool opened = false;

    State() : socket(io) {}
};

UdpSocket::UdpSocket() : state_(std::make_unique<State>()) {}

UdpSocket::~UdpSocket() = default;

UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;

UdpSocket& UdpSocket::operator=(UdpSocket&&) noexcept = default;

bool UdpSocket::open() {
    if (state_->opened) {
        return true;
    }
    std::error_code error;
    state_->socket.open(asio::ip::udp::v4(), error);
    if (error) {
        return false;
    }
    state_->socket.set_option(asio::socket_base::reuse_address(true), error);
    state_->opened = true;
    return true;
}

bool UdpSocket::bind(std::uint16_t port) {
    if (!open()) {
        return false;
    }
    std::error_code error;
    state_->socket.bind({asio::ip::udp::v4(), port}, error);
    if (error) {
        return false;
    }
    const auto local = state_->socket.local_endpoint(error);
    if (error || local.port() == 0) {
        return false;
    }
    state_->port = local.port();
    return true;
}

std::uint16_t UdpSocket::localPort() const noexcept {
    return state_->port;
}

bool UdpSocket::sendTo(const UdpPeer& peer, std::span<const std::byte> payload) {
    if (!state_->opened || payload.size() > 2048) {
        return false;
    }
    std::error_code error;
    const auto sent = state_->socket.send_to(asio::buffer(payload.data(), payload.size()),
                                              toEndpoint(peer), 0, error);
    return !error && sent == payload.size();
}

std::optional<UdpSocket::Packet> UdpSocket::recvFrom(std::chrono::milliseconds timeout) {
    if (!state_->opened) {
        return std::nullopt;
    }
    // Asio marks the socket non-blocking and waits in poll(). SO_RCVTIMEO
    // does not bound that wait, so a host with no datagram yet never
    // retransmits. poll's timeout is the bound the exchange deadline uses.
#ifdef _WIN32
    WSAPOLLFD ready{};
    ready.fd = static_cast<SOCKET>(state_->socket.native_handle());
    ready.events = POLLIN;
    const int waited = ::WSAPoll(&ready, 1, static_cast<int>(timeout.count()));
#else
    pollfd ready{};
    ready.fd = state_->socket.native_handle();
    ready.events = POLLIN;
    const int waited = ::poll(&ready, 1, static_cast<int>(timeout.count()));
#endif
    if (waited <= 0) {
        return std::nullopt;
    }
    std::array<std::byte, 2048> buffer{};
    asio::ip::udp::endpoint sender;
    std::error_code error;
    const auto got = state_->socket.receive_from(asio::buffer(buffer), sender, 0, error);
    if (error || got == 0) {
        return std::nullopt;
    }

    Packet packet;
    packet.bytes.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got));
    packet.from = fromEndpoint(sender);
    return packet;
}

} // namespace kitsune::net
