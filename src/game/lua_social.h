#pragma once

// Lua socials: a Hailstone extension of the original's socials (game/hotbuttons.h). A social may carry Lua instead
// of its five command lines: `press` runs when its hot button is pressed (with MQ's `mq` module, mq/lua_mq.h), and
// the optional `face` returns how the button looks (a label, a colour, a cooldown sweep, a count). What a Lua social
// may do is the server's choice (plugin/policy.h ButtonRules, its hailstone.cfg's [socials]): anything on a server
// without a hailstone.cfg.
//
// A character keeps them as files beside hotbuttons.ini: socials/<n>.lua, n the social's number 1..120 (page * 12 +
// button + 1). The social's name and colour stay in hotbuttons.ini as the original's; the file holds them too, so
// it can be shared alone:
//   -- Hailstone Lua social
//   -- name: Assist
//   -- color: 3
//   -- [press]
//   <Lua>
//   -- [face]
//   <Lua>
// Shared as one line of text: "hailstone-social:1:" and the file in base64. A plain social is shared the same way,
// its command lines in a "-- [lines]" section instead of the Lua (each line as it is, empty ones kept up to the
// last; not a file a character keeps):
//   -- [lines]
//   /assist
//   /pause 5, /attack on

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sha256.h"

namespace hailstone::game {

struct LuaSocial {
    std::string press;
    std::string face;
    bool empty() const { return press.empty() && face.empty(); }
    bool operator==(const LuaSocial&) const = default;
};

struct LuaSocialFile {
    std::string name;
    int color = 0;
    LuaSocial lua;
    std::vector<std::string> lines; // a plain social's (at most 5, each at most 255), written only without Lua
};

std::string format_lua_social(const LuaSocialFile& f);
// nullopt when the text is not a social (no "-- [press]", "-- [face]" or "-- [lines]" section).
std::optional<LuaSocialFile> parse_lua_social(std::string_view text);

// The SHA-256 of "-- [press]\n" press "\n-- [face]\n" face, with line ends as "\n": the social's code, which a
// Lua state is kept for while it stays the same. The name and colour do not count.
Sha256Digest lua_social_hash(const LuaSocial& s);

inline constexpr std::string_view kLuaSocialSharePrefix = "hailstone-social:1:";
std::string export_lua_social(const LuaSocialFile& f);
std::optional<LuaSocialFile> import_lua_social(std::string_view text);

// The file name of social `index` (0..119): "<index + 1>.lua".
std::string lua_social_file_name(int index);

} // namespace hailstone::game
