#pragma once

// Draws game::ParticleSystem's particles as camera-facing quads. Textures
// come from the game directory's SpellEffects\, EnvEmitterEffects\ and
// ActorEffects\ (searched in that order). EDD +0x68 picks DESTBLEND ONE
// (additive) over INVSRCALPHA; +0x64 turns depth writes off.
//
// The modern lighting (docs/graphics.md, Particles) draws them its own way: one stream sorted back to front, no depth
// writes, soft where they meet the scene (VkContext::begin_translucent's depth copy, so drawn after it), in the haze,
// and the alpha-blended ones (smoke, dust) lit by the scene's sky, sun or moon and point lights.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "core/latest_jobs.h"
#include "render/gpu.h"
#include "render/particle_textures.h"
#include "render/vk_context.h"

namespace hailstone { class GameDir; }
namespace hailstone::game { class ParticleSystem; }

namespace hailstone::render {

struct SceneLighting;

// The modern light on a puff of smoke or dust (or a plant, render/flora_renderer.h) at `p`, as the objects' shading
// without a normal; and shaders/lighting.glsl apply_haze there: what shows through, the colour in front, and under
// water what the water absorbs on the way.
glm::vec3 particle_light(const SceneLighting& l, const glm::vec3& p);
// particle_light with the frame's part worked out once (the ambient and the lights' linear colours): the same light,
// for drawing many particles or plants under one SceneLighting.
class ParticleLight {
public:
    explicit ParticleLight(const SceneLighting& l);
    glm::vec3 at(const glm::vec3& p) const;

private:
    struct Light {
        glm::vec3 position{0.0f};
        float radius = 0.0f;
        glm::vec3 color{0.0f}; // linear
    };
    glm::vec3 base_{0.0f};
    float floor_ = 0.0f;
    std::vector<Light> lights_;
};
struct ParticleHaze {
    glm::vec3 color{0.0f};
    float through = 1.0f;
    glm::vec3 absorb{1.0f};
};
ParticleHaze haze_at(const SceneLighting& l, const glm::vec3& p);

struct SceneLighting;

// The modern lighting's scale for additive (glowing) particles: spells and fires light up past white and bloom.
constexpr float kParticleGlow = 2.5f;

// The quad's axes for an EDD billboard mode (+0x70; this client's reading of the names that use them): 0 facing the
// camera (right and up left as they are), 1 standing along the emitter's axial direction (axes column 2) and turned
// about it towards the eye ("ray", "teleport", torch fires), 2 lying flat in the emitter's transverse plane (columns
// 0 and 1: rune circles, rings, "_horz" water ripples). `from_eye` is the particle's position less the eye's.
void particle_basis(std::uint32_t billboard, const glm::mat3& axes, const glm::vec3& from_eye, glm::vec3& right,
                    glm::vec3& up);

struct Frustum;
// Whether a particle's quad can show in the camera's view volume (render/culling.h): the sphere it may turn in, its
// half-diagonal, against the frustum. Conservative, as the zone's chunk tests: nothing that shows is dropped.
bool particle_in_view(const Frustum& view, const glm::vec3& position, float width, float height, float size_factor);

class ParticleRenderer {
public:
    static constexpr std::uint32_t kMaxTextures = 512; // at most; fewer where the device allows fewer
    static constexpr std::uint32_t kMaxVertices = 6 * 20000;

    ParticleRenderer(VkContext& vk, const GameDir& game, const std::filesystem::path& shader_dir);
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // A source tried before the game directory's effect folders: the zone's
    // archives for the WLD particle clouds' textures (GENG00.DDS, CSMOKE.DDS
    // live in <zone>_obj.s3d).
    using TextureReader = std::function<std::optional<std::vector<std::byte>>(const std::string&)>;
    void set_texture_reader(TextureReader reader) { reader_ = std::move(reader); }
    // Texture rows top to bottom as the DLL's quad corners take them: its
    // particle vertices carry v = 1 at the top corners and v = 0 at the bottom
    // (OpenEQ docs/WLD_PARTICLE_SAMPLER.md, captured from the CPU quad
    // builder), so a top-origin DDS shows its last row at the top. Set for the
    // WLD clouds (a flame texture's tail then points up); the spell particles
    // keep the first row at the top until that is checked against them.
    void set_rows_bottom_up(bool on) { rows_bottom_up_ = on; }
    // Asks for the textures the live emitters need, and puts up those the loader thread has finished (a frame or
    // more later: until then their particles are not drawn); call outside a frame.
    void prepare(const game::ParticleSystem& particles);
    // `glow` scales the additive particles' light (the modern lighting's HDR: above 1 they bloom). The emitters
    // whose entry in `hidden` (by emitter index, when given) is non-zero are left out. With `modern` (the frame's
    // modern SceneLighting, its haze and screen set) they are drawn the modern way, after begin_translucent.
    void draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::mat4& view,
              const game::ParticleSystem& particles, float glow = 1.0f, std::span<const std::uint8_t> hidden = {},
              const SceneLighting* modern = nullptr);
    // A loaded texture's colour, its brightest channel 1 (what its additive particles add, on average): the hue of
    // the light they give off (game::particle_lights). White for one not loaded.
    glm::vec3 texture_color(const std::string& name) const;

private:
    std::uint32_t texture_slot(const std::string& name) const;
    void create_pipelines(const std::filesystem::path& shader_dir);

    VkContext& vk_;
    std::uint32_t max_textures_ = 0; // kMaxTextures, within DeviceCaps::max_sampled_images
    const GameDir& game_;
    TextureReader reader_;
    bool rows_bottom_up_ = false;
    std::vector<Texture> textures_;               // slot 0: white
    std::unordered_map<std::string, std::uint32_t> slots_; // lower-case name -> slot (0 = missing)
    std::unordered_map<std::string, glm::vec3> colors_;    // lower-case name -> texture_color
    std::uint32_t version_ = 1;
    std::array<std::uint32_t, VkContext::kFramesInFlight> set_version_{};
    std::array<Buffer, VkContext::kFramesInFlight> vertices_{};
    std::array<void*, VkContext::kFramesInFlight> mapped_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, VkContext::kFramesInFlight> sets_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipelines_[4]{}; // bit 0: additive, bit 1: no depth write
    VkPipeline modern_pipeline_ = VK_NULL_HANDLE; // premultiplied: both kinds in one sorted stream, no depth write

    // Textures asked for and not put up yet (lower case), and the thread reading and decoding them
    // (load_particle_texture). Last, so it stops first: its jobs use game_.
    std::unordered_set<std::string> loading_;
    std::unique_ptr<core::LatestJobs<std::string, LoadedParticleTexture>> loader_;
};

} // namespace hailstone::render
