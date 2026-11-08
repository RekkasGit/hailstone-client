#pragma once

// The falling rain and snow: the graphics DLL's CPrecipitationManager (EQGraphicsDX9.dll, vtable 0x1013aa18), its
// particles and their definitions. Read from the DLL (docs/graphics.md "Weather"):
//
//   0x100810a0  +0x30 load(name): resources\precipitation\<name>.ini, else default.ini when [Settings] Valid != 1
//   0x10080b30  a kind's definitions: [Settings] <Kind>Falling<n> / <Kind>Splashing<n> (n = 1..3) name the
//               [Falling<i>] / [Splashing<i>] sections; kinds Rain, Snow, Hail, Ice, Blood, Slime, Fire
//   0x1007f8e0  the constructor: a pool of count particles (7500 from Weather_StartEffect), all free
//   0x1007f3b0  +0x04 set kind (0xff: stop(1, 1))
//   0x1007fcb0  +0x18 set wind: normalised; the active particles' ground goes stale, stop(0, 1)
//   0x1007fa60  +0x2c stop(active, cached): those particles back to the free list (the intensity is kept)
//   0x10081860  +0x00 update(time): spawn round(count x dt x p) (at most 10 new ones, the rest on old columns),
//               move, land, splash, then 0x10080f10 sorts the dead out
//   0x100814e0  a new particle round the camera; 0x1007fb50 an old column again; 0x10081240 / 0x1007f7f0 set one up
//   0x1007fd10 / 0x1007f480  CRender's draw: screen-space quads, 2.5 units to the far plane
//
// The particles fall along the wind (0, 0, -1) from a point the camera's way, onto the first surface a line from
// 1200 units above them to 500 below meets (transparent surfaces ignored, passable ones hit, back faces ignored):
// so they stop on roofs, ledges and tree tops, as in the original. Hailstone also stops them on a back face below
// the particle (a roof with nothing over it); not above it, where a zone's ceiling would catch every drop.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace hailstone { class GameDir; class IniFile; }

namespace hailstone::game {

// The kinds, in the DLL's order (OP_Weather's kind indexes them: 0 rain, 1 snow; EQEmu's #weather 2 sends 2, hail).
inline constexpr std::array<std::string_view, 7> kPrecipitationKindNames = {"Rain", "Snow",  "Hail", "Ice",
                                                                             "Blood", "Slime", "Fire"};

// [Falling<i>]: how a drop or flake falls and looks. The defaults are the DLL's.
struct PrecipitationFalling {
    float max_speed = 32.152233f;    // units a second
    float acceleration = 32.152233f; // units a second, each second
    float width = 1.0f, height = 1.0f;
    float rate = 1.0f;               // animation frames a second
    std::int32_t frames = 0;         // 16: a 4 x 4 atlas, 4: 2 x 2, else the whole texture
    std::int32_t orientation = 0;    // 0 facing the camera, 1 upright (a streak Height long), 2 flat
    std::string file;                // Resources/Precipitation/<File>.dds
};

// [Splashing<i>]: what a drop leaves where it lands (Chance of it, for Life seconds).
struct PrecipitationSplash {
    float life = 32.152233f;
    float chance = 32.152233f;
    float width = 1.0f, height = 1.0f;
    float rate = 1.0f;
    std::int32_t frames = 0;
    std::int32_t orientation = 0;
    std::string file;
};

struct PrecipitationKindDefinitions {
    std::vector<PrecipitationFalling> falling;   // up to 3
    std::vector<PrecipitationSplash> splashing; // up to 3
};

struct PrecipitationDefinitions {
    std::array<PrecipitationKindDefinitions, kPrecipitationKindNames.size()> kinds;

    // The kind's definitions, or null for one out of range (0xff).
    const PrecipitationKindDefinitions* kind(std::int32_t k) const {
        return k >= 0 && static_cast<std::size_t>(k) < kinds.size() ? &kinds[static_cast<std::size_t>(k)] : nullptr;
    }

    static PrecipitationDefinitions parse(const IniFile& ini);
    // Resources/Precipitation/<name>.ini, or default.ini when that one has no [Settings] Valid=1; nullopt when
    // neither can be read.
    static std::optional<PrecipitationDefinitions> load(const GameDir& game, std::string_view name = "default");
};

// One particle (the DLL's 0x50-byte record).
struct PrecipitationParticle {
    enum State : std::int32_t { Dead = 0, Falling = 1, Splashing = 2 };
    std::int32_t falling = -1;      // +0x00: the falling definition
    std::int32_t splash = -1;       // +0x04: the splash definition
    float speed = 0.0f;             // +0x08
    float max_speed = 0.0f;         // +0x0c
    float acceleration = 0.0f;      // +0x10
    float start_speed = 0.0f;       // +0x14
    float splash_life = 0.0f;       // +0x18: seconds of splash left (0: none)
    float age = 0.0f;               // +0x1c: seconds since it started falling or landed (the animation's clock)
    glm::vec3 position{0.0f};       // +0x20
    glm::vec3 ground{0.0f};         // +0x2c: where it lands (0.05 above the surface)
    bool stale = false;             // +0x38: the wind changed since its ground was found
    std::int32_t state = Dead;      // +0x3c
    bool may_splash = true;         // +0x40: cleared for good once it found steep ground (normal z <= 0.8)
    std::uint32_t colour = 0;       // +0x44: 0xAARRGGBB
};

// The first surface on a segment: the fraction t along it and the surface normal's z.
struct PrecipitationHit {
    float t = 1.0f;
    float normal_z = 1.0f;
};
// The first surface from `from` to `to`; with `back_faces`, a triangle's back face counts as well as its front.
using PrecipitationGround =
    std::function<std::optional<PrecipitationHit>(const glm::vec3& from, const glm::vec3& to, bool back_faces)>;

class PrecipitationField {
public:
    // Weather_StartEffect's resource manager +0x9c(7500).
    static constexpr std::uint32_t kParticles = 7500;
    // At most this many new particles an update (0x10081860); the rest of the update's share falls on old columns.
    static constexpr std::int32_t kNewPerUpdate = 10;
    // A landed particle's column is reused while it is within 200 units of the camera (0x10080f10, 40000 squared).
    static constexpr float kColumnReach = 200.0f;

