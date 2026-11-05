#include "game/zone_lines.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "game/collision.h"
#include "game/zoning.h"

namespace nmsc::game {

std::string region_teleport_text(std::string_view tag) {
    if (!tag.empty() && tag[0] == 'A') return atp_teleport_text(tag);
    return std::string(tag);
}

RegionNameFlags parse_region_name_flags(const char* name, std::uint32_t flags) {
    RegionNameFlags r;
    r.flags = flags;
    if (!name) return r;
    std::string_view n(name);
    r.flags = assets::region_flags_from_tag(n, flags);
    if (n.size() < 4) return r;
    if (n[0] == 'A') {
        if (n.starts_with("ATP")) r.teleport_name = atp_teleport_text(n);
    } else if (n[3] == 'T' && n.size() > 4 && n[4] == 'P') {
        r.teleport_name = std::string(n);
    }
    return r;
}

std::vector<BuriedZoneLine> survey_zone_lines(const assets::ZoneRegions& regions, const CollisionMesh& collision) {
    std::vector<BuriedZoneLine> out;
    if (!regions.has_boxes()) return out;
    auto want = [&](std::uint32_t r) { return regions.info(r).is_zone_line(); };
    for (const auto& c : regions.leaf_cells(glm::vec3(-1e6f), glm::vec3(1e6f), want)) {
        BuriedZoneLine b;
        b.region = c.region;
        b.tag = regions.info(c.region).tag;
        b.centre_z = 0.5f * (c.lo.z + c.hi.z);
        b.box_lo_z = c.lo.z;
        b.box_hi_z = c.hi.z;
        b.ground_lo = std::numeric_limits<float>::max();
        b.ground_hi = std::numeric_limits<float>::lowest();
        // Up to 16 x 16 samples over the box's footprint, at the centres of cells at least 4 units wide.
        constexpr int kMaxSteps = 16;
        const glm::vec2 size(c.hi.x - c.lo.x, c.hi.y - c.lo.y);
        const int nx = std::clamp(static_cast<int>(size.x / 4.0f), 1, kMaxSteps);
        const int ny = std::clamp(static_cast<int>(size.y / 4.0f), 1, kMaxSteps);
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j) {
                const glm::vec3 p(c.lo.x + size.x * (static_cast<float>(i) + 0.5f) / static_cast<float>(nx),
                                  c.lo.y + size.y * (static_cast<float>(j) + 0.5f) / static_cast<float>(ny), b.centre_z);
                if (!regions.box_contains(c.region, p)) continue;
                ++b.samples;
                // A floor in the box or just under it: the box is reached from there.
                if (auto f = collision.floor_below(glm::vec3(p.x, p.y, c.hi.z), 0.0f); f && *f >= c.lo.z - kBuriedFloorReach)
                    ++b.reachable;
                auto g = collision.floor_below(glm::vec3(p.x, p.y, 1.0e6f), 0.0f);
                if (!g || *g <= c.hi.z) continue; // nothing over it
                ++b.covered;
                b.ground_lo = std::min(b.ground_lo, *g);
                b.ground_hi = std::max(b.ground_hi, *g);
                if (*g < c.hi.z + kBuriedMargin) ++b.shallow;
                // Inside the ground: going up, more tops than undersides (a closed object adds one of each, a
                // heightmap only its top; a cave's or a room's roof adds an underside).
                int balance = 0;
                for (const auto& h : collision.surfaces_above(glm::vec3(p.x, p.y, c.hi.z)))
                    balance += h.normal.z > 0.0f ? 1 : h.normal.z < 0.0f ? -1 : 0;
                if (balance > 0) ++b.inside;
            }
        b.buried = b.samples > 0 && b.reachable == 0 && b.shallow == 0 &&
                   static_cast<float>(b.covered) >= kBuriedCover * static_cast<float>(b.samples) &&
                   static_cast<float>(b.inside) >= kBuriedCover * static_cast<float>(b.covered);
        b.top_z = b.covered ? b.ground_hi + kBuriedHeadroom : b.box_hi_z;
        out.push_back(std::move(b));
    }
    return out;
}

std::vector<BuriedZoneLine> find_buried_zone_lines(const assets::ZoneRegions& regions, const CollisionMesh& collision) {
    auto all = survey_zone_lines(regions, collision);
    std::erase_if(all, [](const BuriedZoneLine& b) { return !b.buried; });
    return all;
}

std::optional<std::string> ZoneLineTrigger::update(const glm::vec3& feet, const assets::ZoneRegions& regions) {
    if (regions.empty()) return std::nullopt;
    auto info = regions.lookup(feet + glm::vec3(0.0f, 0.0f, kRegionProbeHeight));
    // Buried zone lines (find_buried_zone_lines): the feet over the footprint, between the box's bottom and the
    // headroom over the ground; those zoned in over are held until the feet leave them.
    auto over = [&](const BuriedZoneLine& b) {
        return feet.z >= b.box_lo_z && feet.z <= b.top_z &&
               regions.box_contains(b.region, glm::vec3(feet.x, feet.y, b.centre_z));
    };
    if (arrived_) {
        arrived_ = false;
        held_.clear();
        for (const auto& b : buried_)
            if (over(b)) held_.push_back(b.region);
    }
    std::erase_if(held_, [&](std::uint32_t r) {
        return std::none_of(buried_.begin(), buried_.end(), [&](const BuriedZoneLine& b) { return b.region == r && over(b); });
    });
    if (!info.is_zone_line())
        for (const auto& b : buried_)
            if (std::find(held_.begin(), held_.end(), b.region) == held_.end() && over(b)) {
                info = regions.info(b.region);
                break;
            }
    if (info.flags == last_flags_) return std::nullopt;
    last_flags_ = info.flags;
    if (!info.is_zone_line()) return std::nullopt;
    return region_teleport_text(info.tag);
}

} // namespace nmsc::game
