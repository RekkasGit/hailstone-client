#include "render/ambient_probe.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "assets/zone.h"

namespace hailstone::render {

std::int64_t AmbientProbe::key(int x, int y, int z) {
    return (static_cast<std::int64_t>(x & 0x1fffff) << 42) | (static_cast<std::int64_t>(y & 0x1fffff) << 21) |
           static_cast<std::int64_t>(z & 0x1fffff);
}

void AmbientProbe::add(const glm::vec3& p, std::uint32_t c) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
    glm::ivec3 i = glm::ivec3(glm::floor(p / kCell));
    Cell& cell = cells_[key(i.x, i.y, i.z)];
    // 0xAABBGGRR
    cell.sum += glm::vec4(static_cast<float>(c & 0xff), static_cast<float>((c >> 8) & 0xff),
                          static_cast<float>((c >> 16) & 0xff), static_cast<float>(c >> 24)) / 255.0f;
    cell.centre += p;
    cell.count += 1.0f;
}

AmbientProbe::AmbientProbe(const assets::Zone& zone) {
    for (const auto& mesh : zone.terrain)
        for (const auto& v : mesh.vertices)
            if (v.color != 0xffffffffu) add(v.position, v.color);
    for (const auto& p : zone.placements) {
        auto it = zone.objects.find(p.actor);
        if (it == zone.objects.end()) continue;
        const auto& vertices = it->second.vertices;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            std::uint32_t c = i < p.vertex_colors.size() ? p.vertex_colors[i] : vertices[i].color;
            if (c == 0xffffffffu) continue;
            add(glm::vec3(p.transform * glm::vec4(vertices[i].position, 1.0f)), c);
        }
    }
    for (auto& [k, cell] : cells_) cell.centre /= cell.count;
}

glm::vec4 AmbientProbe::sample(const glm::vec3& p) const {
    if (cells_.empty()) return glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
    glm::ivec3 c = glm::ivec3(glm::floor(p / kCell));
    glm::vec4 sum(0.0f);
    float weight = 0.0f;
    for (int z = -kReach; z <= kReach; ++z)
        for (int y = -kReach; y <= kReach; ++y)
            for (int x = -kReach; x <= kReach; ++x) {
                auto it = cells_.find(key(c.x + x, c.y + y, c.z + z));
                if (it == cells_.end()) continue;
                const Cell& cell = it->second;
                glm::vec3 d = cell.centre - p;
                // Near surfaces count most; a cell's many vertices count a little more than a few.
                float w = std::sqrt(cell.count) / (1.0f + glm::dot(d, d) / (0.5f * kCell * kCell));
                sum += cell.sum / cell.count * w;
                weight += w;
            }
    if (weight <= 0.0f) return glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
    return sum / weight;
}

glm::vec4 ProbeBlend::update(const glm::vec4& sample, float dt) {
    if (sample.w < 0.0f) return valid_ ? current_ : glm::vec4(0.0f);
    glm::vec4 target(glm::vec3(sample), 1.0f + std::clamp(sample.w, 0.0f, 1.0f));
    if (!valid_) {
        current_ = target;
        valid_ = true;
        return current_;
    }
    float k = 1.0f - std::exp(-std::clamp(dt, 0.0f, 1.0f) * 6.0f);
    current_ = glm::mix(current_, target, k);
    return current_;
}

} // namespace hailstone::render