    explicit PrecipitationField(std::uint32_t count = kParticles);

    void set_definitions(PrecipitationDefinitions defs) { defs_ = std::move(defs); }
    const PrecipitationDefinitions& definitions() const { return defs_; }

    // +0x04 (0x1007f3b0): 0xff stops everything.
    void set_kind(std::int32_t kind);
    std::int32_t kind() const { return kind_; }
    // +0x18 (0x1007fcb0): the wind, normalised. The falling particles' ground goes stale and the landed columns are
    // forgotten (stop(false, true)).
    void set_wind(const glm::vec3& wind);
    const glm::vec3& wind() const { return wind_; }
    // +0x2c (0x1007fa60): the falling (active) and landed (cached) particles back to the free list.
    void stop(bool active, bool cached);

    struct Frame {
        float seconds = 0.0f;   // since the last update (the manager's +0x50)
        // On the game clock (core/step_clock.h, docs/mechanics.md "Frame rate"): the drawn frame's seconds, which the
        // particles move by, while the new particles are counted once per step of the game clock since the last
        // update (round(count x step x p) each, as at 60 frames a second). Below 0: the original's rule as written,
        // both over `seconds`.
        float motion_seconds = -1.0f;
        std::vector<float> spawn_steps; // the steps' seconds (filled in by Precipitation::update_particles)
        float intensity = 0.0f; // p (the manager's +0x10), already moved for this update
        // This client's own (the modern look, render::WeatherLook::precipitation): p eased over tens of seconds in
        // place of the manager's; negative keeps the manager's.
        float eased_intensity = -1.0f;
        glm::vec3 camera{0.0f};
        float heading = 0.0f;   // the camera's heading in 512ths of a circle, 0 along +x, 128 along +y
        // The sky's precipitation colours for the kind (CSky +0x38(2 kind), +0x38(2 kind + 1): colour map rows
        // 4 + 2 kind and 5 + 2 kind), 0xAARRGGBB; each particle takes a random blend of the two.
        std::array<std::uint32_t, 2> colours{0xffffffffu, 0xffffffffu};
        std::function<std::int32_t()> random; // rand(): 0..0x7fff
        // CCollision +0 (CollideLineSegment) with the precipitation filter, front faces over the whole probe (the
        // original's); back faces only from the particle down (Hailstone's, see launch).
        PrecipitationGround ground;
    };
    void update(const Frame& frame);

    std::span<const PrecipitationParticle> particles() const { return particles_; }
    // The particles falling or splashing, in the DLL's list order (the draw order).
    const std::vector<std::uint32_t>& active() const { return active_; }
    std::size_t cached_count() const { return cached_.size(); }
    std::size_t free_count() const { return free_.size(); }

private:
    bool spawn_new(const Frame& f);   // 0x100814e0
    bool spawn_column(const Frame& f); // 0x1007fb50
    void pick_definitions(const Frame& f, std::int32_t& falling, std::int32_t& splash, float& life) const;
    // 0x10081240: start falling from `at`, finding its ground.
    void launch(PrecipitationParticle& p, const glm::vec3& at, float speed, float acceleration, float max_speed,
                float life, std::int32_t splash, std::int32_t falling, const Frame& f);

    PrecipitationDefinitions defs_;
    std::vector<PrecipitationParticle> particles_;
    std::vector<std::uint32_t> active_; // +0x20 list
    // FIFO, as the original's lists: taken from the front, so a deque (a vector's erase(begin()) moved the whole pool
    // for every drop).
    std::deque<std::uint32_t> cached_; // +0x2c list (landed: their column can be reused)
    std::deque<std::uint32_t> free_;   // +0x38 list
    std::vector<std::uint32_t> still_; // update()'s scratch, kept between frames
    std::int32_t kind_ = 0xff;          // +0x0c
    glm::vec3 wind_{0.0f, 0.0f, -1.0f}; // +0x44 (DAT_10173058..60)
    bool wind_changed_ = false;         // DAT_1017c988
};

// FUN_1007ef70: a blended (1 - t) + b t per byte, alpha too, each rounded to the nearest.
std::uint32_t blend_precipitation_colour(std::uint32_t a, std::uint32_t b, float t);

} // namespace hailstone::game
