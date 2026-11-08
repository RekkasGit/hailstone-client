#pragma once

// Navigation meshes for automated movement (docs/navigation.md). The original client has no navigation of its own:
// this is a Hailstone addition, in the spirit of MacroQuest's MQ2Nav (its command set, not its code), built on
// Recast/Detour (zlib, THIRD_PARTY.md).
//
// The mesh is built from the zone's own collision triangles (game::CollisionMesh), so it needs no separate tool and no
// hand-made file: the triangles the movement sweep collides with are the ones rasterised here. Passable triangles
// (WLD 0x10, EQG flag 1) are left out, since the sweep never hits them, except a water surface (a flat passable
// triangle with water just under it and none just over it), which is swum on; everything else is an obstacle, and
// a triangle facing up within the slope limit is ground. A floor stops a fall only when its record normal faces up
// (CollisionTriangle: back faces are skipped, n . delta > 0), so a triangle whose normal faces down is never ground.
//
// Axes: Recast is Y-up; world coordinates (game/coords.h) are Z-up. to_recast / to_world turn one into the other
// with a rotation (x, y, z) -> (x, z, -y), which keeps the handedness.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace hailstone::game { class CollisionMesh; }

namespace hailstone::nav {

// Polygon areas (Detour area ids, < 64) and flags. The areas come from the zone's regions once a tile is added
// (NavMesh::mark_areas): the builder marks every polygon Ground.
enum class Area : std::uint8_t {
    Ground = 1,
    Water = 2,
    Lava = 3,     // lava and slime: never crossed unless the filter allows it
    ZoneLine = 4, // a zone line's volume: stepping in leaves the zone
    PvP = 5,
    Jump = 6,     // a link off a ledge (a drop, or into water): taken by walking off it
};
inline constexpr std::uint16_t kFlagWalk = 0x01;
inline constexpr std::uint16_t kFlagSwim = 0x02;
inline constexpr std::uint16_t kFlagHazard = 0x04;   // lava, slime
inline constexpr std::uint16_t kFlagZoneLine = 0x08;
inline constexpr std::uint16_t kFlagPvP = 0x10;
inline constexpr std::uint16_t kFlagJump = 0x20;     // a drop off a ledge
inline constexpr std::uint16_t kFlagDisabled = 0x8000;

inline glm::vec3 to_recast(const glm::vec3& w) { return {w.x, w.z, -w.y}; }
inline glm::vec3 to_world(const float* r) { return {r[0], -r[2], r[1]}; }

// What the mesh is built for. The agent's size and climb are not the original's: the movement sweep's radius is
// itself provisional (docs/unknowns.md), so these are conservative values that keep a path clear of walls the sweep
// would catch on. Units are world units.
struct BuildParams {
    float cell_size = 0.6f;      // the voxel's side
    float cell_height = 0.3f;    // the voxel's height
    float agent_height = 6.0f;   // the clearance a polygon needs above it
    float agent_radius = 2.0f;   // how far paths keep from walls (the human sweep radius is about 1.78)
    float agent_climb = 4.0f;    // the highest step taken without jumping (the sweep starts lifted over the feet)
    float max_slope = 55.0f;     // degrees; steeper ground is not walkable
    int tile_cells = 128;        // a tile's side in cells

    float tile_size() const { return static_cast<float>(tile_cells) * cell_size; }
    // Everything above, for the cache's key.
    std::uint32_t hash() const;
};

// A box of space (world coordinates) whose ground the builder marks with an area, so the polygons split along its
// edges: a zone line's volume is thin, and a polygon that only overlaps it would otherwise take its area from one
// point over its middle (NavMesh::mark_areas) and lead a path through it.
struct AreaBox {
    glm::vec3 lo{0.0f}, hi{0.0f};
    Area area = Area::ZoneLine;
};

// The zone's triangles in Recast axes, with each one's area (Ground or none) and, per tile, the triangles that
// touch it. Built once on the main thread; the tile builds read it from worker threads.
class InputGeometry {
public:
    // Whether a world point is in water (the zone's regions); null: no water anywhere.
    using WaterTest = std::function<bool(const glm::vec3&)>;
    // From the collision mesh: what the movement sweep collides with, and the water surfaces `water` finds.
    static std::shared_ptr<const InputGeometry> from_collision(const game::CollisionMesh& mesh, const BuildParams& p,
                                                               const WaterTest& water = {},
                                                               std::vector<AreaBox> boxes = {});
    // From world-space triangles (three vertices each) and whether each one blocks movement (tests and tools).
    // `solid` false makes the triangle passable (left out unless it is a water surface).
    static std::shared_ptr<const InputGeometry> from_world(std::span<const glm::vec3> vertices,
                                                           std::span<const std::uint8_t> solid, const BuildParams& p,
                                                           const WaterTest& water = {}, std::vector<AreaBox> boxes = {});

