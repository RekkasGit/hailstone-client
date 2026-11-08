// The zone's own ladders (game/zone_ladders.h): LADDER* placements are actors of type 6, so walking into one climbs
// it (CollidingType 6 from the sweep's hit callback) and a click mounts it.

#include <doctest/doctest.h>

#include <optional>
#include <vector>

#include "app/player_movement.h"
#include "assets/zone.h"
#include "core/game_dir.h"
#include "game/collision.h"
#include "game/mechanics/collision_movement.h"
#include "game/zone_ladders.h"
#include "game_files.h"
#include "net/game_connection.h"
#include "plugin/policy.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

// An axis-aligned box as a mesh (12 triangles).
assets::Mesh box_mesh(glm::vec3 lo, glm::vec3 hi) {
    assets::Mesh m;
    for (int i = 0; i < 8; ++i) {
        assets::Vertex v;
        v.position = {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
        m.vertices.push_back(v);
    }
    const std::uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto& f : faces) m.indices.insert(m.indices.end(), {f[0], f[1], f[2], f[0], f[2], f[3]});
    m.materials.push_back(assets::Material{});
    m.parts.push_back({0, static_cast<std::uint32_t>(m.indices.size()), 0, true});
    return m;
}

// The triangle with its record normal (game::CollisionTriangle::make) facing `out`.
game::CollisionTriangle facing(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3& out, std::uint8_t flags) {
    if (glm::dot(glm::cross(a - b, c - b), out) < 0.0f) std::swap(b, c);
    return game::CollisionTriangle::make(a, b, c, flags);
}

void quad(std::vector<game::CollisionTriangle>& t, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const glm::vec3& out, std::uint8_t flags = 0) {
    t.push_back(facing(a, b, c, out, flags));
    t.push_back(facing(a, c, d, out, flags));
}

enum class Wall { Plain, Ladder, PassableLadder };

// Ground at z 0; a wall 60 high at x 10 facing -x, itself a ladder (Ladder), or plain with a passable ladder 0.7 in
// front of it (PassableLadder, as Cabilis's LADDERCB101 against a canal wall).
game::CollisionMesh wall_scene(Wall wall) {
    std::vector<game::CollisionTriangle> t;
    quad(t, {-30, -300, 0}, {300, -300, 0}, {300, 300, 0}, {-30, 300, 0}, {0, 0, 1});
    const std::uint8_t flags = wall == Wall::Ladder ? game::kTriangleObject | game::kTriangleLadder : game::kTriangleObject;
    quad(t, {10, -5, 0}, {10, 5, 0}, {10, 5, 60}, {10, -5, 60}, {-1, 0, 0}, flags);
    if (wall == Wall::PassableLadder)
        quad(t, {9.3f, -2, 0}, {9.3f, 2, 0}, {9.3f, 2, 60}, {9.3f, -2, 60}, {-1, 0, 0},
             game::kTriangleObject | game::kTriangleLadder | game::kTrianglePassable);
    return game::CollisionMesh::from_triangles(std::move(t));
}

