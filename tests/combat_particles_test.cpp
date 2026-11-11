#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "app/actor_emitters.h"
#include "app/spell_particles.h"
#include "assets/character.h"
#include "core/byte_io.h"
#include "game/missiles.h"
#include "game/spells.h"
#include "mock_server/harness.h"
#include "net/spell_packets.h"
#include "net/zone_packets.h"

using namespace hailstone;

namespace {

assets::EmitterDefinition dot() {
    assets::EmitterDefinition d;
    d.duration = 1.0f;
    d.lifetime = 2.0f;
    d.initial_count = 1;
    d.per_tick = 0;
    d.max_opacity = 1.0f;
    return d;
}

bool near(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-4f; }

std::string spell_line(std::uint32_t id, unsigned target_type = 0, unsigned ae_duration = 0, bool npc_no_los = false,
                       int first_effect = 254, bool lasting_buff = false) {
    std::vector<std::string> columns(237, "0");
    columns[0] = std::to_string(id);
    columns[1] = "Fixture spell";
    columns[145] = "1";
    columns[98] = std::to_string(target_type);
    columns[18] = std::to_string(ae_duration);
    columns[159] = npc_no_los ? "1" : "0";
    for (std::size_t i = 86; i <= 97; ++i) columns[i] = "254";
    columns[86] = std::to_string(first_effect);
    if (lasting_buff) {
        columns[16] = "7";
        columns[17] = "10";
        columns[83] = "1";
        columns[20] = "10";
        columns[70] = "100";
        for (std::size_t i = 104; i < 120; ++i) columns[i] = "255";
        columns[105] = "1";
    }
    std::string line;
    for (const auto& c : columns) line += (line.empty() ? "" : "^") + c;
    return line;
}

} // namespace

TEST_CASE("Spell landing packets use the source actor and its level for impact-stage emitters") {
    using namespace net;
    using namespace test;
    Rig rig;
    REQUIRE(rig.to_zone());
    auto caster = make_spawn("FixtureCaster", 333, 1, 1, 50, 60, 7);
    caster.level = 50;
    auto struck = make_spawn("FixtureTarget", 334, 1, 1, 80, 90, 8);
    struck.level = 1;
    rig.server.add_spawn(caster);
    rig.server.add_spawn(struck);
    REQUIRE(rig.until([&] { return rig.conn.world().spawns.contains(333) && rig.conn.world().spawns.contains(334); }));
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line(600)) == 1);
    rig.conn.set_spell_table_for_test(table);
    rig.conn.take_spell_effect_cues();
    Action hit;
    hit.source = 333;
    hit.target = 334;
    hit.type = 231;
    hit.spell_id = 600;
    hit.spell_level = 50;
    hit.effect_flag = 4;
    rig.conn.receive_zone_packet_for_test(encode(hit));
    auto cues = rig.conn.take_spell_effect_cues();
    REQUIRE(std::any_of(cues.begin(), cues.end(), [](const auto& c) { return c.stage == 2; }));
    for (auto& cue : cues)
        if (cue.stage == 2) cue.delay = 0.05;
    assets::SpellEffect effect;
    effect.stages[2].slots[0] = {1, 20, 1, 8}; // caster-level gate and caster attachment
    effect.stages[2].slots[1] = {1, 0, 3, 8};  // separately attach to the struck actor
    app::SpellParticles particles({assets::EmitterDefinition{}, dot()}, {assets::SpellEffect{}, effect});
    particles.update(rig.conn, cues, {}, nullptr, 0.01f);
    CHECK(particles.simulation().emitters().empty());
    particles.update(rig.conn, {}, {}, nullptr, 0.04f);
    CHECK(particles.simulation().emitters().empty());
    particles.update(rig.conn, {}, {}, nullptr, 0.02f);
    const auto& emitters = particles.simulation().emitters();
    REQUIRE(emitters.size() == 2);
    CHECK(emitters[0].actor == 333);
    CHECK(emitters[1].actor == 334);
    CHECK(emitters[0].key == ((333u << 16) | 600u));
    CHECK(near(emitters[0].origin, rig.conn.world().spawns.at(333).position));
    CHECK(near(emitters[1].origin, rig.conn.world().spawns.at(334).position));
    REQUIRE(emitters[0].particles.size() == 1);
    particles.clear();
    CHECK(particles.simulation().emitters().empty());
}

