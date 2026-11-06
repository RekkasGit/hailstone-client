#pragma once

// Non-blocking UDP socket and name resolution. The only place socket APIs
// are used (BSD sockets on Linux/macOS, Winsock on Windows).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nmsc::platform {

// An IPv4 or IPv6 address and port, independent of the OS socket structs.
struct Endpoint {
    enum class Family : std::uint8_t { V4, V6 };

    Family family = Family::V4;
    std::array<std::uint8_t, 16> address{}; // V4 uses the first 4 bytes
    std::uint16_t port = 0;

    static Endpoint v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t port);

    // "1.2.3.4:5" or "[::1]:5".
    std::string to_string() const;
    // The address alone, without the port.
    std::string address_string() const;

    bool operator==(const Endpoint&) const = default;
};

// Resolves host (a name or numeric address) to endpoints, IPv4 first.
// Returns an empty list and sets error on failure.
std::vector<Endpoint> resolve(const std::string& host, std::uint16_t port, std::string& error);

// resolve() on a thread of its own; `done` is called on that thread with its result. A lookup still running when
// the program exits is waited for before the socket library shuts down (it is never cut off inside the resolver).
void resolve_async(std::string host, std::uint16_t port,
                   std::function<void(std::vector<Endpoint> endpoints, std::string error)> done);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Opens a non-blocking socket bound to an ephemeral port on all
    // interfaces of the family, or to bind_to when given.
    bool open(Endpoint::Family family, std::string& error, const Endpoint* bind_to = nullptr);
    void close();
    bool is_open() const;

    std::optional<Endpoint> local_endpoint() const;

    // Returns false on a hard error (the datagram is dropped either way on
    // a full send buffer, as UDP allows).
    bool send_to(const Endpoint& to, std::span<const std::byte> data);

    // Returns the datagram size, or nullopt when nothing is waiting or on
    // an error (see last_error()). A datagram larger than buffer is dropped
    // on every platform, not returned cut short.
    std::optional<std::size_t> recv_from(std::span<std::byte> buffer, Endpoint& from);

    // Waits until a datagram is readable or the timeout passes.
    bool wait_readable(std::chrono::milliseconds timeout) const;

    const std::string& last_error() const { return error_; }

private:
    // SOCKET on Windows is pointer-sized; int elsewhere. Stored widened.
    std::intptr_t handle_ = -1;
    std::string error_;
};

} // namespace nmsc::platform