// The zone mesh as ZoneMoveWorld uses it (app/client_app_internal.h): the sweep's final triangle tells a ladder hit, and
// with `passable_ladders` (hailstone.cfg [movement]) so does a passable ladder met short of the stop.
class MeshWorld final : public app::MoveWorld {
public:
    explicit MeshWorld(const game::CollisionMesh& mesh, bool passable_ladders = false)
        : mesh_(mesh), passable_ladders_(passable_ladders) {}
    bool collision_loaded() const override { return true; }
    std::optional<std::pair<glm::vec3, glm::vec3>> world_box() const override { return mesh_.bounds(); }
    Sweep sweep(const glm::vec3& from, const glm::vec3& to, float radius) const override {
        return run(from, to, radius, false);
    }
    Sweep world_sweep(const glm::vec3& from, const glm::vec3& to, float radius) const override {
        return run(from, to, radius, true);
    }
    bool in_world(const glm::vec3&) const override { return true; }
    std::optional<float> floor_below(const glm::vec3& p, float step_up) const override {
        return mesh_.floor_below(p, step_up);
    }
    float floor_sweep(const glm::vec3& from, const glm::vec3& to, float radius, std::uint32_t* = nullptr,
                      std::vector<FloorSwitchHit>* = nullptr) const override {
        return run(from, to, radius, true).out.position.z;
    }
    std::optional<float> ceiling_above(const glm::vec3& p) const override { return mesh_.ceiling_above(p); }

private:
    Sweep run(const glm::vec3& from, const glm::vec3& to, float radius, bool floor_only) const {
        game::SphereSweep s = game::SphereSweep::make(from, to, radius);
        s.floor_only = floor_only;
        Sweep out;
        if (s.valid()) {
            const std::int32_t tri = mesh_.sweep_sphere_hit(s);
            out.ladder_hit =
                tri >= 0 && (mesh_.triangles()[static_cast<std::size_t>(tri)].flags & game::kTriangleLadder);
            if (passable_ladders_ && !floor_only && !out.ladder_hit) out.ladder_hit = mesh_.touches_passable_ladder(s);
        }
        out.out = game::sweep_output(s);
        out.collided = s.collided;
        return out;
    }
    const game::CollisionMesh& mesh_;
    bool passable_ladders_;
};

struct Walk {
    float top_z = 0.0f;
    std::int32_t colliding_type = 0;
};

// Walks at the wall (+x) for 2.4 s looking up 45 degrees (short of the top, which a climb reaches in about 3 s).
Walk walk_at_wall(Wall wall, bool passable_ladders = false) {
    test::ScratchDir scratch("zone_ladders");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    const game::CollisionMesh mesh = wall_scene(wall);
    const MeshWorld world(mesh, passable_ladders);
    constexpr float kAvatarHeight = 3.75f;
    app::PlayerMovement m;
    m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 1}));
    m.set_race(1);
    m.reset(glm::vec3(0.0f, 0.0f, 0.0f), 0.0f);
    app::MovementKeys keys;
    std::uint32_t now = 10000;
    Walk w;
    const float pitch = glm::radians(45.0f);
    for (int i = 0; i < 150; ++i) {
        keys.forward = i >= 3;
        now += 16;
        app::PlayerMovement::Frame f = m.update(keys, 0.0f, pitch, 16.0f, now, conn, nullptr, kAvatarHeight, &world);
        w.top_z = std::max(w.top_z, f.feet.z);
        w.colliding_type = m.controller().body.colliding_type;
    }
    return w;
}

} // namespace

TEST_CASE("zone ladders: the actor tag starts with LADDER, in any case") {
    CHECK(game::is_ladder_actor("LADDER14_ACTORDEF"));
    CHECK(game::is_ladder_actor("ladderCB101_ACTORDEF"));
    CHECK(game::is_ladder_actor("LADDER"));
    CHECK_FALSE(game::is_ladder_actor("LADDE"));
    CHECK_FALSE(game::is_ladder_actor("XLADDER_ACTORDEF"));
    CHECK_FALSE(game::is_ladder_actor("IT63_ACTORDEF"));
}

