#pragma once

// Flora for zones that have none of their own (docs/graphics.md "Zone flora"): Hailstone's own, not the original's.
// The classic (WLD) zones carry no flora data; a rules file of this client's (flora/<zone>.txt in the install folder)
// names the ground textures that grow plants, how densely and of which kinds, and the EQG archive in the player's game
// folder whose RadialFloraDefs.rfd and textures give the plants' shapes and pictures (East Commonlands takes
// commonlands.eqg's). Nothing in the zone's files changes; the plants are placed over the zone's terrain as it loads,
// into Zone::flora, and drawn by render::FloraRenderer like an EQG zone's. They are looks only: no collision, line of
// sight or picking sees them.
//
// The rules file, one directive a line, '#' to the end of a line a comment:
//   donor <archive>                  the EQG archive of the shapes and textures (commonlands.eqg)
//   max_slope <degrees>              steeper ground grows nothing (default 40)
//   scale <min> <max>                every plant's size times a random factor in the range (default 1 1)
//   ground <glob> <density> [+green] <kind>[:<weight>]...
//                                    ground whose texture matches the glob (case-insensitive, '*' and '?') grows
//                                    <density> plants per 100 square units, each one of the kinds (definition names
//                                    of the donor's RadialFloraDefs.rfd) picked by weight (default 1); the first
//                                    matching line wins, and a density of 0 keeps a texture bare. With +green a plant
//                                    grows only where the texel under it is green (is_green_texel): the classic
//                                    zones' ground textures blend grass into cliff, sand or a path within one picture
// Plants keep off water, lava and zone-line regions.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "assets/eqg.h"

namespace nmsc { class GameDir; }

namespace nmsc::assets {

struct Zone;
struct ZoneFlora;

struct ZoneFloraRules {
    struct Kind {
        std::string definition;
        float weight = 1.0f;
    };
    struct Ground {
        std::string texture; // glob
        float density = 0.0f; // plants per 100 square units
        bool green_only = false;
        std::vector<Kind> kinds;
    };
    std::string donor;
    float max_slope = 40.0f; // degrees
    float min_scale = 1.0f, max_scale = 1.0f;
    std::vector<Ground> ground;
};

// Reads a rules file. A line that does not parse is skipped and described in `errors` (with its line number) when
// given.
ZoneFloraRules parse_zone_flora_rules(std::string_view text, std::vector<std::string>* errors = nullptr);

// The most plants one zone gets: a bound for a rules file asking too much.
inline constexpr std::size_t kMaxZoneFlora = 4'000'000;
// The tiles the plants are binned into (ZoneFlora::tiles), in units.
inline constexpr float kZoneFloraTile = 128.0f;

// Whether a texel (RGB bytes) is grass for +green: green over red and over blue, and not near black.
bool is_green_texel(std::uint8_t r, std::uint8_t g, std::uint8_t b);

// The colour (RGB bytes, 0x00BBGGRR) of a texture at a UV (wrapped), or nullopt when the texture cannot be read.
using TexelLookup = std::function<std::optional<std::uint32_t>(std::string_view texture, glm::vec2 uv)>;

// Places plants over `zone`'s terrain by `rules`, of the shapes in `definitions`, into `out` (its definitions,
// instances and tiles replaced). `texel` answers +green (without it, or for a texture it cannot read, +green keeps
// every spot). The same zone and rules give the same plants at every load.
void place_zone_flora(const Zone& zone, const ZoneFloraRules& rules, std::vector<eqg::FloraDefinition> definitions,
                      ZoneFlora& out, const TexelLookup& texel = {});

// For a zone without flora of its own: reads <rules_dir>/<zone name>.txt, opens its donor archive from `game` and
// places the plants into zone.flora. Returns how many were placed (0 when the zone has flora already, has no rules
// file, or its donor or definitions are missing; each logged).
std::size_t add_zone_flora(const GameDir& game, Zone& zone, const std::filesystem::path& rules_dir);

} // namespace nmsc::assets