    const BuildParams& params() const { return params_; }
    // The box in Recast axes (y up); empty geometry has min > max.
    const glm::vec3& bmin() const { return bmin_; }
    const glm::vec3& bmax() const { return bmax_; }
    int tiles_x() const { return tiles_x_; }
    int tiles_y() const { return tiles_y_; }
    std::size_t triangle_count() const { return areas_.size(); }
    // A hash of every triangle and its area, for the cache's key.
    std::uint64_t hash() const { return hash_; }
    // The triangles touching a tile (with the build's border), as indices.
    std::span<const std::uint32_t> tile_triangles(int tx, int ty) const;
    const float* triangle(std::uint32_t i) const { return &verts_[static_cast<std::size_t>(i) * 9]; }
    std::uint8_t area(std::uint32_t i) const { return areas_[i]; }
    // Whether triangle i is a water surface (a passable triangle the mesh swims on).
    bool water_surface(std::uint32_t i) const { return surface_[i] != 0; }
    // Whether a point (Recast axes) is in water; false without a water test.
    bool in_water(const glm::vec3& r) const;
    // The nearest triangle straight down from (x, y_top, z) to y_bottom (Recast axes), or straight up from y_bottom
    // to y_top: its height and index.
    struct Hit {
        float y = 0.0f;
        std::uint32_t tri = 0;
    };
    std::optional<Hit> cast_down(float x, float z, float y_top, float y_bottom) const;
    std::optional<Hit> cast_up(float x, float z, float y_bottom, float y_top) const;
    // Whether a solid triangle (not a water surface) crosses the segment (Recast axes).
    bool blocked(const glm::vec3& a, const glm::vec3& b) const;
    const std::vector<AreaBox>& boxes() const { return boxes_; }

private:
    void finish();
    BuildParams params_;
    std::vector<float> verts_;          // nine floats a triangle, Recast axes
    std::vector<std::uint8_t> areas_;   // RC_WALKABLE_AREA or RC_NULL_AREA
    std::vector<std::uint8_t> surface_; // 1: a water surface
    WaterTest water_;
    std::span<const std::uint32_t> triangles_at(float x, float z) const;
    std::vector<AreaBox> boxes_;
    glm::vec3 bmin_{0.0f}, bmax_{-1.0f};
    int tiles_x_ = 0, tiles_y_ = 0;
    std::vector<std::vector<std::uint32_t>> tiles_;
    std::uint64_t hash_ = 0;
};

// One tile's Detour data (dtCreateNavMeshData's output), empty when the tile has no walkable ground.
std::vector<unsigned char> build_tile(const InputGeometry& geo, int tx, int ty);

// What a path search found.
struct PathResult {
    std::vector<glm::vec3> points; // world coordinates, the start first and the (reached) end last
    bool partial = false;          // the end could not be reached; the path stops at the nearest point
    bool found() const { return !points.empty(); }
};

// The filter a search uses: which areas it crosses and at what cost.
struct PathOptions {
    bool avoid_water = false;   // never swim (water costs 3x otherwise)
    bool allow_hazards = false; // cross lava and slime (never otherwise)
    bool avoid_pvp = false;     // never enter PvP areas
    bool allow_zone_lines = false; // cross a zone line's volume on the way (the end may still be in one)
    bool open_doors = true;     // the walk clicks a closed door it stalls at (app/navigator.h); not the search's
    // A Hailstone difference: while /nav or /stick moves the player, a door that is not shut (open or swinging) does
    // not block (app/navigator.h, Navigator::passes_open_doors); not the search's
    bool pass_open_doors = true;
    bool assist_follow = true;  // /follow takes the mesh's way round what blocks it (app/navigator.h); not the search's
    // How much further than the mesh's own margin (BuildParams::agent_radius) a path's bends keep from walls, where
    // there is room (NavMesh::find_path). 0: the bends hug the margin, as the search finds them.
    float wall_clearance = 2.5f;
};

// The tiled Detour mesh of one zone and its queries. Main thread only.
class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // An empty mesh laid over the geometry's tile grid. False when Detour refuses (too many tiles).
    bool init(const glm::vec3& rc_origin, float tile_size, int tiles_x, int tiles_y, const BuildParams& params);
    bool initialised() const { return mesh_ != nullptr; }
    // Adds a tile's data (build_tile's output, or the cache's). False when Detour refuses it.
    bool add_tile(std::span<const unsigned char> data);
    bool has_tile(int tx, int ty) const;
    std::size_t tile_count() const { return tiles_; }

