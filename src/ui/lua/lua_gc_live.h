#pragma once

#include <cstddef>

struct lua_State;

namespace nmsc::ui::lua {

// The live heap the collector's last finished cycle found, in bytes: LUA_GCCOUNT less the garbage made while that
// cycle ran (which only the next cycle can free). Lua's API has no call for it.
std::size_t gc_live_bytes(lua_State* L);

} // namespace nmsc::ui::lua
