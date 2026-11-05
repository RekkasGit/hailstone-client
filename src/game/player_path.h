#pragma once

// The find path (pinstPlayerPath, rof2decomp 0x59a060..0x59da00, decompiled in a scratch copy of the Ghidra
// database; the request side is rewritten in rof2decomp re/src/game/spawn/player_path_find.cpp). The path itself is
// the server's: the client asks with OP_FindPersonRequest (Send_OP_FindPersonRequest @0x59a420) and draws what
// OP_FindPersonReply brings (PlayerPath_HandleFindPersonReply @0x59c870). No path data is read from the game folder;
// SpellEffects\Path.ini only shapes the path (PlayerPath_LoadSettings @0x59b310).
//
// The points are kept as the original keeps them: index 0 at the destination, the last one where the player stands
// (the reply is walked from its end). PlayerPath_Update @0x59d280 runs every frame (CDisplay::RealRender_World): it
// trims the path behind the player and extends it to where the player is (FUN_0059cfa0), and ends it within
// FinishRange of the destination (PlayerPath_OnDestinationReached @0x59c4a0).
//
// Coordinates are this client's world axes (game/coords.h), which are the original's (Y, X, Z) of a spawn
// (PlayerBase +0x64, +0x68, +0x6c) and the packet's FindPerson_Point order.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace nmsc { class IniFile; }

namespace nmsc::game {

// What a find looks for (+0x18; the request's first dword).
enum class FindKind : std::uint32_t {
    Spawn = 1,          // PlayerPath_FindSpawn / FindSpawnByID / FindGroupMember / FindRaidMember: +0x1c the spawn id
    ZoneConnection = 2, // FUN_0059c210 (a Find Location row of kind 2): +0x1c its id
    RealEstateItem = 3, // PlayerPath_FindRealEstateItem: the item's GUID, +0x34 / +0x38 its real estate ids
    RealEstatePlot = 4, // PlayerPath_FindRealEstatePlot: +0x34 the plot
    MapLocation = 5,    // PlayerPath_FindMapLocation: +0x3c the point (the map's right click)
    Switch = 6,         // FindManager::SetDestSwitch: +0x1c the switch id
    Point = 7,          // FUN_0059c3c0: +0x3c the point
};

// SpellEffects\Path.ini [Path] (PlayerPath_LoadSettings; the defaults are its GetFloat / GetInt fallbacks).
struct PathEmitterSettings {
    int definition = -1; // Definition: the spell emitter (spellsnew.edd record), -1 none
    int life_span = 10;  // LifeSpan (ms)
    float density = 1.0f, opacity = 1.0f;
};

struct PathSettings {
    float segment_length = 5.0f;         // +0x4c SegmentLength: the drawn line's step
    float lod_factor = 0.0f;             // +0x13c LODFactor: the step grows with the distance from the player
    float control_segment_length = 5.0f; // +0x50 ControlSegmentLength: longer gaps get points on the way
    float merge_distance = 5.0f;         // +0x54 MergeDistance: closer reply points are dropped
    float finish_range = 10.0f;          // +0x10 FinishRange
    float floor_offset = 1.0f;           // FloorOffset, times the player's AvatarHeight (+0x138)
    std::string texture;                 // Texture (SpellEffects\<name>.tga, or <name><n>.tga for FrameCount n)
    int frame_count = 0;                 // FrameCount
    int frame_ms = 0;                    // +0x17c Duration
    float texture_scale = 1.0f;          // +0x188 TextureScale
    float texture_speed = 0.0f;          // +0x18c TextureSpeed
    std::uint8_t alpha = 0x80;           // +0x80 Alpha
    float fade_start = 80.0f, fade_end = 110.0f;  // +0x84 / +0x88 FadeStart / FadeEnd (ordered)
    float opaque_start = 10.0f, opaque_end = 20.0f; // +0x98 / +0x9c OpaqueStart / OpaqueEnd (ordered)
    float initial_length = 1.0f;         // +0x12c InitialLength
    float grow_speed = 1.0f;             // +0x134 GrowSpeed (per ms)
    float scale_min = 0.5f, scale_max = 1.5f; // +0xc0 / +0xc4 ScaleMin / ScaleMax (ordered)
    float scale_speed = 1.0f;            // +0xd4 ScaleSpeed
    glm::vec3 sine_period{0.0f}, sine_radius{0.0f}, sine_speed{0.0f}, sine_offset{0.0f}; // Sine*X / Y / Z
    float twist_factor = 0.0f, twist_speed = 1.0f; // +0x118 / +0x11c
    float curve_scale = 20.0f;           // +0x124 CurveScale: the spline's tangent handles
    float emitter_speed = 1.0f;          // +0x37c EmitterSpeed (units per ms)
    int emitter_period = 1000;           // +0x380 EmitterPeriod (ms)
    float emitter_start = 10.0f;         // +0x378 EmitterStartCoordinate
    int max_emitters = 0;                // +0x38c MaxEmitterCount (0 .. 20)
    float sound_distance = 10.0f, sound_loop_volume = 4.0f, sound_loop_speed = 1.0f; // +0x424 / +0x428 / +0x420
    bool additive = true;                // Additive
    std::array<PathEmitterSettings, 4> emitters{};        // Emitter0..3 -> [section] Definition / LifeSpan / ...
    std::array<PathEmitterSettings, 4> finish_emitters{}; // FinishEmitter0..3
};

// Path.ini as PlayerPath_LoadSettings reads it (null: every default).
PathSettings load_path_settings(const IniFile* ini);

// The sounds the path plays (EqSoundManager::WavePlay ids).
inline constexpr std::uint32_t kPathSoundStart = 0x50f;    // the reply drew a path
inline constexpr std::uint32_t kPathSoundLoop = 0x510;     // the loop on the path (an EqMobileEmitter)
inline constexpr std::uint32_t kPathSoundComplete = 0x511; // arrived
inline constexpr std::uint32_t kPathSoundAbort = 0x512;    // PlayerPath_Stop
// The chat lines (CEverQuest::dsp_chat, default colour).
inline constexpr std::uint32_t kStrPathAppears = 6775;  // 0x1a77 "A mystical path appears before you."
inline constexpr std::uint32_t kStrPathFades = 6776;    // 0x1a78 "The mystical path fades away."
inline constexpr std::uint32_t kStrPathPrevented = 6779; // 0x1a7b "Mysterious forces prevent you ..."

// OP_FindPersonRequest's fields (Send_OP_FindPersonRequest: the kind, the id's low 16 bits, +0x34, +0x38, the
// player's position, the point).
struct FindRequest {
    FindKind kind = FindKind::Spawn;
    std::uint16_t id = 0xffff;
    std::int32_t item_a = -1, item_b = -1;
    glm::vec3 point{0.0f};
};

class PlayerPath {
public:
    // The floor under a point (the highest floor no higher than p.z + step_up), as CollisionMesh::floor_below.
    using FloorFn = std::function<std::optional<float>(const glm::vec3& p, float step_up)>;

