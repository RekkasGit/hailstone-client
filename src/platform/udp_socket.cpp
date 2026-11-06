#include "platform/udp_socket.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace nmsc::platform {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
const NativeSocket kInvalid = INVALID_SOCKET;
using SockLen = int;

struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockInit() {
        if (ok) WSACleanup();
    }
};

bool ensure_init() {
    static WinsockInit init;
    return init.ok;
}

int last_socket_error() { return WSAGetLastError(); }
bool would_block(int e) { return e == WSAEWOULDBLOCK; }
// An ICMP port-unreachable reply surfaces as WSAECONNRESET on the next
// recvfrom; for UDP it is not a socket error.
bool transient(int e) { return e == WSAECONNRESET || e == WSAEMSGSIZE; }
std::string error_text(int e) { return std::format("winsock error {}", e); }
void close_native(NativeSocket s) { closesocket(s); }
#else
using NativeSocket = int;
const NativeSocket kInvalid = -1;
using SockLen = socklen_t;

bool ensure_init() { return true; }
int last_socket_error() { return errno; }
bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool transient(int e) { return e == ECONNREFUSED || e == EINTR; }
std::string error_text(int e) { return std::strerror(e); }
void close_native(NativeSocket s) { ::close(s); }
#endif

NativeSocket native(std::intptr_t h) { return static_cast<NativeSocket>(h); }
std::intptr_t widen(NativeSocket s) { return static_cast<std::intptr_t>(s); }
bool valid(std::intptr_t h) { return native(h) != kInvalid; }

SockLen to_sockaddr(const Endpoint& ep, sockaddr_storage& ss) {
    std::memset(&ss, 0, sizeof ss);
    if (ep.family == Endpoint::Family::V4) {
        sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_port = htons(ep.port);
        std::memcpy(&sin.sin_addr, ep.address.data(), 4);
        std::memcpy(&ss, &sin, sizeof sin);
        return static_cast<SockLen>(sizeof sin);
    }
    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(ep.port);
    std::memcpy(&sin6.sin6_addr, ep.address.data(), 16);
    std::memcpy(&ss, &sin6, sizeof sin6);
    return static_cast<SockLen>(sizeof sin6);
}

std::optional<Endpoint> from_sockaddr(const sockaddr* sa) {
    Endpoint ep;
    if (sa->sa_family == AF_INET) {
        sockaddr_in sin;
        std::memcpy(&sin, sa, sizeof sin);
        ep.family = Endpoint::Family::V4;
        std::memcpy(ep.address.data(), &sin.sin_addr, 4);
        ep.port = ntohs(sin.sin_port);
        return ep;
    }
    if (sa->sa_family == AF_INET6) {
        sockaddr_in6 sin6;
        std::memcpy(&sin6, sa, sizeof sin6);
        ep.family = Endpoint::Family::V6;
        std::memcpy(ep.address.data(), &sin6.sin6_addr, 16);
        ep.port = ntohs(sin6.sin6_port);
        return ep;
    }
    return std::nullopt;
}

} // namespace

Endpoint Endpoint::v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t p) {
    Endpoint ep;
    ep.family = Family::V4;
    ep.address[0] = a;
    ep.address[1] = b;
    ep.address[2] = c;
    ep.address[3] = d;
    ep.port = p;
    return ep;
}

std::string Endpoint::address_string() const {
    if (family == Family::V4)
        return std::format("{}.{}.{}.{}", address[0], address[1], address[2], address[3]);
    std::string out;
    for (std::size_t i = 0; i < 16; i += 2) {
        if (i) out += ':';
        out += std::format("{:x}", (address[i] << 8) | address[i + 1]);
    }
    return out;
}

std::string Endpoint::to_string() const {
    if (family == Family::V4) return std::format("{}:{}", address_string(), port);
    return std::format("[{}]:{}", address_string(), port);
}

std::vector<Endpoint> resolve(const std::string& host, std::uint16_t port, std::string& error) {
    std::vector<Endpoint> out;
    if (!ensure_init()) {
        error = "socket library initialisation failed";
        return out;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || !result) {
#ifdef _WIN32
        error = std::format("cannot resolve {}: error {}", host, rc);
#else
        error = std::format("cannot resolve {}: {}", host, gai_strerror(rc));
#endif
        return out;
    }
    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        if (auto ep = from_sockaddr(ai->ai_addr)) {
            ep->port = port;
            bool dup = false;
            for (const auto& e : out) dup = dup || e == *ep;
            if (!dup) out.push_back(*ep);
        }
    }
    freeaddrinfo(result);
    std::stable_partition(out.begin(), out.end(), [](const Endpoint& e) { return e.family == Endpoint::Family::V4; });
    if (out.empty()) error = std::format("no usable address for {}", host);
    return out;
}

namespace {

// resolve_async's threads. Joined, not detached: one still in getaddrinfo when the program exits would otherwise
// run on while static destructors do, WinsockInit's WSACleanup among them. ensure_init() is called before this is
// first made, so this is destroyed first. A finished thread is joined when the next lookup starts.
class Lookups {
public:
    ~Lookups() {
        std::vector<Entry> all;
        {
            std::lock_guard lock(mutex_);
            all.swap(entries_);
        }
        for (auto& e : all)
            if (e.thread.joinable()) e.thread.join();
    }
    void start(std::function<void()> fn) {
        std::lock_guard lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->done->load()) {
                it->thread.join();
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        auto done = std::make_shared<std::atomic<bool>>(false);
        Entry e;
        e.done = done;
        e.thread = std::thread([fn = std::move(fn), done] {
            fn();
            done->store(true);
        });
        entries_.push_back(std::move(e));
    }

private:
    struct Entry {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex mutex_;
    std::vector<Entry> entries_;
};

} // namespace

void resolve_async(std::string host, std::uint16_t port,
                   std::function<void(std::vector<Endpoint> endpoints, std::string error)> done) {
    ensure_init(); // its static first, so the lookups' static below is destroyed before it
    static Lookups lookups;
    lookups.start([host = std::move(host), port, done = std::move(done)] {
        std::string error;
        auto endpoints = resolve(host, port, error);
        done(std::move(endpoints), std::move(error));
    });
}

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : handle_(other.handle_), error_(std::move(other.error_)) {
    other.handle_ = widen(kInvalid);
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        error_ = std::move(other.error_);
        other.handle_ = widen(kInvalid);
    }
    return *this;
}

