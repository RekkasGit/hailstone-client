#pragma once

// How the weather (game/weather.h) looks: the precipitation manager's drops, flakes and splashes
// (game/precipitation.h) as the original's CRender draws them (0x1007fd10: its definitions' textures, sizes,
// animation frames and orientations, the sky's colours; shaders/precipitation.vert), and for the modern lighting
// the world's response, WeatherLook: wet ground that dries afterwards, puddles in its low spots ringed by the drops,
// snow lying on the level surfaces of the ground and what stands on it, the characters' wet sheen, the storm's
// darker light and the lightning's flash. WeatherLook is not the original's look; docs/graphics.md, "Weather".

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "game/precipitation.h"
#include "game/weather.h"
#include "render/gpu.h"
#include "render/vk_context.h"

namespace nmsc { class GameDir; }

namespace nmsc::render {

struct Daylight;
struct SceneLighting;

// The weather's mark on the world, built up and worn off over time. The original's weather changes over a second
// (the precipitation's 0.25 a second); the look eases it over tens of seconds: the clouds gather before the rain
// grows heavy and break up after it stops.
struct WeatherLook {
    // 0..1: how far the storm holds the sky (the server's intensity 10 is 1): the cloud cover, the sun's dimming, the
    // fog band. Eased towards the falling precipitation, gathering over about kOvercastIn and clearing over
    // kOvercastOut.
    float storm = 0.0f;
    // The precipitation drawn (the manager's p, 0..0.1 for intensities 1..10): eased towards the server's over about
    // kFallEase either way, so the drops thicken and thin instead of starting and stopping at once.
    float precipitation = 0.0f;
    float snowing = 0.0f; // 0..1: the share of it that is snow (eased over a few seconds)
    float wet = 0.0f;   // 0..1: the ground's wetness (30 s of rain to soak, 2 minutes to dry)
    float snow = 0.0f;  // 0..1: snow lying on level surfaces (2 minutes to cover, 5 to melt)
    // 0..1: the lightning's light: a strike's flicker (three flashes over a fifth of a second) and its afterglow.
    float flash = 0.0f;
    float rain = 0.0f;  // 0..1: the rain falling now: the rings on standing water
    float puddles = 0.0f; // 0..1: the water standing in the ground's low spots (90 s of rain to fill, 4 minutes to dry)
    float strike_age = 10.0f; // seconds since the last lightning strike

    static constexpr float kOvercastIn = 36.0f;  // seconds to about 95 %
    static constexpr float kOvercastOut = 75.0f;
    static constexpr float kFallEase = 18.0f;

    // The look belongs to a zone: `zone` (the zone id; 0 not known) other than the last one's starts it over, dry and
    // clear, so the snow lying in one zone or its storm's sky does not follow the player into the next.
    void update(const game::Weather& weather, float dt, std::uint32_t zone = 0);

private:
    std::uint32_t zone_ = 0;
};

// The lightning's light `age` seconds after a strike (0..1): a flash, a dimmer one 70 ms on and a third at 190 ms,
// each gone in a few frames, over a faint afterglow.
float lightning_flash(float age);

// The look into the lighting (SceneLighting::weather; the flash in time.z), for shaders/weather.glsl.
void set_weather(SceneLighting& lighting, const WeatherLook& look);

// The storm darkens the sun, greys the sky's light and dims the baked colour (Daylight::baked) a little.
void apply_storm(Daylight& daylight, float storm);

// The wind (SceneLighting::wind): xy the way it blows, z its strength, w its gusts'. A breeze in fair weather, and
// harder with the storm (WeatherLook::storm). It moves the plants (shaders/zone.vert) and slants the rain.
glm::vec4 wind_at(double seconds, float storm);

// How the falling drops are drawn. The classic style is the original's: the definitions' sizes, at least a pixel
// wide. The original's rain (1-unit streaks a pixel wide, a few hundred over 200 units round the camera) all but
// vanishes at today's resolutions, so the modern lighting stretches each upright streak over the distance it falls
// in `streak_seconds` (a camera's motion blur), draws it at least `min_pixels` wide and has the precipitation
// manager start `density` times as many (its spawn rate only: the pool and the columns are the original's).
// Not the original's look; docs/graphics.md, "Weather".
struct PrecipitationStyle {
    float streak_seconds = 0.0f;
    float min_pixels = 1.0f;
    float density = 1.0f;
};
inline constexpr PrecipitationStyle kClassicPrecipitation{};
inline constexpr PrecipitationStyle kModernPrecipitation{0.08f, 1.5f, 3.0f};

class WeatherRenderer {
public:
    static constexpr std::uint32_t kMaxDrops = game::PrecipitationField::kParticles;
    static constexpr std::uint32_t kMaxTextures = 16;
    // The original draws a particle only beyond 2.5 units in front of the camera (0x1007fd10).
    static constexpr float kNearest = 2.5f;

    WeatherRenderer(VkContext& vk, const std::filesystem::path& shader_dir);
    ~WeatherRenderer();
    WeatherRenderer(const WeatherRenderer&) = delete;
    WeatherRenderer& operator=(const WeatherRenderer&) = delete;

    // The definitions' textures (Resources/Precipitation/<File>.dds); call outside a frame.
    void load_textures(const GameDir& game, const game::PrecipitationDefinitions& defs);

    // The field's falling and splashing particles, depth tested against the scene with no depth write. `forward`
    // is the camera's direction, `pixel` the world size of a pixel one unit in front of it (the drops are at least
    // a pixel wide, as the original's), `light` scales the sky's colours (1: as they are), `style` the streaks'
    // length and width.
    void draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, const glm::vec3& forward,
              float pixel, const game::PrecipitationField& field, float light = 1.0f,
              const PrecipitationStyle& style = kClassicPrecipitation);

private:
    std::uint32_t texture_slot(const std::string& file) const;

    VkContext& vk_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    std::vector<Texture> textures_; // slot 0: white
    std::unordered_map<std::string, std::uint32_t> slots_; // lower-case file -> slot
    // One particle, per instance.
    struct GpuDrop {
        glm::vec4 position; // xyz, w: the orientation (0 facing the camera, 1 upright, 2 flat)
        glm::vec4 size;     // x width, y height
        glm::vec4 uv;       // the animation frame's u0, v0, u1, v1
        glm::vec4 color;    // linear rgb, a
    };
    // draw's: the drops by texture, kept from frame to frame (their room reused, not allocated each frame).
    std::array<std::vector<GpuDrop>, kMaxTextures> groups_;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
};

} // namespace nmsc::render
