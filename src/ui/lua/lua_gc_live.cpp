// The one place the client reads the collector's internals (third_party/lua, pinned): kept in a file of its own so
// Lua's internal macros (G, cast, ...) reach nothing else.

#include "ui/lua/lua_gc_live.h"

extern "C" {
#include "lstate.h"
}

namespace nmsc::ui::lua {

std::size_t gc_live_bytes(lua_State* L) {
    // Set by the cycle's atomic step to the heap then, and lowered by what its sweep frees: the objects the cycle
    // found reachable. What was made after the atomic step is not in it (lgc.c singlestep, sweepstep).
    return static_cast<std::size_t>(G(L)->GCestimate);
}

} // namespace nmsc::ui::lua
