#pragma once

// The classic sky: sky.s3d / sky.wld (format and evidence: docs/formats/sky.md).
//
// OP_NewZone's sky byte N picks the actors LAYER<N>1 (the sky dome),
// LAYER<N>2 (a skeleton carrying the sun and moon; only skies 1, 3 and 6 have
// one) and LAYER<N>3 (the cloud layer). Sky 0 has no sky. The meshes are unit
// sized; the original draws them around the camera scaled by 5000.
//
// Sky colours come from a fixed 60-level x 18-entry table (the "sky level"
// steps through it over the day). Each dome vertex has one entry of a level,
// picked by its height: entry = 16 - floor(z * 128), clamped to 0..16.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "assets/model.h"
#include "assets/pfs.h"

namespace hailstone { class GameDir; }

namespace hailstone::assets {

struct SkyLayer {
    Mesh mesh;
    // Per vertex: the colour entry (0..16) of the sky colour table.
    std::vector<std::uint8_t> color_entry;
};

struct Sky {
    int index = 0;
    std::optional<SkyLayer> dome;   // LAYER<N>1
    std::optional<SkyLayer> clouds; // LAYER<N>3
    // LAYER<N>2_HS_DEF in its rest pose (sun and moon), or empty.
    std::optional<Mesh> bodies;

    std::shared_ptr<const PfsArchive> archive; // sky.s3d, for the textures
    std::optional<std::vector<std::byte>> read_file(std::string_view name) const;
};

// Highest sky index with layers in the shipped sky.wld.
inline constexpr int kMaxSkyIndex = 17;

// The colour entry of a dome vertex at height z (sky units; Mac client
// t3dInitSky @0x36e1d0: 16 - (int)(z * 16 * 8), negative or >= 16 -> 16 or 0).
std::uint8_t sky_color_entry(float z);

// Loads sky `index` (1..kMaxSkyIndex) from sky.s3d. Returns nullopt for index 0,
// an index without layers, or a missing sky.s3d. Throws on a malformed archive.
std::optional<Sky> load_sky(const GameDir& game, int index);

// ---------------------------------------------------------------------------
// Time of day.

// The sky colour table: kSkyLevels levels x kSkyColors entries, RGB 0..255.
// Level 14 is full day, 44 full night (from the Mac client's static table
// @0x4b8640, passed to t3dInitSky by CDisplay::InitWorld). Entry 17 is a grey
// that no dome vertex uses.
inline constexpr int kSkyLevels = 60;
inline constexpr int kSkyColors = 18;
glm::vec3 sky_table_color(int level, int entry); // 0..1

// The sky level for a sky time (hour as sent by the server, 0..24, plus a
// fraction of minutes), following CDisplay_UpdateSkyTimeOfDay @0x496d80: the
// day is cut into 12-minute slots q; 3:00-9:00 steps 44 -> 59 -> 0 -> 13, 9:00-15:00
// holds 14, 15:00-21:00 steps 14 -> 43, and the night holds 44. The result is a
// continuous level in [0, 60) (the original steps every 36 real seconds).
float sky_level(float hour);

// The colour of a table entry at a continuous level, interpolated between the
// two neighbouring levels.
glm::vec3 sky_color(float level, int entry);

// The scene light level of a sky level (RoF2 table @0xaf3170, 0 night .. 1 day).
float sky_light_level(float level);

// The outdoor ambient light colour for an hour, interpolated per minute
// (RoF2 per-hour tables @0xaf3260/0xaf32c0/0xaf3320, read by
// CDisplay_UpdateSkyWeatherAndFog when there is no new-style sky object).
glm::vec3 sky_ambient(float hour);
// One entry of those tables: channel 0 red, 1 green, 2 blue; index 0..23 is
// hour 1..24.
float sky_hour_ambient(int channel, int index);

// The heading (0..512 per turn) of the sky's day-cycle sweep: 448 at 9:00,
// 512 at noon, a full turn per day (CDisplay_UpdateSkyTimeOfDay's sweeps of 128
// over each 6-hour block). The moon is half a turn ahead.
float sky_sweep_heading(float hour);

} // namespace hailstone::assets
