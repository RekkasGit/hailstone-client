#include "app/login_launch.h"

#include <charconv>
#include <stdexcept>

#include "core/ascii_case.h"

namespace hailstone::app {

bool consume_login_launch_arg(LoginLaunch& launch, std::span<const std::string_view> args, std::size_t& at,
                              const std::function<const char*(const char*)>& env) {
    if (at >= args.size()) return false;
    const auto arg = args[at];
    if (iequals(arg, "patchme")) return true;
    auto value = [&]() -> std::string_view {
        if (at + 1 >= args.size() || args[at + 1].starts_with("--"))
            throw std::runtime_error("Launcher option needs a value.");
        return args[++at];
    };
    const bool native_ticket = istarts_with(arg, "/ticket:");
    if (arg == "--ticket" || arg == "--ticket-env" || native_ticket) {
        if (launch.token_requested) throw std::runtime_error("Supply only one login ticket.");
        launch.token_requested = true;
        std::string_view ticket;
        if (native_ticket) ticket = arg.substr(8);
        else if (arg == "--ticket-env") {
            const std::string name(value());
            if (name.empty() || name.find('=') != std::string::npos || name.find('\0') != std::string::npos)
                throw std::runtime_error("--ticket-env needs an environment variable name.");
            const char* found = env(name.c_str());
            if (found) ticket = found;
        } else ticket = value();
        // RoF2's launcher has a 64-byte slot (eqgame 0x5fdec0). Reject overlong native arguments instead of
        // silently truncating them. Hailstone's explicit transports accept larger opaque tickets as an extension.
        if (native_ticket && ticket.size() > 63)
            throw std::runtime_error("RoF2-style /ticket: accepts at most 63 bytes; use --ticket for a larger ticket.");
        if (!net::login_codec::valid_login_token(ticket))
            throw std::runtime_error("Login ticket is empty, contains a NUL, or exceeds the supported size.");
        launch.ticket.assign(ticket);
        return true;
    }
    if (arg == "--world" || istarts_with(arg, "/server:")) {
        if (!launch.world.empty()) throw std::runtime_error("Supply only one launcher world.");
        const auto world = arg == "--world" ? value() : arg.substr(8);
        const std::size_t limit = arg == "--world" ? 256 : 127;
        if (world.empty() || world.size() > limit || world.find('\0') != std::string_view::npos)
            throw std::runtime_error("Launcher world is empty or too long.");
        launch.world.assign(world);
        return true;
    }
    return false;
}

void resolve_login_launch(const LoginLaunch& launch, std::string& user, std::string& password,
                          std::string& account, bool viewer) {
    if (launch.token_requested) {
        user.clear();
        core::wipe(password);
        account.clear();
    }
    if (launch.token_requested && viewer)
        throw std::runtime_error("A login ticket is for the playable client, not a viewer.");
    if (!launch.world.empty() && !launch.token_requested && user.empty() && password.empty() && account.empty())
        throw std::runtime_error("--world needs a ticket, --user, or --account login.");
}

ServerProfile token_login_profile(std::string_view saved_name, const ServerProfile* named,
                                  const std::optional<ServerProfile>& eqhost,
                                  const std::vector<ServerProfile>& profiles, std::string_view host, int port) {
    if (!saved_name.empty() && !named) throw std::runtime_error("Token login's saved server was not found.");
    if (port < 0 || port > 65535) throw std::runtime_error("Login port must be between 1 and 65535.");
    ServerProfile profile;
    if (named) profile = *named;
    else if (!host.empty()) {
        profile.host = host;
        if (port > 0) profile.port = port;
        profile.name = profile.host;
    } else if (eqhost) profile = *eqhost;
    else throw std::runtime_error("Token login needs --server, --host, or the asset folder's eqhost.txt.");
    if (!host.empty()) profile.host = host;
    if (port > 0) profile.port = port;
    if (profile.host.empty() || profile.port < 1 || profile.port > 65535)
        throw std::runtime_error("Token login's server has no valid login address.");
    // Overrides must not accidentally use another endpoint's assets or saved identity.
    if (!named) {
        if (const auto* match = find_profile(profiles, profile.host, profile.port)) profile = *match;
    }
    return profile;
}

const net::ServerInfo* launcher_world(std::span<const net::ServerInfo> servers, std::string_view selector) {
    if (selector.empty()) return nullptr;
    std::uint32_t id = 0;
    const auto number = std::from_chars(selector.data(), selector.data() + selector.size(), id);
    const bool numeric = number.ec == std::errc() && number.ptr == selector.data() + selector.size();
    const net::ServerInfo* match = nullptr;
    for (const auto& server : servers) {
        if (!(numeric ? server.id == id : iequals(server.name, selector))) continue;
        if (match) return nullptr;
        match = &server;
    }
    return match;
}

} // namespace hailstone::app
