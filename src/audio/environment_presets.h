#pragma once

// The per-zone listener room presets of EqSoundManager::SetCurrentZone @0x4ab4c0, read at run time from the player's
// own eqgame.exe: no copy of the table is in this repository (clean room). In the RoF2 build the table is at virtual
// address 0xac7b80, 127 records of 0x2c bytes: the zone's short name (32 bytes, NUL-terminated), then u32s at +0x20
// the outside preset (EqSoundManager +0xfe8), +0x24 the high preset (+0xfe0) and +0x28 the low preset (+0xfe4), each
// a Miles / EAX room type. A missing exe, another build, or anything that does not look like that table gives no
// per-zone presets: every zone takes the defaults (audio/listener.h RoomPresets).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hailstone {
class GameDir;
}

namespace hailstone::audio {

struct ZoneRoomPreset {
    std::string zone; // lower case
    std::int32_t outside = 0, high = 0, low = 0;
};

// Where the table is in the RoF2 eqgame.exe, and its shape.
inline constexpr std::uint32_t kZoneRoomTableVa = 0xac7b80;
inline constexpr std::size_t kZoneRoomRecords = 127;
inline constexpr std::size_t kZoneRoomRecordSize = 0x2c;
// The highest Miles room type (mss32's 26 EAX 1 environments, audio/listener.h miles_room_reverb).
inline constexpr std::int32_t kMaxRoomType = 25;

// Reads up to `size` bytes at a file offset of the exe (fewer at its end); nullopt when it cannot be read.
using ExeReader = std::function<std::optional<std::vector<std::byte>>(std::uint64_t offset, std::size_t size)>;

// The table out of a 32-bit PE image read through `read`: the headers map the virtual address to a file offset
// (inside one section's raw data), and every record must have a NUL-terminated name of lower-case letters, digits and
// '_' and room types 0..kMaxRoomType. nullopt, with the reason in `error`, when anything fails.
std::optional<std::vector<ZoneRoomPreset>> read_zone_room_presets(const ExeReader& read, std::string* error = nullptr);
// The same over an image in memory (tests).
std::optional<std::vector<ZoneRoomPreset>> read_zone_room_presets(std::span<const std::byte> exe,
                                                                  std::string* error = nullptr);
// The table from the base asset folder's own eqgame.exe (never the server's overlay), read in place: the headers
// and the table only. Empty when there is none or it does not validate; that is logged once per run.
std::vector<ZoneRoomPreset> load_zone_room_presets(const GameDir& game);

} // namespace hailstone::audio