    // ---- finding (each stops the current path first: PlayerPath_Stop, which plays kPathSoundAbort; then the request
    // to send; then the settings are read again, which the caller does with set_settings)
    // PlayerPath_FindSpawnByID @0x59c0d0 (and FindSpawn / FindGroupMember / FindRaidMember once the spawn passed):
    // `face` turns the player toward the path when it comes (+0x15).
    FindRequest find_spawn(std::uint16_t id, bool face, std::uint32_t now_ms = 0);
    // PlayerPath_FindMapLocation @0x59c310: the map's right click, at `point` (the player's height).
    FindRequest find_map_location(const glm::vec3& point, bool face, std::uint32_t now_ms = 0);
    // FUN_0059c210 / FindManager::SetDestSwitch / FUN_0059c3c0: the Find Location rows of kinds 2, 6 and 7.
    FindRequest find_target(FindKind kind, std::int32_t id, bool face, std::uint32_t now_ms = 0);
    FindRequest find_point(const glm::vec3& point, bool face, std::uint32_t now_ms = 0);
    // PlayerPath_FindRealEstatePlot @0x59c2c0.
    FindRequest find_plot(std::int32_t plot, bool face, std::uint32_t now_ms = 0);
    // Send_OP_FindPersonRequest: the request again (FUN_0059a9c0 on zoning while active; RefreshIfMoved).
    FindRequest request(std::uint32_t now_ms);
    // PlayerPath_RefreshIfMoved @0x59c420: a request when 3 s passed since the last and `player` is 25 or more from
    // the path's current point (+0x68). Nothing calls it in RoF2.
    std::optional<FindRequest> refresh_if_moved(const glm::vec3& player, std::uint32_t now_ms);
    // PlayerPath_Stop @0x59a950: the path ends (the caller plays kPathSoundAbort).
    void stop();

    // ---- the reply (PlayerPath_HandleFindPersonReply): the packet's points in order (count = size / 12).
    struct Reply {
        // Fewer than two points (or more than 10000 after the extra points): kStrPathPrevented and the path ends;
        // with exactly one point that is not (0, 0, 0) the map shows it as a failed find (FUN_006c92e0: the map
        // window opens) with the find's kind.
        bool failed = false;
        std::optional<glm::vec3> failed_point;
        FindKind failed_kind = FindKind::Spawn;
        // Started: kPathSoundStart and kStrPathAppears; FindLocationWnd closes (its Show(false) before the call).
        bool started = false;
        // With +0x15 set: PlayerZoneClient_FaceLocation toward the mean of points 1..3 of the reply.
        std::optional<glm::vec3> face;
    };
    // `gm`: the player's vfunc +0xdc (a GM's path is not put on the floor). `floor`: the floor probe for the reply's
    // points (FUN_008d4120, then FUN_008d42e0 within 50 above); null leaves them where they are.
    Reply handle_reply(std::span<const glm::vec3> points, std::uint32_t now_ms, bool gm, const FloorFn& floor);

