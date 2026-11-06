#pragma once

// One of a plugin's own TCP connections (the SDK's tcp_* functions, NMSC_CAP_NET; docs/plugins.md "Connections").
// Nothing waits for the network: a host name is looked up on a thread of its own (platform::resolve_async) while the
// connection counts as connecting, and what the system cannot take at once waits here until a later call.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "platform/tcp_socket.h"

namespace nmsc::plugin {

class PluginTcp {
public:
    // What send() keeps for a peer that stopped reading before the connection counts as broken.
    static constexpr std::size_t kPendingMax = std::size_t{16} << 20;

    // Starts connecting to `address` (a name or a numeric address). Null, with `error` set, when a numeric address's
    // socket cannot even be made; a name that does not resolve shows later as state() -1, with error() saying why.
    static std::unique_ptr<PluginTcp> open(const std::string& address, std::uint16_t port, std::string& error);

    ~PluginTcp();
    PluginTcp(const PluginTcp&) = delete;
    PluginTcp& operator=(const PluginTcp&) = delete;

    // 1 connected, 0 still connecting (or looking the name up), -1 closed.
    int state();
    // Takes every byte and returns the count, or -1 once closed.
    std::int64_t send(std::span<const std::byte> data);
    // What has arrived: the count, 0 for nothing yet, -1 once closed with nothing left.
    std::int64_t recv(std::span<std::byte> out);
    // Why it closed.
    std::string error() const;

    // Tests: the bytes kept (those the system took that are not let go yet, and those still waiting), and those
    // still waiting.
    std::size_t kept_for_test() const { return pending_.size(); }
    std::size_t waiting_for_test() const { return pending_.size() - sent_; }

private:
    struct Lookup;
    PluginTcp() = default;
    // Starts the socket once the name's addresses came; then hands the system what waits.
    void progress();
    void flush();

    std::string address_;
    std::shared_ptr<Lookup> lookup_; // while the name is looked up
    platform::TcpSocket socket_;
    std::vector<std::byte> pending_; // from sent_ on: what the system has not taken
    std::size_t sent_ = 0;
    std::string error_;
};

} // namespace nmsc::plugin