TEST_CASE("Attached spell particles preserve raw actor axes and update their owner opacity") {
    auto d = dot();
    d.follow_attachment = 1;
    d.offset = {0, 1, 0}; // transverse 1
    game::ParticleSystem particles({assets::EmitterDefinition{}, d});
    game::SpellEmitterRequest request;
    request.emitter = 1;
    request.actor = 7;
    request.position = {10, 20, 30};
    request.opacity = 0.5f;
    REQUIRE(particles.create(request) > 0);
    game::EmitterAttachment attachment;
    attachment.actor_axes = glm::mat3(glm::vec3(2, 0, 0), glm::vec3(0, 3, 0), glm::vec3(0, 0, 4));
    attachment.alpha = 0.25f;
    auto resolve = [&](std::uint32_t, std::int32_t) { return std::optional(attachment); };
    particles.update(0.01f, resolve);
    const auto& first = particles.emitters().front().particles.front();
    CHECK(near(first.position, {12, 20, 30}));
    CHECK(first.color.a == doctest::Approx(0.125f));
    (*attachment.actor_axes)[0] = {5, 0, 0};
    attachment.alpha = 0.75f;
    particles.update(0.01f, resolve);
    const auto& second = particles.emitters().front().particles.front();
    CHECK(near(second.position, {15, 20, 30}));
    CHECK(second.color.a == doctest::Approx(0.375f));
}

TEST_CASE("Actor emitters use point-owner PESFactor and freeze floor placements as the actor moves") {
    auto d = dot();
    d.duration = 0;
    d.follow_attachment = 1;
    d.bone_orientation = 1;
    d.actor_scales_size = 1;
    d.actor_scales_offsets = 0;
    assets::CharacterMesh mesh;
    assets::MeshEmitter attached;
    attached.bone = 0;
    attached.emitter.emitter = 1;
    attached.model_code = "IT42";
    mesh.emitters.push_back(attached);
    auto floor = attached;
    floor.emitter.floor = 1;
    floor.local = glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0));
    mesh.emitters.push_back(floor);
    auto heights = game::parse_height_tables({}, "[IT42]\nPESFactor=3\n");
    app::ActorEmitters particles({assets::EmitterDefinition{}, d}, heights);
    bool floor_available = false;
    particles.set_floor([&](const glm::vec3&) -> std::optional<float> {
        return floor_available ? std::optional(10.0f) : std::nullopt;
    });
    glm::mat4 world = glm::translate(glm::mat4(1), glm::vec3(100, 20, 30));
    world = glm::scale(world, glm::vec3(2, 4, 6));
    app::ActorEmitters::Actor actor{7, &mesh, false, 0.25f, glm::mat3(world)};
    auto pose = [&](std::uint32_t id, int bone, glm::mat4& out) {
        if (id != 7 || bone != 0) return false;
        out = world;
        return true;
    };
    particles.update(0.01f, std::span(&actor, 1), pose, {});
    REQUIRE(particles.simulation().emitters().size() == 1); // no guessed floor while collision is unavailable
    const auto& attached_particle = particles.simulation().emitters()[0].particles.front();
    CHECK(attached_particle.size_factor == doctest::Approx(6.0f)); // first row length 2 x item PESFactor 3
    CHECK(attached_particle.color.a == doctest::Approx(0.25f));
    CHECK(near(particles.simulation().emitters()[0].axes[2], {0, 0, 3})); // one common divisor, not three unit axes
    floor_available = true;
    particles.update(0.01f, std::span(&actor, 1), pose, {});
    REQUIRE(particles.simulation().emitters().size() == 2);
    CHECK(near(particles.simulation().emitters()[1].origin, {104, 20, 10.05f}));
    CHECK(particles.simulation().emitters()[1].actor == 0);
    world[3].x += 20;
    actor.alpha = 0.5f;
    particles.update(0.01f, std::span(&actor, 1), pose, {});
    CHECK(near(particles.simulation().emitters()[0].origin, {120, 20, 30}));
    CHECK(near(particles.simulation().emitters()[1].origin, {104, 20, 10.05f}));
    CHECK(particles.simulation().emitters()[0].particles.front().color.a == doctest::Approx(0.5f));
    particles.update(0.01f, {}, pose, {});
    CHECK(particles.simulation().emitters().empty());
}