    // ---- every frame (PlayerPath_Update, with the player's position)
    struct Update {
        // PlayerPath_OnDestinationReached: kPathSoundComplete, kStrPathFades, the finish emitters at `finish_at`.
        bool arrived = false;
        glm::vec3 finish_at{0.0f};
    };
    // `zone`: the current zone id (a new zone ends the path silently). `gm` and `floor` as for handle_reply (the
    // player's point the line fades from is put on the floor).
    Update update(const glm::vec3& player, std::uint32_t now_ms, std::uint32_t zone, bool gm = false,
                  const FloorFn& floor = {});

    // ---- state
    bool active() const { return active_; }       // +0: a path is shown
    bool finished() const { return finished_; }   // +0x14: arrived (until the next reply)
    FindKind kind() const { return kind_; }       // +0x18
    std::int32_t target_id() const { return id_; } // +0x1c
    const glm::vec3& destination() const { return dest_; } // +0x160 (z follows the player for a map location)
    // The path, the destination first, the player's end last (+0x144 / +0x154).
    const std::vector<glm::vec3>& points() const { return points_; }
    int trim_index() const { return trim_index_; } // +0x64: the point the player has reached

    const PathSettings& settings() const { return settings_; }
    // PlayerPath_LoadSettings (after each find): the settings and the player's AvatarHeight for FloorOffset.
    void set_settings(const PathSettings& s, float avatar_height);
    float floor_offset() const { return floor_offset_; } // +0x138

    // ---- the line's look (PlayerPath_Update's drawing half: the thick line and its travelling emitters)
    // The visible stretch (+0xac / +0xb0): FUN_00599fd0 of the grown length (+0x130) capped by FadeStart / FadeEnd.
    float fade_near() const { return fade_near_; }
    float fade_far() const { return fade_far_; }
    // FUN_0059b040's alpha (0..255) of a line point `distance` from the player.
    int line_alpha(float distance) const;
    // The thick line's points this frame (FUN_0059b040 / FUN_0059b220): from the destination to the trim point, the
    // segments sampled every SegmentLength on a Catmull-Rom spline whose tangent handles are CurveScale long.
    struct LinePoint {
        glm::vec3 position{0.0f};
        float alpha = 0.0f;      // 0..1 (Alpha, faded by the distance from the player)
        float scale = 1.0f;      // ScaleMin .. ScaleMax
        float coordinate = 0.0f; // the distance from the destination along the path
    };
    const std::vector<LinePoint>& line() const { return line_; }
    // The travelling emitters (+0x198, 0x18 bytes each, MaxEmitterCount of them): EmitterN definitions placed at
    // their distance from the destination along the path (FUN_0059ab60).
    struct Mover {
        float coordinate = 0.0f;
        std::uint32_t born_ms = 0;
        std::uint32_t serial = 0; // a new mover (or a reused one) gets a new serial: its emitters are made again
        bool shown = true;        // FUN_0059a7c0: off the path, it stops emitting
        bool placed = false;
        glm::vec3 position{0.0f};
    };
    const std::vector<Mover>& movers() const { return movers_; }
    std::optional<glm::vec3> mover_position(const Mover& m) const;
    float trim_distance() const { return trim_distance_; } // +0x128: the trim point's distance from the destination
    float length() const { return total_; }

private:
    void clear();        // FUN_0059a910's fields and the movers
    void target(FindKind kind, std::int32_t id); // the fields the finds set
    void trim(const glm::vec3& player);           // FUN_0059cfa0
    void move_emitters(std::uint32_t now_ms, std::uint32_t dt); // FUN_0059a5f0
    void walk(const glm::vec3& eye, std::uint32_t now_ms);

    PathSettings settings_;
    float floor_offset_ = 1.0f;

    bool active_ = false, finished_ = false, face_ = false;
    std::uint32_t zone_ = 0xffffffff;
    FindKind kind_ = FindKind::Spawn;
    std::int32_t id_ = -1;
    std::int32_t item_a_ = -1, item_b_ = -1;
    glm::vec3 point_{0.0f};
    std::uint32_t request_ms_ = 0; // +0x48
    std::uint32_t frame_ms_ = 0;   // +0x8: the last update
    bool frame_known_ = false;
    glm::vec3 dest_{0.0f};
    std::vector<glm::vec3> points_;
    glm::vec3 tail_{0.0f}; // +0x58
    int trim_index_ = 0;   // +0x64
    int current_ = 0;      // +0x68
    std::uint32_t trim_ms_ = 0;      // +0x78
    glm::vec3 trim_from_{0.0f};      // +0x6c
    float grown_ = 0.0f;             // +0x130
    float trim_distance_ = 0.0f;     // +0x128
    float fade_near_ = 0.0f, fade_far_ = 0.0f;
    std::vector<Mover> movers_;
    std::uint32_t mover_ms_ = 0;     // +0x390
    std::uint32_t next_serial_ = 1;
    std::vector<LinePoint> line_;
    float total_ = 0.0f;
    glm::vec3 phase_{0.0f};          // +0x100: the sine phases
    float twist_ = 0.0f, uv_ = 0.0f; // +0x120 / +0x190
};

} // namespace nmsc::game
