#pragma once

// Non-blocking TCP connections, for the plugins' own services (the plugin SDK's tcp_* functions). With
// udp_socket.cpp the only place socket APIs are used (BSD sockets on Linux/macOS, Winsock on Windows).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "platform/udp_socket.h"

namespace nmsc::platform {

class TcpSocket {
public:
    enum class State : std::uint8_t { Closed, Connecting, Connected };

    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Starts connecting without waiting: false (and error set) when the socket cannot even be made.
    bool connect(const Endpoint& to, std::string& error);
    void close();

    // Connecting until the connection is made or refused; Closed after an error or the peer's close (last_error).
    State state();

    // The bytes the system took (0 while its buffer is full), or nullopt once the connection is closed.
    std::optional<std::size_t> send(std::span<const std::byte> data);
    // The bytes read (0 when nothing is waiting), or nullopt once the connection is closed and nothing is left.
    std::optional<std::size_t> recv(std::span<std::byte> buffer);

    const std::string& last_error() const { return error_; }

private:
    friend class TcpListener;
    void fail(const std::string& what);

    std::intptr_t handle_ = -1; // SOCKET on Windows is pointer-sized; int elsewhere. Stored widened.
    State state_ = State::Closed;
    std::string error_;
};

// A listening socket on one address (the tests' services).
class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // Binds and listens; port 0 picks a free one (local_endpoint says which).
    bool listen(const Endpoint& at, std::string& error);
    void close();
    std::optional<Endpoint> local_endpoint() const;
    // A waiting connection, already connected, or nullopt.
    std::optional<TcpSocket> accept();

private:
    std::intptr_t handle_ = -1;
};

} // namespace nmsc::platform