TEST_CASE("Local sharp-weapon damage produces asset-backed blood only at a supported actor point") {
    using namespace net;
    using namespace test;
    Rig rig;
    REQUIRE(rig.to_zone());
    rig.server.add_spawn(make_spawn("FixtureTarget", 334, 1, 1, 80, 90, 8));
    REQUIRE(rig.until([&] { return rig.conn.world().spawns.contains(334); }));
    app::SpellParticles particles({}, {});
    glm::mat4 point = glm::translate(glm::mat4(1), glm::vec3(10, 20, 30));
    bool supported = true;
    particles.set_blood_point([&](std::uint32_t id) -> std::optional<glm::mat4> {
        return supported && id == 334 ? std::optional(point) : std::nullopt;
    });
    CombatDamage hit;
    hit.source = static_cast<std::uint16_t>(rig.conn.world().player_id);
    hit.target = 334;
    hit.type = 1;
    hit.damage = 10;
    // Even with a valid point, an installation without the source sprite shows no fabricated fallback.
    rig.conn.receive_zone_packet_for_test(encode(hit));
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    CHECK(particles.simulation().emitters().empty());
    particles.set_blood_sprite(game::blood_spurt_definition("fixture.dds"));
    rig.conn.receive_zone_packet_for_test(encode(hit));
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    REQUIRE(particles.simulation().emitters().size() == 1);
    const auto& blood = particles.simulation().emitters().front();
    CHECK(blood.actor == 334);
    CHECK(blood.point_only);
    CHECK(near(blood.origin, {10, 20, 30}));
    CHECK(blood.duration == doctest::Approx(0.5f));
    CHECK(blood.counts.capacity == 100);
    REQUIRE(blood.particles.size() == 1);
    CHECK(blood.particles.front().color.a == doctest::Approx(0.5f));
    CHECK(blood.particles.front().size_factor == doctest::Approx(0.375f));
    point[3].x += 20;
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    CHECK(near(particles.simulation().emitters().front().origin, {30, 20, 30}));
    particles.clear();
    // Skill, ownership, successful hit and a living target are all part of the packet-to-cue contract.
    for (auto skill : {4u, 7u, 21u, 30u, 51u, 231u}) {
        hit.type = static_cast<std::uint8_t>(skill);
        rig.conn.receive_zone_packet_for_test(encode(hit));
    }
    hit.type = 1;
    hit.source = 333;
    rig.conn.receive_zone_packet_for_test(encode(hit));
    hit.source = static_cast<std::uint16_t>(rig.conn.world().player_id);
    hit.damage = 0;
    rig.conn.receive_zone_packet_for_test(encode(hit));
    CHECK(rig.conn.take_blood_spurts().empty());
    hit.damage = 10;
    for (auto skill : {0u, 1u, 2u, 3u, 8u, 10u, 36u, 77u}) {
        hit.type = static_cast<std::uint8_t>(skill);
        rig.conn.receive_zone_packet_for_test(encode(hit));
    }
    CHECK(rig.conn.take_blood_spurts().size() == 8);
    rig.conn.world_for_test().spawns.at(334).is_corpse = true;
    rig.conn.receive_zone_packet_for_test(encode(hit));
    CHECK(rig.conn.take_blood_spurts().empty());
    rig.conn.world_for_test().spawns.at(334).is_corpse = false;
    supported = false;
    rig.conn.receive_zone_packet_for_test(encode(hit));
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    CHECK(particles.simulation().emitters().empty());
    supported = true;
    rig.conn.receive_zone_packet_for_test(encode(hit));
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    REQUIRE(particles.simulation().emitters().size() == 1);
    rig.conn.world_for_test().spawns.erase(334);
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    CHECK(particles.simulation().emitters().empty());
    particles.clear();
    rig.conn.disconnect();
    CHECK(rig.conn.take_blood_spurts().empty());
}

