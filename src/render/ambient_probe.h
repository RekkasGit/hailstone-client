#pragma once

// The zone's baked light round a point, for what the modern lighting draws without it (this client's own;
// docs/graphics.md "Modern lighting"). The zone geometry is lit by its vertex colours (the builders' baked light, their
// alpha the sky's share) as well as the sky and the sun; the characters and the doors have no such colours, and were
// lit by the open sky wherever they stood: brighter than the walls inside a building or a dungeon, flat beside them.
// The probe averages the vertex colours of the zone round a point, so they take the light of the place they stand in.

#include <cstdint>
#include <unordered_map>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace nmsc::assets { struct Zone; }

namespace nmsc::render {

class AmbientProbe {
public:
    static constexpr float kCell = 24.0f;  // world units a side
    static constexpr int kReach = 2;       // cells each way a sample reads

    AmbientProbe() = default;
    // The zone's terrain and placed objects; uncoloured meshes (every vertex opaque white) are left out.
    explicit AmbientProbe(const assets::Zone& zone);

    bool empty() const { return cells_.empty(); }
    // The baked light at `p`: rgb the vertex colour (display values), a the sky's share; weighted towards the nearest
    // vertices. nullopt-like: w < 0 when no vertex lies within reach.
    glm::vec4 sample(const glm::vec3& p) const;

    // Adds one vertex (tests, and the constructor).
    void add(const glm::vec3& p, std::uint32_t rgba);

private:
    struct Cell {
        glm::vec4 sum{0.0f}; // rgb, a
        glm::vec3 centre{0.0f};
        float count = 0.0f;
    };
    static std::int64_t key(int x, int y, int z);
    std::unordered_map<std::int64_t, Cell> cells_;
};

// The probe for the shaders (SceneLighting::probe): rgb the baked colour, w 1 + the sky's share; w 0 none. Eased over
// about half a second, so walking through a doorway does not pop.
class ProbeBlend {
public:
    void reset() { valid_ = false; }
    glm::vec4 update(const glm::vec4& sample, float dt);

private:
    glm::vec4 current_{0.0f};
    bool valid_ = false;
};

} // namespace nmsc::render
