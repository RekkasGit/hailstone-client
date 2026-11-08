// platform::TcpSocket and TcpListener on the loopback: a connection made without waiting, bytes both ways, the
// peer's close, and a refused connect.
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "platform/tcp_socket.h"

using namespace hailstone::platform;

namespace {

// Polls `ready` for up to ten seconds; this only bounds a broken run. Linux answers a loopback connect in microseconds,
// but Windows retries a refused one for about two seconds before it fails it.
template <typename F> bool eventually(F&& ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until) {
        if (ready()) return true;
        std::this_thread::yield();
    }
    return ready();
}

std::span<const std::byte> as_bytes(const std::string& s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

} // namespace

TEST_CASE("tcp socket: connects without waiting, carries bytes both ways and sees the peer's close") {
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    const auto at = listener.local_endpoint();
    REQUIRE(at);
    REQUIRE(at->port != 0);

    TcpSocket client;
    REQUIRE(client.connect(*at, error));
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] { return (server = listener.accept()).has_value(); }));
    REQUIRE(eventually([&] { return client.state() == TcpSocket::State::Connected; }));
    CHECK(server->state() == TcpSocket::State::Connected);

    CHECK(client.send(as_bytes("hello")) == 5u);
    std::vector<std::byte> buf(64);
    std::size_t got = 0;
    REQUIRE(eventually([&] {
        auto n = server->recv(buf);
        if (n) got += *n;
        return got == 5;
    }));
    CHECK(std::string(reinterpret_cast<const char*>(buf.data()), 5) == "hello");
    CHECK(server->recv(buf) == 0u); // nothing more yet

    server->close();
    REQUIRE(eventually([&] { return !client.recv(buf).has_value(); }));
    CHECK(client.state() == TcpSocket::State::Closed);
    CHECK_FALSE(client.last_error().empty());
    CHECK_FALSE(client.send(as_bytes("x")).has_value());
}

TEST_CASE("tcp socket: a connect nobody listens for ends closed") {
    // A port that was free a moment ago: the listener is gone again.
    std::uint16_t port = 0;
    {
        TcpListener l;
        std::string error;
        REQUIRE(l.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
        port = l.local_endpoint()->port;
    }
    TcpSocket s;
    std::string error;
    if (!s.connect(Endpoint::v4(127, 0, 0, 1, port), error)) {
        CHECK_FALSE(error.empty()); // refused at once
        return;
    }
    REQUIRE(eventually([&] { return s.state() != TcpSocket::State::Connecting; }));
    CHECK(s.state() == TcpSocket::State::Closed);
    CHECK_FALSE(s.last_error().empty());
}