TEST_CASE("A valid spell missile packet creates its stage at the missile and releases missile-owned emitters") {
    using namespace net;
    using namespace test;
    Rig rig;
    REQUIRE(rig.to_zone());
    rig.server.add_spawn(make_spawn("FixtureCaster", 333, 1, 1, 50, 60, 7));
    rig.server.add_spawn(make_spawn("FixtureTarget", 334, 1, 1, 80, 90, 8));
    REQUIRE(rig.until([&] { return rig.conn.world().spawns.contains(333) && rig.conn.world().spawns.contains(334); }));
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line(600, 8, 1000, true)) == 1);
    REQUIRE(table->get(600));
    CHECK(table->get(600)->npc_no_los);
    REQUIRE(table->get(600)->spell_anim == 1);
    REQUIRE(table->get(600)->target_type == 8);
    rig.conn.set_spell_table_for_test(table);
    assets::SpellEffect effect;
    effect.stages[2].slots[0] = {1, 0, 2, 8}; // the actual missile actor
    effect.stages[2].slots[1] = {1, 0, 1, 8}; // source is still separately addressable
    effect.stages[2].slots[2] = {1, 0, 0, 0}; // the explicit world point
    effect.stages[2].slots[3] = {1, 0, 3, 8}; // native missile launch passes no target actor to DoSpellEffect
    app::SpellParticles particles({assets::EmitterDefinition{}, dot()}, {assets::SpellEffect{}, effect});
    ByteWriter packet;
    for (float value : {10.0f, 20.0f, 30.0f, 1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}) packet.f32(value);
    for (auto value : {333u, 334u, 0u, 0u, 600u}) packet.le(value);
    packet.le<std::uint8_t>(0); // light
    packet.le<std::uint32_t>(0);
    packet.le<std::uint8_t>(1); // range
    packet.le<std::uint8_t>(9); // spell missile
    packet.le<std::uint8_t>(0); // skill
    packet.le<std::uint8_t>(0); // explicit destination flag
    for (int i = 0; i < 3; ++i) packet.f32(0);
    packet.fixed_string("IT123", 27);
    REQUIRE(packet.data().size() == 116);
    rig.conn.receive_zone_packet_for_test({to_wire(Op::SomeItemPacketMaybe), packet.take()});
    auto incoming = rig.conn.take_missiles();
    REQUIRE(incoming.size() == 1);
    REQUIRE(incoming.front().item_type == 9);
    REQUIRE(incoming.front().spell == 600);
    REQUIRE(incoming.front().source_id == 333);
    game::MissileList missiles;
    missiles.add(game::make_missile(incoming.front(), 1000, 0));
    game::MissileWorld world;
    world.spawn = [&](std::uint32_t id) -> std::optional<game::MissileSpawn> {
        const auto& spawns = rig.conn.world().spawns;
        auto it = spawns.find(id);
        if (it == spawns.end()) return std::nullopt;
        return game::MissileSpawn{id, it->second.position, 4, 6, it->second.race, false};
    };
    world.spell = [&](std::uint32_t id) { return table->get(id); };
    int played_stage = -1;
    world.effect = [&](const game::Missile& m, int stage) {
        played_stage = stage;
        particles.missile_effect(rig.conn, m, stage, nullptr);
    };
    world.ended = [&](const game::Missile& m) { particles.end_missile(m.serial); };
    particles.set_missile_pose([&](std::uint32_t serial) -> std::optional<glm::mat4> {
        for (const auto& m : missiles.missiles())
            if (m.serial == serial) return glm::translate(glm::mat4(1), m.actor_position);
        return std::nullopt;
    });
    missiles.update(1000, world);
    REQUIRE(missiles.missiles().size() == 1);
    REQUIRE(missiles.missiles().front().has_actor);
    REQUIRE(played_stage == 2);
    particles.update(rig.conn, {}, {}, nullptr, 0.01f);
    const auto& emitters = particles.simulation().emitters();
    REQUIRE(emitters.size() == 3);
    CHECK(emitters[0].actor == (0x80000000u | missiles.missiles().front().serial));
    CHECK(near(emitters[0].origin, rig.conn.world().spawns.at(334).position));
    CHECK(emitters[1].actor == 333);
    CHECK(near(emitters[1].origin, rig.conn.world().spawns.at(333).position));
    CHECK(emitters[2].actor == 0);
    CHECK(near(emitters[2].origin, rig.conn.world().spawns.at(334).position));
    missiles.update(2033, world);
    REQUIRE(particles.simulation().emitters().size() == 2);
    CHECK(std::none_of(particles.simulation().emitters().begin(), particles.simulation().emitters().end(),
                       [](const auto& e) { return (e.actor & 0x80000000u) != 0; }));
    particles.clear();
    CHECK(particles.simulation().emitters().empty());
}

TEST_CASE("Spell missile duration reads the native AE clamp and its first-slot cloud exception") {
    game::SpellTable table;
    REQUIRE(table.parse(spell_line(600, 8, 20000) + "\n" + spell_line(601, 8, 20000, false, 76)) == 2);
    REQUIRE(table.get(600));
    REQUIRE(table.get(601));
    CHECK(table.get(600)->ae_duration == 14000);
    CHECK(table.get(601)->ae_duration == 20000);
}

