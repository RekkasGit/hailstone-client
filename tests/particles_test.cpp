#include <doctest/doctest.h>

#include <cmath>

#include <cstring>
#include <numbers>
#include <random>
#include <vector>

#include "assets/model_points.h"
#include "assets/pfs.h"
#include "assets/spell_effects.h"
#include "core/byte_io.h"
#include "core/game_dir.h"
#include "game/particles.h"
#include "render/particle_renderer.h"
#include "game_files.h"

using namespace nmsc;

namespace {

std::vector<std::byte> edd_record(const char* name, const char* texture, float duration, std::uint32_t initial) {
    ByteWriter w;
    w.fixed_string(name, 64);
    w.fixed_string(texture, 32);
    for (int i = 0; i < 6; ++i) w.le<std::uint32_t>(i == 2 ? 1u : 0u); // additive at 0x68
    w.f32(duration);  // 0x78
    w.f32(-1.0f);     // 0x7c lifetime
    w.le<std::uint32_t>(initial); // 0x80
    w.le<std::uint32_t>(3);       // 0x84 per tick
    w.f32(10.0f);                 // 0x88 ticks per second
    while (w.data().size() < 0xa8) w.f32(0.0f);
    w.le<std::uint32_t>(5); // 0xa8 shape
    while (w.data().size() < 0xd4) w.f32(0.0f);
    for (std::uint32_t c : {255u, 128u, 0u, 0u, 0u, 255u}) w.le<std::uint32_t>(c); // colours
    while (w.data().size() < 0x130) w.f32(0.0f);
    w.le<std::uint32_t>(16); // frames
    while (w.data().size() < 0x144) w.f32(0.0f);
    w.le<std::uint32_t>(0); // capacity
    while (w.data().size() < 0x1a0) w.le<std::uint8_t>(0);
    return w.take();
}

} // namespace

TEST_CASE("EDD 110: header, 0x1a0-byte records, fields at their offsets") {
    ByteWriter w;
    w.fixed_string("EDD", 4);
    w.fixed_string("110", 4);
    auto none = edd_record("None", "", 0.0f, 1);
    auto fire = edd_record("Fire", "flame.dds", -5.0f, 12);
    std::vector<std::byte> data = w.take();
    data.insert(data.end(), none.begin(), none.end());
    data.insert(data.end(), fire.begin(), fire.end());
    auto defs = assets::parse_edd(data);
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].name == "None");
    CHECK(defs[1].name == "Fire");
    CHECK(defs[1].texture == "flame.dds");
    CHECK(defs[1].additive == 1u);
    CHECK(defs[1].duration == -5.0f);
    CHECK(defs[1].initial_count == 12u);
    CHECK(defs[1].per_tick == 3u);
    CHECK(defs[1].ticks_per_second == 10.0f);
    CHECK(defs[1].shape == 5u);
    CHECK(defs[1].color_start == glm::uvec3(255, 128, 0));
    CHECK(defs[1].color_end == glm::uvec3(0, 0, 255));
    CHECK(defs[1].frames == 16u);
    // Other versions, the legacy headerless format and bad sizes are refused.
    auto bad = data;
    bad[6] = std::byte{'1'};
    CHECK_THROWS(assets::parse_edd(bad));
    CHECK_THROWS(assets::parse_edd(std::vector<std::byte>(0xc4 * 2)));
    data.pop_back();
    CHECK_THROWS(assets::parse_edd(data));
}