TEST_CASE("zone ladders: a LADDER placement is a ladder actor and its triangles are marked") {
    assets::Zone zone;
    zone.objects["LADDER14_ACTORDEF"] = box_mesh({0.0f, -1.7f, -1.0f}, {0.7f, 1.7f, 14.6f});
    zone.objects["BARREL_ACTORDEF"] = box_mesh({-1, -1, 0}, {1, 1, 3});
    const glm::vec3 at(100.0f, 200.0f, 5.0f);
    constexpr float kUnit = 2.0f * glm::pi<float>() / 512.0f;
    assets::Placement ladder;
    ladder.actor = "LADDER14_ACTORDEF";
    // As wld::placement_transform: heading 128 about Z, a small lean about Y, scale 2.
    ladder.transform = glm::translate(glm::mat4(1.0f), at);
    ladder.transform = glm::rotate(ladder.transform, 128.0f * kUnit, glm::vec3(0, 0, 1));
    ladder.transform = glm::rotate(ladder.transform, -8.0f * kUnit, glm::vec3(0, 1, 0));
    ladder.transform = glm::scale(ladder.transform, glm::vec3(2.0f));
    assets::Placement barrel;
    barrel.actor = "BARREL_ACTORDEF";
    barrel.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0));
    zone.placements = {barrel, ladder};

    auto ladders = game::zone_ladders(zone);
    REQUIRE(ladders.size() == 1);
    const auto& l = ladders[0].ladder;
    CHECK(l.type == 6);
    CHECK(l.x == doctest::Approx(at.x));
    CHECK(l.y == doctest::Approx(at.y));
    CHECK(l.z == doctest::Approx(at.z));
    CHECK(l.heading == doctest::Approx(128.0f).epsilon(1e-4));
    CHECK(l.height == doctest::Approx(14.6f * 2.0f));
    CHECK(ladders[0].bound > 0.0f);

    const game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    std::size_t marked = 0, objects = 0;
    for (const auto& t : mesh.triangles()) {
        if (t.flags & game::kTriangleObject) ++objects;
        if (t.flags & game::kTriangleLadder) ++marked;
    }
    CHECK(objects == 24);
    CHECK(marked == 12); // the ladder's, not the barrel's
}

TEST_CASE("zone ladders: walking into a ladder climbs it; into a wall does not") {
    const Walk wall = walk_at_wall(Wall::Plain);
    CHECK(wall.colliding_type == 0);
    CHECK(wall.top_z < 1.0f);
    const Walk ladder = walk_at_wall(Wall::Ladder);
    CHECK(ladder.colliding_type == 6);
    CHECK(ladder.top_z > 30.0f);
}

TEST_CASE("zone ladders: a ladder whose polygons are all passable is kept, clickable, and never blocks") {
    // Cabilis's LADDERCB101: every polygon has the WLD passable flag (a part that is not solid).
    assets::Zone zone;
    assets::Mesh mesh = box_mesh({0.0f, -1.0f, -12.0f}, {0.7f, 1.0f, 12.0f});
    mesh.parts[0].solid = false;
    zone.objects["LADDERCB101_ACTORDEF"] = mesh;
    assets::Placement p;
    p.actor = "LADDERCB101_ACTORDEF";
    p.transform = glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 60.0f, -13.0f));
    zone.placements = {p};

    const auto ladders = game::zone_ladders(zone);
    REQUIRE(ladders.size() == 1);
    CHECK(ladders[0].ladder.height == doctest::Approx(12.0f)); // the top of all its triangles
    REQUIRE(ladders[0].shape.records.size() == 12);
    for (const auto& r : ladders[0].shape.records) CHECK((r.flags & game::kTrianglePassable) != 0); // the pick's
    // passableBlocking pass hits them

    const game::CollisionMesh collision = game::CollisionMesh::build(zone);
    std::size_t passable_ladder = 0;
    for (const auto& t : collision.triangles())
        if ((t.flags & game::kTriangleLadder) && (t.flags & game::kTrianglePassable)) ++passable_ladder;
    CHECK(passable_ladder == 12);
}

TEST_CASE("zone ladders: walking into a passable ladder climbs it only with [movement] passable_ladders") {
    // As in the original (nothing gives its triangles the DLL's bit 2): the player walks through it to the wall.
    const Walk faithful = walk_at_wall(Wall::PassableLadder, false);
    CHECK(faithful.colliding_type == 0);
    CHECK(faithful.top_z < 1.0f);
    // Hailstone's switch: touching it short of the stop, or overlapping it where the move starts, climbs.
    const Walk climbed = walk_at_wall(Wall::PassableLadder, true);
    CHECK(climbed.colliding_type == 6);
    CHECK(climbed.top_z > 30.0f);
    // A plain wall or a solid ladder is the same either way.
    CHECK(walk_at_wall(Wall::Plain, true).top_z < 1.0f);
    CHECK(walk_at_wall(Wall::Ladder, true).top_z > 30.0f);
}