TEST_CASE("Spell859 missile destruction invokes the local Action presentation without applying a buff") {
    using namespace net;
    using namespace test;
    Rig rig;
    REQUIRE(rig.to_zone());
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line(859, 5, 0, false, 1, true)) == 1);
    rig.conn.set_spell_table_for_test(table);
    const auto me = rig.conn.world().player_id;
    REQUIRE(rig.conn.world().player());
    const auto position = rig.conn.world().player()->position;
    rig.server.add_spawn(make_spawn("FixtureCaster", 333, 1, 1, 50, 60, 7));
    REQUIRE(rig.until([&] { return rig.conn.world().spawns.contains(333); }));
    rig.conn.take_spell_effect_cues();
    rig.conn.take_sound_cues();
    const auto hp = rig.conn.player_status().hp;
    const auto original_buffs = rig.conn.spell_state().buffs;
    game::MissilePacket packet;
    packet.source_id = 333;
    packet.item_type = 9;
    packet.spell = 859;
    packet.position = position + glm::vec3(39.0f, 0, 1000.0f); // destruction distance deliberately ignores height
    game::MissileWorld world;
    world.local_id = me;
    world.spawn = [&](std::uint32_t id) -> std::optional<game::MissileSpawn> {
        auto it = rig.conn.world().spawns.find(id);
        if (it == rig.conn.world().spawns.end()) return std::nullopt;
        return game::MissileSpawn{id, it->second.position, 4, 6, it->second.race, false};
    };
    world.spell = [&](std::uint32_t id) { return table->get(id); };
    int local_hits = 0;
    world.spell_hit = [&](const game::Missile& m) {
        ++local_hits;
        rig.conn.spell_missile_hit(m);
    };
    game::MissileList missiles;
    missiles.add(game::make_missile(packet, 1000, 0));
    missiles.update(1000, world);
    missiles.update(1066, world);
    REQUIRE(local_hits == 1);
    auto cues = rig.conn.take_spell_effect_cues();
    REQUIRE(cues.size() == 1);
    CHECK(cues.front().stage == 2);
    CHECK(cues.front().source == 333);
    CHECK(cues.front().target == me);
    CHECK_FALSE(cues.front().proc); // no invented proc cast stage or animation broadcast
    auto sounds = rig.conn.take_sound_cues();
    REQUIRE(sounds.size() == 1);
    CHECK(sounds.front().kind == SoundCue::Kind::SpellLanded);
    CHECK(rig.conn.player_status().hp == hp);
    REQUIRE(rig.conn.spell_state().buffs.size() == original_buffs.size());
    for (std::size_t i = 0; i < original_buffs.size(); ++i) {
        CHECK(rig.conn.spell_state().buffs[i].spell == original_buffs[i].spell);
        CHECK(rig.conn.spell_state().buffs[i].tics == original_buffs[i].tics);
    }
    CHECK(std::none_of(rig.conn.spell_state().buffs.begin(), rig.conn.spell_state().buffs.end(),
                       [](const auto& buff) { return buff.spell == 859; }));
    assets::SpellEffect effect;
    effect.stages[2].slots[0] = {1, 0, 3, 8};
    app::SpellParticles particles({assets::EmitterDefinition{}, dot()}, {assets::SpellEffect{}, effect});
    particles.update(rig.conn, cues, {}, nullptr, 0.01f);
    REQUIRE(particles.simulation().emitters().size() == 1);
    CHECK(particles.simulation().emitters().front().actor == me);
    CHECK(near(particles.simulation().emitters().front().origin, position));
    packet.position.x = position.x + 40.0f;
    missiles.add(game::make_missile(packet, 2000, 0));
    missiles.update(2000, world);
    missiles.update(2066, world);
    CHECK(local_hits == 1); // strict <40 at the destruction site
    CHECK(rig.conn.take_spell_effect_cues().empty());

    // A real success Action for the same fixture must be capable of applying a lasting buff, so the assertion
    // above proves the synthetic path's missing success flag rather than an inert spell definition.
    Action success;
    success.source = 333;
    success.target = static_cast<std::uint16_t>(me);
    success.type = 231;
    success.spell_id = 859;
    success.level = success.spell_level = 50;
    success.effect_flag = 4;
    rig.conn.receive_zone_packet_for_test(encode(success));
    CHECK(std::any_of(rig.conn.spell_state().buffs.begin(), rig.conn.spell_state().buffs.end(),
                     [](const auto& buff) { return buff.spell == 859 && buff.tics > 0; }));
}
