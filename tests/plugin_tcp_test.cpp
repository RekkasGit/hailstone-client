// plugin/plugin_tcp.h: a plugin's own TCP connection (tcp_open ...) on the loopback: a name looked up off the
// caller's thread with what was sent meanwhile kept, and a numeric address.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/tcp_socket.h"
#include "plugin/plugin_tcp.h"

using namespace nmsc;
using platform::Endpoint;
using platform::TcpListener;
using platform::TcpSocket;
using plugin::PluginTcp;

namespace {

// Polls `ready` for up to 30 seconds; this only bounds a broken run.
template <typename F> bool eventually(F&& ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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

TEST_CASE("plugin tcp: a host name is looked up without waiting, and what was sent meanwhile goes out") {
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    const auto at = listener.local_endpoint();
    REQUIRE(at);
    auto conn = PluginTcp::open("localhost", at->port, error); // the hosts file's name, no name server
    REQUIRE(conn);
    CHECK(conn->send(as_bytes("hello")) == 5); // kept while the name is looked up or the connect goes on
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] {
        conn->state();
        return (server = listener.accept()).has_value();
    }));
    REQUIRE(eventually([&] { return conn->state() == 1; }));
    std::vector<std::byte> buf(64);
    std::size_t got = 0;
    REQUIRE(eventually([&] {
        conn->recv(std::span<std::byte>(buf.data(), 0)); // any call sends what waits
        auto n = server->recv(std::span<std::byte>(buf).subspan(got));
        if (n) got += *n;
        return got == 5;
    }));
    CHECK(std::string(reinterpret_cast<const char*>(buf.data()), 5) == "hello");
}

TEST_CASE("plugin tcp: a numeric address connects at once; a closed one reads -1") {
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    auto conn = PluginTcp::open("127.0.0.1", listener.local_endpoint()->port, error);
    REQUIRE(conn);
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] { return (server = listener.accept()).has_value(); }));
    REQUIRE(eventually([&] { return conn->state() == 1; }));
    server->close();
    std::vector<std::byte> buf(16);
    REQUIRE(eventually([&] { return conn->recv(buf) == -1; }));
    CHECK(conn->state() == -1);
    CHECK(conn->send(as_bytes("x")) == -1);
    CHECK_FALSE(conn->error().empty());
}