TEST_CASE("CreateSpellEmitter: duration, density and capacity rules") {
    // EDD +0x78 < 0 forces |value|; else a requested duration wins; else +0x78.
    CHECK(game::emitter_duration_seconds(-5.0f, 3000) == 5.0f);
    CHECK(game::emitter_duration_seconds(2.0f, 3000) == doctest::Approx(3.0f));
    CHECK(game::emitter_duration_seconds(2.0f, 0) == 2.0f);
    CHECK(game::emitter_duration_seconds(2.0f, -1) == 2.0f);
    assets::EmitterDefinition d;
    d.initial_count = 10;
    d.per_tick = 2;
    d.ticks_per_second = 5.0f;
    d.lifetime = 1.0f;
    CHECK(game::emitter_capacity(d, 4.0f) == 10u + 2u * 6u);
    d.capacity = 7; // +0x144 replaces the computed count
    CHECK(game::emitter_capacity(d, 4.0f) == 7u);
    d.capacity = 200000;
    CHECK(game::emitter_capacity(d, 4.0f) == 100000u);
    CHECK(game::flipbook_grid(1) == glm::uvec2(1, 1));
    CHECK(game::flipbook_grid(3) == glm::uvec2(2, 2));
    CHECK(game::flipbook_grid(6) == glm::uvec2(4, 2));
    CHECK(game::flipbook_grid(16) == glm::uvec2(4, 4));
    CHECK(game::flipbook_grid(17) == glm::uvec2(1, 1));

    std::vector<assets::EmitterDefinition> defs(2);
    defs[1].duration = 1.5f;
    defs[1].initial_count = 4;
    game::ParticleSystem ps(defs);
    game::SpellEmitterRequest r;
    r.emitter = 1;
    r.density = 0.0f; // density <= 0 creates nothing
    CHECK(ps.create(r) == 0);
    r.density = 0.5f;
    r.duration_ms = 0;
    CHECK(ps.create(r) == 1500);
    r.emitter = 2; // out of range
    CHECK(ps.create(r) == 0);
    REQUIRE(ps.emitters().size() == 1);
    ps.update(0.01f, {});
    CHECK(ps.emitters()[0].particles.size() == 2); // the initial count x density
    ps.update(2.0f, {});
    CHECK(ps.emitters().empty()); // duration and particles over
}

