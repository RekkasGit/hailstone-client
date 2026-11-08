#pragma once

// Zone lines: when the player's feet enter a zone-line region, the original's
// PlayerClient::CheckRegionChange @0x508e10 (called from CollisionPass) reads
// the region flags at the feet; when they differ from the last flags and have
// the teleport bit (0x80000000), it calls PlayerZoneClient::DoTeleport
// @0x508c10 with the region's teleport text: an ATP region's
// "DRNTP00255%05d000000000000000" of atoi(name + 4) (zone point 10 x N), else
// the region's name. DoTeleport, Teleport_ResolveTeleportIndex and DoTeleportB
// are game/zoning.h (rof2decomp batch 263).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "assets/regions.h"

namespace hailstone::game {

// The text DoTeleport gets for a zone-line region named `tag`.
std::string region_teleport_text(std::string_view tag);

// ParseRegionNameFlags @0x487530 as a whole: the flags (assets::region_flags_from_tag; a null name changes
// nothing) and the teleport text it writes to CDisplay +0x15 when the name sets the teleport bit ("ATP": the
// DRNTP00255 reference text; otherwise names with "TP" at [3]: the name itself). The region bits 0x20000000,
// 0x08000000 and 0x04000000 ('M' at [0x1f], 'P' / 'F' at [0x21] of names longer than 32) have no reader but the
// whole-dword change test of CheckRegionChange (rof2decomp batch 265).
struct RegionNameFlags {
    std::uint32_t flags = 0;
    std::optional<std::string> teleport_name;
};
RegionNameFlags parse_region_name_flags(const char* name, std::uint32_t flags);

// Buried zone lines: a client addition, not the original's (docs/mechanics.md, "Buried zone lines"). Some EQG
// zone files newer than RoF2 (toxxulia, mistythicket, oldbloodfield and more) have zone-line boxes hundreds of units
// under the terrain, where no player can stand; the original reads the boxes the same way and cannot use them
// either. At zone load survey_zone_lines samples each zone-line box's footprint (its cross-section at the box
// centre's height) on a grid against the zone's collision; a box is buried when
//   - no sample has a floor inside the box or less than kBuriedFloorReach under its bottom (a cave or tunnel with
//     a real floor at the box's depth keeps the box as it is),
//   - ground lies over at least kBuriedCover of the samples, every sample's ground at least kBuriedMargin over the
//     box's top (a pit or a fall into the void, with open air over most of the box, keeps it as it is),
//   - over at least kBuriedCover of those the box's top is inside the ground: the surfaces over it have more tops
//     (normals facing up) than undersides, as a heightmap's single surface and closed objects give, not a roof.
// For a buried box ZoneLineTrigger tests the feet's x / y against the footprint and takes z as inside anywhere
// from the box's bottom up to kBuriedHeadroom over the highest ground in the footprint, so walking over it on the
// surface fires the zone line; every other box, and the region tests of anything but zone lines, are unchanged.
// A zone-in point can lie over a buried box's footprint (mistythicket's arrival from runnyeye is over its line back),
// where the original stands well above the box: a buried box the player zones in over does not fire until the feet
// have left its footprint.
struct BuriedZoneLine {
    std::uint32_t region = 0;     // 1-based box number
    std::string tag;
    float centre_z = 0.0f;        // the footprint is the box's cross-section at this height
    float box_lo_z = 0.0f, box_hi_z = 0.0f;   // the box's world z range
    float ground_lo = 0.0f, ground_hi = 0.0f; // the lowest / highest ground over the footprint
    float top_z = 0.0f;           // the feet count as inside up to here (ground_hi + kBuriedHeadroom)
    // The survey: footprint samples, those with ground over them, those inside the ground, and
    // the ones that rule the box out.
    int samples = 0, covered = 0, inside = 0, reachable = 0, shallow = 0;
    bool buried = false;
};
inline constexpr float kBuriedFloorReach = 50.0f;
inline constexpr float kBuriedMargin = 100.0f;
inline constexpr float kBuriedCover = 0.75f;
inline constexpr float kBuriedHeadroom = 50.0f;

class CollisionMesh;
// Every zone-line box of an EQG zone with its survey (none for a BSP zone).
std::vector<BuriedZoneLine> survey_zone_lines(const assets::ZoneRegions& regions, const CollisionMesh& collision);
// The buried ones.
std::vector<BuriedZoneLine> find_buried_zone_lines(const assets::ZoneRegions& regions, const CollisionMesh& collision);

// Tracks the region flags under the player's feet and fires once per entry
// into a zone-line region (CheckRegionChange fires on a change of flags).
class ZoneLineTrigger {
public:
    // The teleport text when the feet just entered a zone-line region.
    std::optional<std::string> update(const glm::vec3& feet, const assets::ZoneRegions& regions);
    // The current zone's buried boxes (find_buried_zone_lines), set at zone load; reset() keeps them.
    void set_buried(std::vector<BuriedZoneLine> buried) {
        buried_ = std::move(buried);
        held_.clear();
    }
    const std::vector<BuriedZoneLine>& buried() const { return buried_; }
    // Call on zone-in: PlayerZoneClient::ResetVariables sets the last region
    // flags (+0x94, +0x98, +0x9c) to 0xFFFFFFFF for every new spawn, the
    // rebuilt local player included, so the first check after zone-in always
    // counts as a change: a zone line at the arrival point fires (rof2decomp
    // batch 265).
    // It also holds back the buried boxes the player arrives over (BuriedZoneLine).
    void reset() {
        last_flags_ = kNoRegionFlags;
        arrived_ = true;
    }
    std::uint32_t last_flags() const { return last_flags_; }

    static constexpr std::uint32_t kNoRegionFlags = 0xFFFFFFFFu;

private:
    std::uint32_t last_flags_ = kNoRegionFlags;
    std::vector<BuriedZoneLine> buried_;
    std::vector<std::uint32_t> held_; // buried boxes zoned in over, until the feet leave them
    bool arrived_ = true;             // the next update is the first after a zone-in
};

// How far above the feet the region is sampled. The original samples at
// z + 0.25 - (a model offset - AvatarHeight); the offset is unresolved
// (docs/unknowns.md), so the feet plus 0.25 is used.
inline constexpr float kRegionProbeHeight = 0.25f;

} // namespace hailstone::game
