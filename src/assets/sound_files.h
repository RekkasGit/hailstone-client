#pragma once

// The game's sound tables and zone emitter files (docs/formats/audio.md):
//   <zone>_sounds.eff   classic emitters, 84-byte records (EqSoundManager::LoadOldEmitters @0x4ab150)
//   <zone>_sndbnk.eff   the zone's wave names, EMIT ids 1.. and LOOP ids 162.. (same function)
//   <zone>.emt          later emitters, comma-separated (EqSoundManager::EmitterLoad @0x4ab000,
//                       EqEmitterData_ParseLine @0x4a9bd0)
//   soundassets.txt     global wave id -> file name ("id^name")
//   mp3index.txt        the old music selections, 1-based (EqSoundManager::OldMp3Init @0x4a9e80)
//   racedata.txt        per race the 44 sound ids of each gender (RaceManager_LoadFile @0x7f47d0)
// Everything here is a pure parser over bytes or text.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace hailstone::assets {

// One 84-byte record of <zone>_sounds.eff. The record holds a "day" side
// (first) and a "night" side (second); the loader collapses them into one
// emitter when both sides agree (see zone emitters in audio/zone_emitters.h).
struct EffSoundRecord {
    glm::vec3 position{0.0f};          // 16, 20, 24: zone file coordinates
    float radius = 0.0f;               // 28: < 0 = an environment restriction (-radius)
    std::int32_t cooldown[2] = {0, 0}; // 32, 36: ms
    std::int32_t random_delay = 0;     // 40: ms, shared by both sides
    std::int32_t unknown44 = 0;        // 44: not read by the loader
    std::int32_t sound[2] = {0, 0};    // 48, 52: wave id, or music (kind 1: < 0 mp3index, >= 0 zone XMI track)
    std::uint8_t kind[2] = {0, 0};     // 56, 57: 0 2D ambient, 1 music, 2 3D, 3 3D at random places
    std::int32_t level[2] = {0, 0};    // 60, 64: effects: attenuation (1/100 dB); music: loop count
    std::int32_t fade_out[2] = {0, 0}; // 68, 72: ms
    std::int32_t param[2] = {0, 0};    // 76, 80: music: fade-in ms; kind 2: full-volume distance; kind 3: spread
};

inline constexpr std::size_t kEffSoundRecordSize = 84;

// Reads every whole record; a trailing partial record is ignored, as fread does.
std::vector<EffSoundRecord> parse_sounds_eff(std::span<const std::byte> data);

// <zone>_sndbnk.eff: wave id -> file name. CR, LF and spaces are removed from
// each line; "EMIT" restarts the ids at 1, "LOOP" at 162; a name without '.'
// gets ".wav".
std::map<std::int32_t, std::string> parse_sound_bank(std::string_view text);

inline constexpr std::int32_t kSoundBankLoopBase = 162;

// soundassets.txt: lines "id^name[^]"; "Unknown" names are left out.
std::unordered_map<std::int32_t, std::string> parse_sound_assets(std::string_view text);
// soundassets.txt's named lines, "name^file" ("YouveGotMail^mail1.wav"): the name -> the file. The original gives
// each a free wave id and keeps name -> id in the sound manager (+0xbaec, FUN_004ab800); FUN_004ab700 looks a
// name up (-1 when it is not there).
std::unordered_map<std::string, std::string> parse_named_sound_assets(std::string_view text);

// mp3index.txt: one name per line (CR, LF and spaces removed, empty lines
// skipped, at most 31 characters kept, at most 100 entries).
std::vector<std::string> parse_mp3_index(std::string_view text);

// One line of a .emt file (EqEmitterData).
struct EmtRow {
    std::int32_t version = 0;      // field 0: rows below 2 have no fields 19..21
    std::string file;              // 1: wave, .mp3 or .xmi
    std::int32_t environment = 0;  // 2: 0 any, else the listener environment it plays in
    std::int32_t period = 0;       // 3: 0 always, 1 day, 2 night
    float gain = 1.0f;             // 4
    std::int32_t fade_in = 0;      // 5: ms
    std::int32_t fade_out = 0;     // 6: ms
    std::int32_t loop_count = 0;   // 7: 0 forever, else plays
    glm::vec3 position{0.0f};      // 8, 9, 10
    float full_volume_distance = 0.0f; // 11: Miles min distance
    float max_distance = 0.0f;     // 12: Miles max distance
    float random_distance = 0.0f;  // 13: > 0 plays at a random spot this far around the listener
    float range = 0.0f;            // 14: activation radius (0 = none)
    std::int32_t delay_min = 0;    // 15: ms
    std::int32_t delay_max = 0;    // 16: ms
    std::int32_t xmi_track = 0;    // 17
    float echo = 0.0f;             // 18
    bool user_disableable = true;  // 19 (version < 2: true)
    bool flag29 = false;           // 20 (version < 2: false)
    bool flag2a = false;           // 21
};

// Lines of 10 characters or fewer are skipped (EmitterLoad). Missing fields
// read as 0, as the original's field reader gives.
std::vector<EmtRow> parse_emt(std::string_view text);

// racedata.txt sound ids: per race, male and female, the 44 ids in the order
// RaceManager_GetSoundID indexes them (0..17 then 18..43).
struct RaceSoundIds {
    std::array<std::int32_t, 44> male{};
    std::array<std::int32_t, 44> female{};
};
// The live client's racedata.txt has one line per race and gender instead:
// race, gender (0 male, 1 female, 2 both), the 44 ids in index order, the male
// and female heights, then fields RoF2 does not have (the model code at 50).
// Both layouts are read; a line with fewer than 85 carets is a live one.
std::unordered_map<std::int32_t, RaceSoundIds> parse_race_sounds(std::string_view text);
bool is_live_racedata_line(std::string_view line);
// A live line's gender field: 0, 1, or 2 for both (also when it is empty).
int live_racedata_gender(std::string_view line);

// spellsnew.eff (SpellEffects_ReadFiles @0x4adeb0, rof2decomp
// re/src/game/world/spell_effects_read.cpp): 268-byte records, a 64-byte name
// then three stages (cast, projectile, impact) of 68 bytes that each start with
// a wave id. A spell's SpellAnim (spells_us.txt field 145) indexes it; index 0
// and empty names are no effect (SpellMgr_SetSpellExtraData @0x467260).
struct SpellEffectSounds {
    std::string name;
    std::int32_t cast = 0, projectile = 0, impact = 0;
};
std::vector<SpellEffectSounds> parse_spell_effect_sounds(std::span<const std::byte> data);

} // namespace hailstone::assets