    // Gives every polygon of the tile at (tx, ty) the area `classify` returns for a point over its middle (world
    // coordinates, `lift` above the polygon), and the flags that go with it. A polygon that is Ground there but has
    // water just under it is a water surface (InputGeometry), and Water. Polygons the builder marked from an
    // AreaBox keep their area.
    void mark_areas(int tx, int ty, const std::function<Area(const glm::vec3&)>& classify, float lift);

    // The nearest point on the mesh within `reach` horizontally and `reach_up` vertically, or nullopt.
    std::optional<glm::vec3> nearest(const glm::vec3& world, float reach = 4.0f, float reach_up = 12.0f) const;
    // The corner points from `from` to `to` over the mesh.
    PathResult find_path(const glm::vec3& from, const glm::vec3& to, const PathOptions& opt = {}) const;
    // A straight walk over the mesh from `from` towards `to`: true when nothing on the mesh is in the way.
    bool walkable_line(const glm::vec3& from, const glm::vec3& to, const PathOptions& opt = {}) const;
    // How far along the straight line from `from` to `to` the mesh goes before an edge (0..1, 1: all the way), or
    // nullopt when `from` is not on the mesh.
    std::optional<float> walkable_part(const glm::vec3& from, const glm::vec3& to, const PathOptions& opt = {}) const;

    // Every link (off a ledge, or between a deep floor and the water's surface): its ends in world coordinates and
    // whether it goes both ways. For the debug overlay.
    void for_each_link(const std::function<void(const glm::vec3& from, const glm::vec3& to, bool both)>& fn) const;
    // Every polygon's outline in world coordinates, for the debug overlay (a loop of points each).
    void for_each_polygon(const std::function<void(std::span<const glm::vec3> outline, Area area)>& fn) const;

private:
    void make_filter(const PathOptions& opt, dtQueryFilter& f) const;
    // Moves each of the path's bends (movable[i]) up to `clearance` further from the nearest wall, where the lines to
    // it and on from it stay on the mesh and it ends no nearer another wall.
    void keep_from_walls(std::vector<glm::vec3>& points, const std::vector<bool>& movable, float clearance,
                         const dtQueryFilter& f) const;
    dtNavMesh* mesh_ = nullptr;
    dtNavMeshQuery* query_ = nullptr;
    BuildParams params_;
    std::size_t tiles_ = 0;
};

// The cache file (docs/navigation.md "Cache"): a header (magic, version, the native layout of Detour's data, the
// geometry and parameter hashes, the grid) then each tile's data. Detour's tile data is its own in-memory layout,
// so a file is read back only by a build with the same layout (endianness, polygon reference size, Detour's
// version); any mismatch reads as no cache.
struct CachedTile {
    int tx = 0, ty = 0;
    std::vector<unsigned char> data;
};
struct CacheHeader {
    std::uint64_t geometry_hash = 0;
    std::uint32_t params_hash = 0;
    std::uint64_t builder = 0; // builder_fingerprint(): a file built by other rules is not read back
    glm::vec3 origin{0.0f};
    float tile_size = 0.0f;
    int tiles_x = 0, tiles_y = 0;
};
// A small scene with one of each thing the builder makes: ground and a step a little over 3, a drop off a ledge, a
// pool whose floor is joined to its drawn surface, a flooded room that is dropped into and climbed out of, and a
// zone line's box. Its tiles' bytes are the builder's fingerprint.
std::shared_ptr<const InputGeometry> fingerprint_scene();
// A hash of what this build of the client makes of fingerprint_scene(), worked out once (a few milliseconds).
// Any change to what the builder makes there (its rules, its links, Recast itself, the compiler's arithmetic)
// changes it, so caches written by another build are built again without anyone raising a version by hand.
std::uint64_t builder_fingerprint();

std::vector<std::byte> encode_cache(const CacheHeader& h, std::span<const CachedTile> tiles);
// The tiles when the file's header matches `expect` (hashes and grid), else nullopt. Never throws.
std::optional<std::vector<CachedTile>> decode_cache(std::span<const std::byte> bytes, const CacheHeader& expect);

} // namespace hailstone::nav