TEST_CASE("A box-surface particle draws its two free axes, then its face, as the graphics DLL does") {
    // EQGraphicsDX9.dll's shape 6 (@0x100737be): r1, r2 in -1..1, then the face fistp(u * 6.0) with u in 0..1;
    // faces 0..4 are -x, +x, -y, +y, -z, anything else (5, and 6 from the rounding) +z.
    for (std::uint32_t seed : {7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u}) {
        std::vector<assets::EmitterDefinition> defs(2);
        defs[1].duration = 1.0f;
        defs[1].initial_count = 1;
        defs[1].lifetime = 1.0f;
        defs[1].shape = 6;
        defs[1].shape_size = {2.0f, 3.0f, 4.0f};
        game::ParticleSystem ps(defs, seed);
        game::SpellEmitterRequest r;
        r.emitter = 1;
        r.density = 1.0f;
        REQUIRE(ps.create(r) > 0);
        ps.update(0.0f, {});
        REQUIRE(ps.emitters().size() == 1);
        REQUIRE(ps.emitters()[0].particles.size() == 1);
        // The same draws by hand: the azimuth (drawn for every shape), r1, r2, the face.
        std::mt19937 rng(seed);
        auto draw = [&](float lo, float hi) { return lo + (hi - lo) * std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
        draw(0.0f, 2.0f * std::numbers::pi_v<float>);
        const float r1 = draw(-1.0f, 1.0f);
        const float r2 = draw(-1.0f, 1.0f);
        const int face = static_cast<int>(std::nearbyint(draw(0.0f, 1.0f) * 6.0f));
        glm::vec3 v = face == 0   ? glm::vec3(-1.0f, r1, r2)
                      : face == 1 ? glm::vec3(1.0f, r1, r2)
                      : face == 2 ? glm::vec3(r1, -1.0f, r2)
                      : face == 3 ? glm::vec3(r1, 1.0f, r2)
                      : face == 4 ? glm::vec3(r1, r2, -1.0f)
                                  : glm::vec3(r1, r2, 1.0f);
        const glm::vec3 want = v * glm::vec3(2.0f, 3.0f, 4.0f), got = ps.emitters()[0].particles[0].local;
        INFO("seed " << seed << " face " << face);
        CHECK(got.x == doctest::Approx(want.x));
        CHECK(got.y == doctest::Approx(want.y));
        CHECK(got.z == doctest::Approx(want.z));
    }
}

TEST_CASE("SpellEffect_SpawnStageEmitters: slots, levels, locations and settings") {
    assets::SpellEffect fx;
    auto& st = fx.stages[0];
    st.slots[0] = {10, 0, 1, 0};  // at the caster
    st.slots[1] = {11, 20, 1, 0}; // needs level 20
    st.slots[2] = {12, 0, 3, 4};  // at the target, bone point 3
    st.slots[3] = {0, 0, 1, 0};   // empty
    game::StageActor caster{7, {1, 2, 3}, 10, 6.0f}, target{9, {4, 5, 6}, 1, 5.0f};
    target.points = 0x3ff; // all ten spell points
    auto s = game::spell_particle_settings(0.5f, 2.0f); // opacity clamped to 1
    CHECK(s.opacity == 1.0f);
    auto out = game::spawn_stage_emitters(fx, 0, 1234, &caster, &target, {0, 0, 0}, 3000, s);
    REQUIRE(out.size() == 2);
    CHECK(out[0].emitter == 10);
    CHECK(out[0].key == ((7u << 16) | 1234u));
    CHECK(out[0].duration_ms == 3000);
    CHECK(out[0].density == 0.5f);
    CHECK(out[0].position == glm::vec3(1, 2, 3));
    CHECK(out[0].actor == 0u);
    CHECK(out[1].emitter == 12);
    CHECK(out[1].position == glm::vec3(4, 5, 6));
    CHECK(out[1].actor == 9u);
    CHECK(out[1].bone_point == 3);
    // The flag forces density and opacity to 1.0.
    auto full = game::spawn_stage_emitters(fx, 0, 1234, &caster, &target, {0, 0, 0}, 0, s, true);
    CHECK(full[0].density == 1.0f);
}

TEST_CASE("SpellEffect_SpawnStageEmitters: the attach ids and their spell point fallbacks") {
    auto run = [](std::int32_t attach, std::uint16_t points) {
        assets::SpellEffect fx;
        fx.stages[0].slots[0] = {10, 0, 1, attach};
        game::StageActor caster{7, {1, 2, 3}, 10, 6.0f};
        caster.points = points;
        caster.chest = {8, 9, 10};
        return game::spawn_stage_emitters(fx, 0, 1, &caster, nullptr, {}, 0, {});
    };
    // A WLD human: default, chest, head, hands, feet and mouth; no eyes.
    const std::uint16_t human = 0x27f;
    CHECK(run(1, human)[0].bone_point == 0);
    CHECK(run(4, human)[0].bone_point == 3); // LHAND
    CHECK(run(5, human)[0].bone_point == 4); // RHAND
    CHECK(run(4, 0x1)[0].bone_point == 0);   // no hands: the default point
    CHECK(run(4, 0)[0].bone_point == -1);    // nothing: the actor alone
    CHECK(run(4, 0)[0].actor == 7u);
    CHECK(run(8, human)[0].bone_point == -1);
    CHECK(run(8, human)[0].actor == 7u);
    CHECK(run(9, human).empty()); // no eye: the slot is skipped
    CHECK(run(10, 0x3ff)[0].bone_point == 8);
    CHECK(run(11, human)[0].bone_point == 9);
    auto feet = run(12, human);
    REQUIRE(feet.size() == 1);
    CHECK(feet[0].bone_point == -1);
    CHECK(feet[0].position == glm::vec3(8, 9, 3)); // the chest's x and y
    CHECK(feet[0].scale == 6.0f);                  // the actor height
    CHECK(run(13, human).empty());
    CHECK(run(0, human)[0].actor == 0u);
}

TEST_CASE("Model points: the WLD table and the point matrix") {
    std::vector<std::string> bones = {"HUM_TRACK", "HUMCH_TRACK", "HUMHEAD_POINT_TRACK", "HUML_POINT_TRACK",
                                      "HUMR_POINT_TRACK", "HUMBO_L_TRACK", "HUMBO_R_TRACK"};
    auto pts = assets::wld_model_points(bones, [](std::string_view section, std::string_view key) -> std::optional<float> {
        if (section == "HUM" && key == "ZOffset") return -0.5f;
        return std::nullopt;
    });
    auto bone = [&](std::string_view n) {
        const auto* p = assets::find_point(pts, n);
        return p ? p->bone : std::string("-");
    };
    CHECK(bone("SPELLPOINT_DEFAULT") == "HUM_TRACK");
    CHECK(bone("SPELLPOINT_CHEST") == "HUMCH_TRACK");
    CHECK(bone("SPELLPOINT_HEAD") == "HUMHEAD_POINT_TRACK");
    CHECK(bone("SPELLPOINT_LHAND") == "HUML_POINT_TRACK");
    CHECK(bone("SPELLPOINT_RFOOT") == "HUMBO_R_TRACK");
    CHECK(bone("SPELLPOINT_LEYE") == "-");
    const auto* mouth = assets::find_point(pts, "SPELLPOINT_MOUTH");
    REQUIRE(mouth);
    CHECK(mouth->offset == glm::vec3(0.6f, 0.0f, -0.5f)); // XOffset's default, ZOffset from [HUM]
    // A Luclin skeleton: the mouth on HEHEAD at its fixed place.
    std::vector<std::string> luclin = {"HUM_TRACK", "HUMHEHEAD_TRACK", "HUMSPELL_POINT_TRACK"};
    auto lp = assets::wld_model_points(luclin, {});
    REQUIRE(assets::find_point(lp, "SPELLPOINT_MOUTH"));
    CHECK(assets::find_point(lp, "SPELLPOINT_MOUTH")->offset == glm::vec3(0.08f, 0.6f, 0.0f));
    CHECK(assets::find_point(lp, "SPELLPOINT_LHAND")->bone == "HUMSPELL_POINT_TRACK");
    CHECK(assets::find_point(lp, "SPELLPOINT_RHAND")->bone == "HUMSPELL_POINT_TRACK");
    // FUN_1006f370: heading 128 turns +X onto +Y; the offset is the translation; the scale applies first.
    assets::ModelPoint p;
    p.offset = {1, 2, 3};
    p.rotation = {128, 0, 0};
    p.scale = {2, 2, 2};
    glm::mat4 m = assets::point_matrix(p);
    glm::vec3 x = glm::vec3(m * glm::vec4(1, 0, 0, 1));
    CHECK(x.x == doctest::Approx(1.0f));
    CHECK(x.y == doctest::Approx(4.0f));
    CHECK(x.z == doctest::Approx(3.0f));
    // Pitch: the DLL negates it, so pitch 128 turns +X onto +Z.
    p = {};
    p.rotation = {0, 128, 0};
    glm::vec3 xp = glm::vec3(assets::point_matrix(p) * glm::vec4(1, 0, 0, 0));
    CHECK(xp.z == doctest::Approx(1.0f));
}

TEST_CASE("NMS model points: dkm.pts" * doctest::skip(!nmsc::test::game_dir())) {
    const auto& game = *nmsc::test::game_dir();
    auto archive = game.find("dkm.eqg");
    REQUIRE(archive);
    auto pfs = assets::PfsArchive::open(*archive);
    auto data = pfs.read("dkm.pts");
    REQUIRE(data);
    auto pts = assets::parse_pts(*data);
    REQUIRE(pts.size() == 11);
    CHECK(pts[0].name == "SPELLPOINT_DEFAULT");
    CHECK(pts[0].bone == "ROOT_BONE");
    CHECK(pts[3].name == "SPELLPOINT_LHAND");
    CHECK(pts[3].bone == "ARML_FARM");
    CHECK(pts[3].offset.x == doctest::Approx(1.3f));
    CHECK(pts[3].rotation == glm::vec3(128, 256, -128));
    CHECK(pts[10].name == "DAMAGE_POINT");
}

TEST_CASE("NMS spell particle files parse" * doctest::skip(!nmsc::test::game_dir())) {
    const auto& game = *nmsc::test::game_dir();
    auto edd = game.read("spellsnew.edd");
    REQUIRE(edd);
    auto defs = assets::parse_edd(*edd);
    CHECK(defs.size() == 2577); // record 0 is the file's own "None" sentinel
    CHECK(defs[0].name == "None");
    auto eff = game.read("spellsnew.eff");
    REQUIRE(eff);
    auto fx = assets::parse_spell_effects(*eff);
    CHECK(fx.size() > 600);
    std::size_t refs = 0, bad = 0;
    for (const auto& e : fx)
        for (const auto& st : e.stages)
            for (const auto& slot : st.slots)
                if (slot.emitter > 0) {
                    ++refs;
                    bad += static_cast<std::size_t>(slot.emitter) >= defs.size();
                }
    CHECK(refs > 1000);
    CHECK(bad == 0);
    for (const char* name : {"actoremittersnew.edd", "environmentemittersnew.edd"})
        if (auto d = game.read(name)) CHECK_FALSE(assets::parse_edd(*d).empty());
}

TEST_CASE("Actor emitters: PRT versions widen as the EQGM loader widens them") {
    auto record = [](std::vector<std::byte>& out, std::size_t size, std::uint32_t emitter, const char* point,
                     std::initializer_list<std::int32_t> words) {
        std::vector<std::byte> r(size);
        std::memcpy(r.data(), &emitter, 4);
        std::memcpy(r.data() + 4, point, std::strlen(point));
        std::size_t o = 68;
        for (std::int32_t w : words) {
            if (o + 4 > size) break;
            std::memcpy(r.data() + o, &w, 4);
            o += 4;
        }
        out.insert(out.end(), r.begin(), r.end());
    };
    auto file = [&](std::uint32_t version, std::size_t size) {
        std::vector<std::byte> d(12);
        std::memcpy(d.data(), "PTCL", 4);
        std::uint32_t n = 1;
        std::memcpy(d.data() + 4, &n, 4);
        std::memcpy(d.data() + 8, &version, 4);
        record(d, size, 1059, "IT12753_POINT1", {0, 0, 0, 0, 480, 5000, 1, 2, 0, 77});
        return d;
    };
    auto v5 = assets::parse_prt(file(5, 0x6c));
    REQUIRE(v5.size() == 1);
    CHECK(v5[0].emitter == 1059u);
    CHECK(v5[0].point == "IT12753_POINT1");
    CHECK(v5[0].delay_ms == 480);
    CHECK(v5[0].duration_ms == 5000);
    CHECK(v5[0].floor == 1);
    CHECK(v5[0].variant == 2);
    CHECK(v5[0].alternate == 77u);
    auto v1 = assets::parse_prt(file(1, 0x5c));
    REQUIRE(v1.size() == 1);
    CHECK(v1[0].floor == 0);
    CHECK(v1[0].variant == -1);
    CHECK(v1[0].alternate == 1059u);
    auto v3 = assets::parse_prt(file(3, 0x64));
    CHECK(v3[0].variant == 2);
    CHECK(v3[0].alternate == 1059u);
    CHECK(assets::parse_prt(file(6, 0x6c)).empty());
}

TEST_CASE("NMS actor emitters: a fire elemental and a glowing sword" * doctest::skip(!nmsc::test::game_dir())) {
    const auto& game = *nmsc::test::game_dir();
    auto archive = game.find("arxmentisweapons.eqg");
    REQUIRE(archive);
    auto pfs = assets::PfsArchive::open(*archive);
    auto prt = pfs.read("it12753.prt");
    auto pts = pfs.read("it12753.pts");
    REQUIRE(prt);
    REQUIRE(pts);
    auto e = assets::parse_prt(*prt);
    auto p = assets::parse_pts(*pts);
    REQUIRE(e.size() == 1);
    CHECK(e[0].emitter == 1059u);
    CHECK(e[0].trigger == 0);
    REQUIRE(assets::find_point(p, e[0].point));
    CHECK(assets::find_point(p, e[0].point)->bone == "ATTACH_TO_ORIGIN");
    auto edd = game.read("actoremittersnew.edd");
    REQUIRE(edd);
    // NMS ships an older ActorEmittersNew.edd (944 or 946 records with the sentinel, by install) than these items
    // expect: their emitter is out of range, and none is made. The record count is install data, so only the range
    // is checked.
    auto defs = assets::parse_edd(*edd);
    CHECK_FALSE(defs.empty());
    CHECK(e[0].emitter >= defs.size());
}

TEST_CASE("particle billboard modes: facing, standing on the axis, flat") {
    glm::mat3 axes(glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1));
    glm::vec3 right(0, 1, 0), up(0.3f, 0, 0.95f);
    render::particle_basis(0, axes, glm::vec3(10, 0, 0), right, up);
    CHECK(right == glm::vec3(0, 1, 0)); // mode 0 keeps the camera's
    render::particle_basis(1, axes, glm::vec3(10, 0, -2), right, up);
    CHECK(up == glm::vec3(0, 0, 1));
    CHECK(std::abs(glm::dot(right, glm::vec3(1, 0, 0))) < 1e-5f); // turned to face the eye along +x
    render::particle_basis(2, axes, glm::vec3(10, 0, -2), right, up);
    CHECK(right == glm::vec3(1, 0, 0));
    CHECK(up == glm::vec3(0, -1, 0)); // lying in the ground plane
}
