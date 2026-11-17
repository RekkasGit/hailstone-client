#pragma once

// The distant copies (docs/graphics.md "Distant copies", render/zone_proxy.h): each zone the world atlas places past the
// current one, drawn far out as a low-detail copy baked from its own files, in place of the land round the zone there.
// Each is baked once (cached under cache/proxies/, keyed by the zone's files) and drawn as a render::ZoneRenderer of its
// own, loaded one at a time on a background thread at low priority once the zone is entered. They are drawn from the
// zone's edge out, the neighbour zones' too (copy_placements), in place of the neighbour zones drawn live; a neighbour
// zone drawn live (display.live_neighbour_zones) wins over its copy: the copy is not drawn while it is.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "app/neighbour_zones.h"
#include "app/world_atlas.h"
#include "render/zone_renderer.h"

namespace hailstone { class GameDir; }
namespace hailstone::game {
struct SceneLight;
struct ViewDistance;
} // namespace hailstone::game

namespace hailstone::app {

class DistantCopies {
public:
    DistantCopies(render::VkContext& vk, const GameDir& game, std::filesystem::path shader_dir);
    ~DistantCopies();
    DistantCopies(const DistantCopies&) = delete;
    DistantCopies& operator=(const DistantCopies&) = delete;

    struct Placed {
        std::string zone;
        glm::vec3 offset{0.0f};          // its coordinates + offset are the current zone's
        std::vector<glm::vec4> keep;     // game::atlas_cuts: kept where xyz . p + w >= 0
    };
    // After zone-in, with the land built: the zones to copy (the world atlas's, AtlasLand::placed_zones) and the land
    // (render::ZoneRenderer::horizon_build; null: none, and nothing is copied).
    void enter(std::vector<Placed> zones, std::shared_ptr<const render::HorizonBuild> horizon);
    // Zoning: drops every copy and abandons a load under way.
    void clear();
    // Each frame: uploads a copy that is ready, starts the next one's load. `live`: the neighbour zones drawn now
    // (their copies are not). `enabled` false: nothing is loaded or drawn.
    void update(bool enabled, const std::vector<std::string>& live);
    // Waits for every load (the viewer's screenshot).
    void finish_loading();

    void set_scene_light(const game::SceneLight& light);
    void set_lighting(const render::SceneLighting& lighting);
    void draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
              const game::ViewDistance& view, double time_seconds, render::ZonePhase phase);

    // The land's squares the copies drawn now cover (the current zone's set_distant_hidden), and a number that changes
    // when which are drawn does.
    void footprint(std::vector<std::int64_t>& out) const;
    std::uint64_t drawn_key() const;
    bool busy() const { return job_ != nullptr || next_ < zones_.size(); }
    std::size_t loaded() const;

private:
    struct Job;
    struct Copy {
        std::string zone;
        std::unique_ptr<render::ZoneRenderer> renderer;
        std::vector<std::int64_t> footprint;
        std::uint64_t id = 0;
    };
    void finish(Job& job);
    void start_next();
    static void run(std::shared_ptr<Job> job, const GameDir* game);

    render::VkContext& vk_;
    const GameDir& game_;
    std::filesystem::path shader_dir_;
    std::vector<Placed> zones_;
    std::size_t next_ = 0; // the next of zones_ to load
    std::shared_ptr<const render::HorizonBuild> horizon_;
    std::vector<Copy> copies_;
    std::vector<std::string> live_;
    bool enabled_ = false;
    std::uint64_t next_id_ = 1;
    std::shared_ptr<Job> job_;
    std::thread thread_;
    struct Abandoned {
        std::shared_ptr<Job> job;
        std::thread thread;
    };
    std::vector<Abandoned> abandoned_;
};

// The zones to copy: the world atlas's (`atlas`: its placements, each kept by its cuts), then each neighbour zone past
// a qualifying line the atlas did not place (one not visited on this server yet, or not outdoor there), at its own
// offset and kept past its seam.
std::vector<DistantCopies::Placed> copy_placements(const AtlasLand& atlas,
                                                   const std::vector<NeighbourZones::CopyTarget>& neighbours);

// Where `zone`'s copy is cached (cache/proxies/<zone>.bin, docs/layout.md).
std::filesystem::path zone_proxy_path(std::string_view zone);
// Whether `zone`'s copy is cached for the asset folder's files as they are (its file's header alone is read).
bool zone_proxy_cached(const GameDir& game, std::string_view zone);
// The land's sources with the trees of the zones whose copy is cached taken out (render::HorizonSource::trees): the
// copy draws that zone's own trees where they stand.
render::HorizonSources without_copied_trees(const GameDir& game, const render::HorizonSources& sources);

} // namespace hailstone::app