bool UdpSocket::is_open() const { return valid(handle_); }

void UdpSocket::close() {
    if (valid(handle_)) close_native(native(handle_));
    handle_ = widen(kInvalid);
}

bool UdpSocket::open(Endpoint::Family family, std::string& error, const Endpoint* bind_to) {
    close();
    if (!ensure_init()) {
        error = "socket library initialisation failed";
        return false;
    }
    int af = family == Endpoint::Family::V4 ? AF_INET : AF_INET6;
    NativeSocket s = ::socket(af, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) {
        error = "socket(): " + error_text(last_socket_error());
        return false;
    }
#ifdef _WIN32
    u_long nonblocking = 1;
    bool nb_ok = ioctlsocket(s, FIONBIO, &nonblocking) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    bool nb_ok = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    if (!nb_ok) {
        error = "cannot make socket non-blocking: " + error_text(last_socket_error());
        close_native(s);
        return false;
    }
    // The original client asks for 128 KiB socket buffers (rof2decomp notes §7.4). The receive buffer is 2 MiB: the
    // server's files come in bursts of up to net::ServerFileSync::kWindow pieces (net/server_files.h), which overflow
    // 128 KiB between two service passes, and EQEmu's resends of what was dropped back off for seconds.
    int buf = 0x20000;
    int rcvbuf = 0x200000;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), static_cast<SockLen>(sizeof rcvbuf));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), static_cast<SockLen>(sizeof buf));

    Endpoint any;
    any.family = family;
    sockaddr_storage ss;
    SockLen len = to_sockaddr(bind_to ? *bind_to : any, ss);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        error = "bind(): " + error_text(last_socket_error());
        close_native(s);
        return false;
    }
    handle_ = widen(s);
    return true;
}

std::optional<Endpoint> UdpSocket::local_endpoint() const {
    if (!is_open()) return std::nullopt;
    sockaddr_storage ss{};
    SockLen len = sizeof ss;
    if (getsockname(native(handle_), reinterpret_cast<sockaddr*>(&ss), &len) != 0) return std::nullopt;
    return from_sockaddr(reinterpret_cast<const sockaddr*>(&ss));
}

bool UdpSocket::send_to(const Endpoint& to, std::span<const std::byte> data) {
    if (!is_open()) return false;
    sockaddr_storage ss;
    SockLen len = to_sockaddr(to, ss);
#ifdef _WIN32
    int n = ::sendto(native(handle_), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                     reinterpret_cast<const sockaddr*>(&ss), len);
#else
    auto n = ::sendto(native(handle_), data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&ss), len);
#endif
    if (n < 0) {
        int e = last_socket_error();
        if (would_block(e) || transient(e)) return true;
        error_ = "sendto(): " + error_text(e);
        return false;
    }
    return true;
}

std::optional<std::size_t> UdpSocket::recv_from(std::span<std::byte> buffer, Endpoint& from) {
    if (!is_open()) return std::nullopt;
    for (;;) {
        sockaddr_storage ss{};
        SockLen len = sizeof ss;
#ifdef _WIN32
        // A datagram larger than the buffer fails with WSAEMSGSIZE (transient above): it is dropped.
        int n = ::recvfrom(native(handle_), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                           reinterpret_cast<sockaddr*>(&ss), &len);
        bool truncated = false;
#else
        // POSIX recvfrom would return the first buffer.size() bytes of a larger datagram as if they were all of it;
        // recvmsg reports the cut in msg_flags (MSG_TRUNC, on Linux, macOS and the BSDs alike), and the datagram is
        // dropped as Winsock drops it.
        iovec iov{buffer.data(), buffer.size()};
        msghdr msg{};
        msg.msg_name = &ss;
        msg.msg_namelen = len;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        auto n = ::recvmsg(native(handle_), &msg, 0);
        bool truncated = n >= 0 && (msg.msg_flags & MSG_TRUNC) != 0;
#endif
        if (n < 0) {
            int e = last_socket_error();
            if (transient(e)) continue;
            if (!would_block(e)) error_ = "recvfrom(): " + error_text(e);
            return std::nullopt;
        }
        if (truncated) continue;
        auto ep = from_sockaddr(reinterpret_cast<const sockaddr*>(&ss));
        if (!ep) continue;
        from = *ep;
        return static_cast<std::size_t>(n);
    }
}

bool UdpSocket::wait_readable(std::chrono::milliseconds timeout) const {
    if (!is_open()) return false;
#ifdef _WIN32
    WSAPOLLFD pfd{};
    pfd.fd = native(handle_);
    pfd.events = POLLRDNORM;
    return WSAPoll(&pfd, 1, static_cast<INT>(timeout.count())) > 0;
#else
    pollfd pfd{};
    pfd.fd = native(handle_);
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, static_cast<int>(timeout.count())) > 0;
#endif
}

} // namespace nmsc::platform
