#pragma once

// The MusicManager: every music the client can play, the global entries 1..15 (EqSoundManager::LoadGlobalMusic
// @0x4aa860) and the zone's music emitters' entries, with the one rule that picks what plays
// (MusicManager_SelectActiveEntry @0x5c0b40, MusicManager::Play @0x5c0c00 and Stop @0x5c0c30;
// checked against the original's vectors in tests/audio_vectors_test.cpp).
//
// An entry: +0 volume, +4 start requested (Play sets it), +0x10 id, +0x14 asset, +0x18 track, +0x1c priority (lower
// wins), +0x20 selected (Play sets, Stop clears), +0x24 fade-in, +0x28 fade-out when another entry takes over or the
// music is off, +0x2c fade-out when nothing replaces it, +0x32 finished. The manager: +0 the list, +4 the entry
// playing, +8 the music volume, +0xc music on.
//
// Global entries (the calls of EqSoundManager::SetMusicSelection(id, on)):
//   1 and 4  eqtheme.mp3 (else opener4.xmi, entry 4 its track 1 at priority 9): 1 from CDisplay_InitDisplay to
//            StartNetworkGame, 4 from there until the character enters the world (DoCharacterSelection / DoMainLoop)
//   2        combat (combattheme1/2.mp3 by rand() parity, else gl.xmi 0): auto attack on with the CombatMusic option
//   3        bank (CBankWnd / guild bank open .. close), 6 merchants, 7 trainers (CTrainWnd)
//   5        the loot window (no asset: never heard)
//   8        death (deaththeme.mp3, else gl.xmi 3): PlayerClient death of the local player; stopped on zone-in
//   9..15    gl.xmi, never selected by this exe
// Zone music emitters have priority int(range + 10000), so a global entry always wins.

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace hailstone::audio {

class EmitterSink;

// One MusicManager entry (MusicManager::Set @0x5c01a0).
struct MusicEntry {
    std::int32_t id = 0;           // +0x10
    std::string file;              // +0x14: the .mp3 or .xmi (empty: no asset, nothing plays)
    std::int32_t track = 0;        // +0x18: XMI sequence (zero-based); 0 for mp3
    std::int32_t loops = 0;        // plays; 0 = forever
    float volume = 1.0f;           // +0x00
    std::int32_t priority = 0;     // +0x1c: lower wins
    std::int32_t fade_in_ms = 0;   // +0x24
    std::int32_t crossfade_ms = 1500; // +0x28: fade-out when another entry takes over (or the music is off)
    std::int32_t fade_out_ms = 0;  // +0x2c: fade-out when nothing replaces it (capped at 20000 for EFF)
    bool active = false;           // +0x20: selected
    bool playable = false;         // +0x04: start requested
    bool finished = false;         // +0x32
    // +0x31 (Set's last argument; EMT field 21 for a zone's music): copied to the instance FadeIn starts (its +0x1d).
    // When such an instance of an mp3 (a 2D sample, instance type 0) has ended, faded out or not, the asset's
    // sweep (@0x5bfb20) marks every entry of that asset finished, and the entry is never chosen again.
    bool finish_when_done = false;
};

// ---- the selection rule (pure) ----

struct MusicSelectEntry {
    bool selected = false, finished = false;
    std::int32_t priority = 0;
    bool start_requested = false;
};

struct MusicSelectResult {
    int playing = -1;                // the list index now playing (+4), -1 none
    int fade_out = -1;               // FadeOut of this entry (the one replaced)
    bool fade_out_nothing_replaces = false; // FadeOut(1): its +0x2c fade, else +0x28
    bool fade_in = false;            // FadeIn(volume) of `playing`
    bool fade_instances = false;     // music off: every instance of `playing`'s asset fades to 0 over its +0x28
};

// MusicManager_SelectActiveEntry: the selected, unfinished entry with the lowest priority (signed); the current one
// keeps a tie, then the first on the list. With music on, FadeIn when the entry's start was requested and the volume
// is above 0 (ordered: NaN is not).
MusicSelectResult select_active_entry(std::span<const MusicSelectEntry> entries, int current, bool music_enabled,
                                      float volume);

// ---- the global entries (pure) ----

// One MusicManager::Set call of LoadGlobalMusic (the vectors' fields).
struct MusicSetCall {
    std::int32_t entry = 0;
    std::string asset;             // "" = none found (the entry is cleared)
    std::int32_t track = 0;
    float volume = 1.0f;
    std::int32_t unused = 0;       // passed, not stored by Set
    std::int32_t priority = 0;
    std::int32_t fade_in_ms = 0;
    std::int32_t fade_out_replaced_ms = 0;
    std::int32_t fade_out_stopped_ms = 0;
    std::int32_t pool = 0;         // the mp3 pool for mp3 files, else 0
};
// `found`: EqSoundManager::GetAsset found the file; `rand_value`: rand() after srand(FastTime()).
std::vector<MusicSetCall> load_global_music(const std::function<bool(const std::string&)>& found,
                                            std::int32_t rand_value);

// The global entry ids.
enum GlobalMusic : std::int32_t {
    kMusicLogin = 1, kMusicCombat = 2, kMusicBank = 3, kMusicCharacterSelect = 4, kMusicLoot = 5,
    kMusicMerchant = 6, kMusicTrainer = 7, kMusicDeath = 8,
};

// ---- the runtime ----

class MusicManager {
public:
    // MusicManager::Set @0x5c01a0: Clear(id) (the entry with that id leaves the list; were it playing, nothing is
    // playing now), then the new entry goes at the head of the list.
    void set(MusicEntry entry);
    void remove(std::int32_t id, EmitterSink& sink);
    MusicEntry* find(std::int32_t id);
    const std::vector<MusicEntry>& entries() const { return entries_; }

    // MusicManager::Play / Stop (EqSoundManager::SetMusicSelection(id, on)).
    void play(std::int32_t id, EmitterSink& sink);
    void stop(std::int32_t id, EmitterSink& sink);
    void select(EmitterSink& sink);

    // +8 and +0xc.
    void set_volume(float v) { volume_ = v; }
    void set_enabled(bool on) { enabled_ = on; }
    float volume() const { return volume_; }

    // The entry playing (+4), or nullptr.
    const MusicEntry* playing() const { return current_ >= 0 ? &entries_[static_cast<std::size_t>(current_)] : nullptr; }
    // Once a frame: the asset sweep for music voices that have ended (finish_when_done).
    void reap(EmitterSink& sink);
    // Every voice released at once (shutdown, tests).
    void release_all(EmitterSink& sink);

private:
    struct Voice {
        std::string file;
        std::uint32_t voice = 0;
        bool finish_when_done = false; // the instance's +0x1d
    };
    Voice* voice_of(const std::string& file);

    std::vector<MusicEntry> entries_;
    int current_ = -1;
    float volume_ = 1.0f;
    bool enabled_ = true;
    std::vector<Voice> voices_; // per file: its voice ("the asset is playing")
};

} // namespace hailstone::audio
