#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/server_profile.h"
#include "core/wipe.h"
#include "net/login.h"

namespace hailstone::app {

// Launcher credentials live only for this attempt, never in a profile or the keyring.
struct LoginLaunch {
    bool token_requested = false;
    std::string ticket;
    std::string world;
    LoginLaunch() = default;
    LoginLaunch(const LoginLaunch&) = default;
    LoginLaunch& operator=(const LoginLaunch& other) {
        if (this != &other) {
            core::wipe(ticket);
            token_requested = other.token_requested;
            ticket = other.ticket;
            world = other.world;
        }
        return *this;
    }
    LoginLaunch(LoginLaunch&& other) noexcept
        : token_requested(other.token_requested), ticket(std::move(other.ticket)), world(std::move(other.world)) {
        core::wipe(other.ticket);
        other.token_requested = false;
    }
    LoginLaunch& operator=(LoginLaunch&& other) noexcept {
        if (this != &other) {
            core::wipe(ticket);
            token_requested = other.token_requested;
            ticket = std::move(other.ticket);
            world = std::move(other.world);
            core::wipe(other.ticket);
            other.token_requested = false;
        }
        return *this;
    }
    ~LoginLaunch() { core::wipe(ticket); }
};

// Consumes one launcher argument (and its value), leaving unrelated client arguments alone.
// Errors name the option, never its credential. argv excludes the executable name.
bool consume_login_launch_arg(LoginLaunch& launch, std::span<const std::string_view> args, std::size_t& at,
                              const std::function<const char*(const char*)>& env);
// As in RoF2, a ticket overrides legacy account/password arguments. Clear them before constructing the client,
// so a launcher can keep its existing argument template without loading a keyring credential or sending it.
void resolve_login_launch(const LoginLaunch& launch, std::string& user, std::string& password,
                          std::string& account, bool viewer);

// A ticket must go to an explicitly selected endpoint or the asset folder's eqhost.txt, never the last-used
// server. A matching profile supplies its assets/UI without changing that endpoint.
ServerProfile token_login_profile(std::string_view saved_name, const ServerProfile* named,
                                  const std::optional<ServerProfile>& eqhost,
                                  const std::vector<ServerProfile>& profiles, std::string_view host, int port);

// A launcher world is an exact name (ignoring ASCII case), or a decimal login-server world id. Missing or
// ambiguous matches stay at server select; they never fall back to another world.
const net::ServerInfo* launcher_world(std::span<const net::ServerInfo> servers, std::string_view selector);

} // namespace hailstone::app
