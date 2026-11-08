#pragma once

// /nav (MQ2Nav's command set, docs/navigation.md) and /moveto (MQ2MoveUtils' straight walk) on the client's
// navigation (nav/nav_control.h), with MQ's 'move' permission (plugin/policy.h kCapMove) for plugins and scripts.

#include <functional>
#include <string>

#include "mq/commands.h"

namespace hailstone::mq {

void add_nav_commands(plugin::Hooks& hooks, plugin::Hooks::Owner owner, const CommandEnv& env,
                      const std::function<void(const std::string&)>& say);

} // namespace hailstone::mq