TEST_CASE("zone ladders: hailstone.cfg [movement] passable_ladders") {
    CHECK(plugin::Rules{}.passable_ladders);              // no file: on
    CHECK(plugin::parse_server_cfg("").passable_ladders); // a file that does not say: on
    std::vector<std::string> warnings;
    CHECK_FALSE(plugin::parse_server_cfg("[movement]\npassable_ladders = off\n", &warnings).passable_ladders);
    CHECK(warnings.empty());
    CHECK(plugin::parse_server_cfg("[movement]\npassable_ladders = on\n").passable_ladders);
}

TEST_CASE("zone ladders: Upper Guk's ladders" * doctest::skip(!hailstone::test::game_dir())) {
    const assets::Zone zone = assets::load_zone(*hailstone::test::game_dir(), "guktop");
    const auto ladders = game::zone_ladders(zone);
    REQUIRE(!ladders.empty());
    for (const auto& l : ladders) {
        CAPTURE(l.ladder.x);
        CAPTURE(l.ladder.y);
        // LADDER14 / 28 / 42 at their placements' scale: about as tall as the name says, stood upright.
        CHECK(l.ladder.height > 10.0f);
        CHECK(l.ladder.height < 100.0f);
        CHECK(l.ladder.heading >= 0.0f);
        CHECK(l.ladder.heading < 512.0f);
    }
    const game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    std::size_t marked = 0;
    for (const auto& t : mesh.triangles())
        if (t.flags & game::kTriangleLadder) ++marked;
    CHECK(marked > 0);
    MESSAGE("guktop: ", ladders.size(), " ladders, ", marked, " ladder triangles");
}

TEST_CASE("zone ladders: Cabilis East's canal ladders" * doctest::skip(!hailstone::test::game_dir())) {
    const assets::Zone zone = assets::load_zone(*hailstone::test::game_dir(), "cabeast");
    const auto ladders = game::zone_ladders(zone);
    // 2 LADDER14 (solid bamboo) and 13 LADDERCB101 (every polygon passable): all of them clickable.
    std::size_t placed = 0;
    for (const auto& p : zone.placements)
        if (game::is_ladder_actor(p.actor)) ++placed;
    CHECK(placed == 15);
    CHECK(ladders.size() == placed);

    // The canal ladder at (850, -98), heading 256: swimming at it from the canal, looking up.
    const game::ZoneLadder* canal = nullptr;
    for (const auto& l : ladders)
        if (glm::distance(glm::vec2(l.center), glm::vec2(850.0f, -98.1f)) < 1.0f) canal = &l;
    REQUIRE(canal != nullptr);
    const game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    test::ScratchDir scratch("zone_ladders_cabeast");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    auto swim = [&](bool passable_ladders) {
        const MeshWorld world(mesh, passable_ladders);
        constexpr float kAvatarHeight = 3.75f;
        app::PlayerMovement m;
        m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 1}));
        m.set_race(1);
        const float yaw = glm::half_pi<float>(); // +y, at the ladder
        m.reset(canal->center + glm::vec3(0.0f, -12.0f, 3.0f), yaw);
        app::MovementKeys keys;
        std::uint32_t now = 10000;
        float top = -1e9f;
        for (int i = 0; i < 400; ++i) {
            keys.forward = i >= 3;
            now += 16;
            auto f = m.update(keys, yaw, glm::radians(45.0f), 16.0f, now, conn, &zone.regions, kAvatarHeight, &world);
            top = std::max(top, f.feet.z);
        }
        return top;
    };
    CHECK(swim(false) < -9.0f); // the original: no way out of the canal here
    CHECK(swim(true) > -1.0f);  // up to the street (z 0)
}
