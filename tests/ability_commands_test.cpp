// The ability commands: /melody and the bard twist, /autofire, /stopdisc, /dismount; the combat ability window's
// menu (CCombatAbilityWnd_RebuildAbilityMenus / CategorizeAbility) and its [CombatAbilities] buttons.
#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

#include "core/byte_io.h"
#include "core/ini_file.h"
#include "game/combat_ability_menu.h"
#include "game/hotbuttons.h"
#include "game/mechanics/sheet_builder.h"
#include "mock_server/harness.h"
#include "mq_test_world.h"

using namespace hailstone;

namespace {

using A = net::GameConnectionTestAccess;

std::vector<std::string> drain(net::GameConnection& c) {
    std::vector<std::string> out;
    while (auto e = c.poll_chat()) out.push_back(e->text);
    return out;
}

// OP_XTargetResponse {max, count, slot, status, id, name}: one slot holding a spawn.
net::AppPacket xtarget_slot(std::uint32_t slot, std::uint32_t id, const std::string& name) {
    ByteWriter w;
    w.le<std::uint32_t>(5);
    w.le<std::uint32_t>(1);
    w.le<std::uint32_t>(slot);
    w.le<std::uint8_t>(1);
    w.le<std::uint32_t>(id);
    w.fixed_string(name, name.size() + 1);
    return net::AppPacket(net::to_wire(net::Op::XTargetResponse), w.take());
}

net::AppPacket death_of(std::uint32_t id) {
    net::Death d;
    d.spawn_id = id;
    d.killer_id = 1;
    d.corpse_id = id;
    return net::encode(d);
}

net::AppPacket hit_on_me(std::uint32_t from, std::int32_t damage = 5) {
    net::CombatDamage d;
    d.target = 1;
    d.source = static_cast<std::uint16_t>(from);
    d.damage = damage;
    return net::encode(d);
}

} // namespace

TEST_CASE("the combat ability menu files abilities by category name, subcategory name and level") {
    const std::map<std::int32_t, std::string> names{{10, "Melee"}, {11, "Defensive"}, {12, "Offensive"},
                                                    {20, "Archery"}, {21, "Utility"}};
    auto name_of = [&](std::int32_t id) {
        auto it = names.find(id);
        return it == names.end() ? std::string() : it->second;
    };
    std::vector<game::CombatAbilityEntry> list{
        {0, 10, 12, 0, 20, "Rage"},
        {1, 10, 11, 12, 30, "Guard"},    // under Defensive and Offensive
        {2, 20, 21, 0, 5, "Aim"},
        {3, 10, 12, 0, 25, "Fury"},      // before Rage (higher level)
        {4, 0, 11, 0, 50, "Nothing"},    // category 0: not filed
        {5, 10, 12, 0, 25, "Fury Two"},  // same level as Fury: after it
        {6, 10, 0, 0, 9, "Loose"},       // no subcategory: not filed
    };
    auto menu = game::build_combat_ability_menu(list, name_of);
    REQUIRE(menu.size() == 2);
    CHECK(menu[0].name == "Archery"); // strcmp order
    CHECK(menu[1].name == "Melee");
    REQUIRE(menu[1].children.size() == 2);
    CHECK(menu[1].children[0].name == "Defensive");
    CHECK(menu[1].children[1].name == "Offensive");
    const auto& off = menu[1].children[1].items;
    REQUIRE(off.size() == 4);
    CHECK(off[0].text == "30 - Guard");
    CHECK(off[1].text == "25 - Fury");
    CHECK(off[2].text == "25 - Fury Two");
    CHECK(off[3].text == "20 - Rage");
    CHECK(off[3].index == 0);
    REQUIRE(menu[0].children.size() == 1);
    CHECK(menu[0].children[0].items.at(0).text == "05 - Aim");
    CHECK(game::build_combat_ability_menu({}, name_of).empty());
}

TEST_CASE("the combat ability buttons are kept in [CombatAbilities]") {
    game::HotButtonStore s;
    for (std::int32_t v : s.combat_abilities) CHECK(v == -1);
    s.combat_abilities[0] = game::kCombatAbilityValueBase + 3;
    s.combat_abilities[7] = game::kCombatAbilityValueBase;
    IniFile ini;
    s.save(ini);
    CHECK(ini.get_int("CombatAbilities", "CombatAbility1", 0) == 0x84 + 3);
    CHECK(ini.get_int("CombatAbilities", "CombatAbility2", 0) == -1);
    game::HotButtonStore t;
    t.load(ini);
    CHECK(t.combat_abilities == s.combat_abilities);
}

TEST_CASE("/melody builds the bard twist and plays it in order") {
    mq_test::World w("hailstone_melody_test");
    auto& c = *w.conn;
    // Not a bard: nothing.
    c.melody_command("1 2");
    CHECK(c.bard_twist().empty());
    A::profile(c)->class_ = 8;
    // Numbers above 0 join, anything else separates them; the first gem is clicked (the position moves on).
    c.melody_command("2, 0 x3 12 99");
    CHECK(c.bard_twist() == std::vector<std::int32_t>{2, 3, 12, 99});
    CHECK(c.current_bard_twist_gem() == 2);
    CHECK(c.next_bard_twist_gem() == 3);
    CHECK(c.current_bard_twist_gem() == 3);
    CHECK(c.next_bard_twist_gem() == 12);
    CHECK(c.next_bard_twist_gem() == 99);
    CHECK(c.next_bard_twist_gem() == 2); // wraps
    // ClearBardTwist (/cast, a gem click, /stopsong).
    c.clear_bard_twist();
    CHECK(c.bard_twist().empty());
    CHECK(c.next_bard_twist_gem() == 0);
    CHECK(c.current_bard_twist_gem() == 0);
}

TEST_CASE("a gem click while a bard song plays stops the song; the next click is free to cast") {
    // HandleSpellLeftClick @0x6488f0: not ready or casting, with a bard song playing, ClearBardTwist and
    // StopCurrentSpellCast(0); anything else "You haven't recovered yet...". The song's cast stays set after it lands
    // (OP_ManaChange keepcasting), so without the stop a bard could never cast again.
    mq_test::World w("hailstone_bard_song_click_test");
    auto& c = *w.conn;
    auto& sp = A::spells(c);
    A::profile(c)->class_ = 8;
    sp.gems[2].spell = 3373; // Test Chant, a song
    c.melody_command("3 1");
    sp.casting = {};
    sp.casting.spell = 3373;
    sp.casting.slot = 2;
    sp.gems[2].ready = 0xffffffffu; // not ready either: the song still stops
    sp.recovery_ready = 0xffffffffu;
    drain(c);
    c.cast_gem(0);
    CHECK_FALSE(sp.casting.casting());
    CHECK(c.bard_twist().empty());
    CHECK(sp.recovery_ready == 0);
    CHECK(sp.gems[2].ready == 0); // a song without a recast unlocks with the song's end
    CHECK(drain(c).size() == 1);  // 12687 Your song ends.

    // Not a song (Minor Healing, cast by a bard): refused, and the cast goes on.
    sp.casting.spell = 200;
    sp.casting.slot = 0;
    c.cast_gem(1);
    CHECK(sp.casting.spell == 200);
    CHECK(drain(c).size() == 1); // 12029

    // The same song for a cleric is not a song: refused.
    A::profile(c)->class_ = 2;
    sp.casting.spell = 3373;
    c.cast_gem(0);
    CHECK(sp.casting.spell == 3373);
    CHECK(drain(c).size() == 1);
}

TEST_CASE("/melody: a gem number of ten digits or more wraps in 32 bits") {
    // The digits accumulate as core::msvc_atoi's do; a signed product overflowed (undefined behaviour).
    mq_test::World w("hailstone_melody_wrap_test");
    auto& c = *w.conn;
    A::profile(c)->class_ = 8;
    // 4294967309 is 13 (no such gem: nothing is clicked), 2147483648 is negative and left out.
    c.melody_command("4294967309 2147483648 99999999999");
    CHECK(c.bard_twist() == std::vector<std::int32_t>{13, static_cast<std::int32_t>(99999999999ull % 4294967296ull)});
}

TEST_CASE("A periodic interrupt clears a normal cast and ends a bard song abruptly") {
    mq_test::World w("hailstone_periodic_interrupt_test");
    auto& c = *w.conn;
    auto& sp = A::spells(c);
    sp.casting.spell = 200;
    sp.pending = 2;
    c.stop_current_spell_cast(1);
    CHECK_FALSE(sp.casting.casting());
    CHECK(sp.pending == 1);
    CHECK(drain(c).empty()); // Non-song interruptions have no song-ended line.

    A::profile(c)->class_ = 8;
    sp.gems[2].spell = 3373;
    sp.gems[2].ready = 99;
    sp.recovery_ready = 99;
    sp.casting.spell = 3373;
    c.stop_current_spell_cast(1);
    CHECK_FALSE(sp.casting.casting());
    CHECK(sp.gems[2].ready == 0);
    CHECK(sp.recovery_ready == 0);
    CHECK(sp.pending == 1); // Songs do not consume an ordinary pending cast.
    CHECK(drain(c).size() == 1);

    // StopSpellCast keeps a bard's non-song gem with <=100 ms recovery, but resets its item cast.
    sp.casting.spell = 200; // The fixture has zero recovery.
    sp.pending = 1;
    c.stop_current_spell_cast(1);
    CHECK(sp.casting.spell == 200);
    CHECK(sp.pending == 0);
    sp.casting.item_location = {0, 23, 0};
    c.stop_current_spell_cast(1);
    CHECK_FALSE(sp.casting.casting());
}

TEST_CASE("StopSpellCast: a bard's non-song never interrupts or restarts the twist; others interrupt for reason 0") {
    // stop_spell_cast.cpp:84-101: a bard's non-song returns at the launch data reset (kept outright when its
    // recovery is at most 100 ms and it is not an item cast). A non-bard's non-song sends the empty OP_ManaChange
    // for reason 0 only (feign death's StopCastAndFeignDeath is such a caller).
    test::Rig rig;
    REQUIRE(rig.to_zone());
    auto& c = rig.conn;
    auto spells = std::make_shared<game::SpellTable>();
    REQUIRE(spells->parse(mq_test::spell_line({{0, "6800"}, {1, "Slow Recovery"}, {13, "1000"}, {14, "2000"},
                                               {83, "1"}}) + "\n" +
                          mq_test::spell_line({{0, "6801"}, {1, "Quick Recovery"}, {13, "1000"}, {83, "1"}})) == 2);
    c.set_spell_table_for_test(spells);
    auto profile = *c.profile();
    profile.class_ = 8;
    profile.level = 60;
    c.set_profile_for_test(profile);
    auto& sp = A::spells(c);
    sp.gems[0].spell = 6801; // the twist's gem 1: a restart would cast it
    sp.gems[0].ready = 0;
    sp.recovery_ready = 0;
    c.melody_command("1");
    REQUIRE(c.next_bard_twist_gem() == 1);
    const auto mana_changes = [&] { return test::MockEqServer::count(rig.server.zone_log, net::Op::ManaChange); };
    const auto casts = [&] { return test::MockEqServer::count(rig.server.zone_log, net::Op::CastSpell); };
    for (std::uint8_t reason : {std::uint8_t{0}, std::uint8_t{1}}) {
        sp.casting = {};
        sp.casting.spell = 6800;
        c.stop_current_spell_cast(reason);
        CHECK_FALSE(sp.casting.casting());
    }
    sp.casting = {};
    sp.casting.spell = 6801;
    c.stop_current_spell_cast(0);
    CHECK(sp.casting.spell == 6801); // short recovery, not an item cast: kept
    sp.casting.item_location = {0, 23, 0};
    c.stop_current_spell_cast(0);
    CHECK_FALSE(sp.casting.casting());

    profile.class_ = 2;
    c.set_profile_for_test(profile);
    sp.casting = {};
    sp.casting.spell = 6800;
    c.stop_current_spell_cast(1);
    CHECK_FALSE(sp.casting.casting());
    sp.casting.spell = 6801;
    c.stop_current_spell_cast(0);
    CHECK_FALSE(sp.casting.casting());
    REQUIRE(rig.until([&] { return mana_changes() >= 1; }));
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(mana_changes() == 1); // only the non-bard's reason 0
    CHECK(casts() == 0);        // no twist restart
}

TEST_CASE("Unconscious status follows local damage and conscious recovery without overriding death") {
    mq_test::World w("hailstone_consciousness_test");
    auto& c = *w.conn;
    auto& st = A::status(c);
    st.hp = 0;
    const auto serial = c.consciousness_changes();
    c.update_player_consciousness(true);
    CHECK(st.owner_status == 3);
    CHECK(st.stand_state == net::kStandLying);
    CHECK(c.player_down());
    CHECK(c.live_state().owner_status == 3);
    CHECK(c.consciousness_changes() == serial + 1);
    c.update_player_consciousness(true);
    CHECK(c.consciousness_changes() == serial + 1);
    st.hp = 1;
    CHECK(c.player_down()); // Healing waits for GetConscious after the next HP tick.
    c.update_player_consciousness();
    CHECK(st.owner_status == 0);
    CHECK(st.stand_state == net::kStandStanding);
    CHECK_FALSE(c.player_down());
    CHECK(c.consciousness_changes() == serial + 2);

    st.owner_status = 4;
    st.dead = true;
    st.hp = 100;
    c.update_player_consciousness();
    CHECK(st.owner_status == 4);
    CHECK(st.dead);
    CHECK(c.consciousness_changes() == serial + 2);
}

TEST_CASE("a fresh zone clears the previous character body's unconscious or dead status") {
    for (std::int8_t previous : {std::int8_t{3}, std::int8_t{4}}) {
        mq_test::World w("hailstone_consciousness_zone_test");
        auto& c = *w.conn;
        auto& status = A::status(c);
        status.owner_status = previous;
        status.dead = previous == 4;
        status.stand_state = net::kStandLying;
        c.receive_world_packet_for_test(net::encode(net::ZoneServerInfo{"127.0.0.1", 9000}));
        CHECK(status.owner_status == 0);
        CHECK_FALSE(status.dead);
        CHECK(status.stand_state == net::kStandStanding);
        CHECK_FALSE(c.player_down());
    }
}

TEST_CASE("/autofire needs a target and swaps auto attack for auto fire") {
    mq_test::World w("hailstone_autofire_test");
    auto& c = *w.conn;
    drain(c);
    A::world(c).target_id = 0;
    c.autofire_command();
    CHECK_FALSE(c.auto_fire());
    CHECK(drain(c).size() == 1); // 12697
    A::world(c).target_id = 10;
    REQUIRE(c.auto_attack());
    c.autofire_command();
    CHECK(c.auto_fire());
    CHECK_FALSE(c.auto_attack());
    CHECK(drain(c).size() == 1); // Auto fire on.
    // SetAutoRangeAttack(0): "Auto fire off." once.
    c.set_auto_fire(false);
    c.set_auto_fire(false);
    CHECK_FALSE(c.auto_fire());
    CHECK(drain(c).size() == 1);
    c.autofire_command();
    c.autofire_command();
    CHECK_FALSE(c.auto_fire());
}

TEST_CASE("attack_until_xtarget_clear: auto attack outlives the target while the extended target list has more") {
    // The original (DeathReport: the target died): auto attack goes off.
    {
        mq_test::World w("hailstone_attack_hold_off_test");
        auto& c = *w.conn;
        c.receive_zone_packet_for_test(xtarget_slot(0, 10, "a_rat01"));
        c.receive_zone_packet_for_test(xtarget_slot(1, 11, "Fippy_Darkpaw000"));
        REQUIRE(c.auto_attack());
        c.receive_zone_packet_for_test(death_of(10));
        CHECK_FALSE(c.auto_attack());
    }
    // The option: on while Fippy is left, off once Fippy dies too.
    {
        mq_test::World w("hailstone_attack_hold_on_test");
        auto& c = *w.conn;
        c.set_attack_until_xtarget_clear(true);
        c.receive_zone_packet_for_test(xtarget_slot(0, 10, "a_rat01"));
        c.receive_zone_packet_for_test(xtarget_slot(1, 11, "Fippy_Darkpaw000"));
        c.receive_zone_packet_for_test(death_of(10));
        CHECK(c.world().target_id == 0);
        CHECK(c.auto_attack());
        c.update();
        CHECK(c.auto_attack());
        c.receive_zone_packet_for_test(death_of(11));
        c.update();
        CHECK_FALSE(c.auto_attack());
    }
    // Nothing else on the list: off at once, as without the option.
    {
        mq_test::World w("hailstone_attack_hold_empty_test");
        auto& c = *w.conn;
        c.set_attack_until_xtarget_clear(true);
        c.receive_zone_packet_for_test(xtarget_slot(0, 10, "a_rat01"));
        c.receive_zone_packet_for_test(death_of(10));
        CHECK_FALSE(c.auto_attack());
    }
    // A new target taken while held: the usual rules from then on (the list emptying does not stop the attack).
    {
        mq_test::World w("hailstone_attack_hold_retarget_test");
        auto& c = *w.conn;
        c.set_attack_until_xtarget_clear(true);
        c.receive_zone_packet_for_test(xtarget_slot(0, 10, "a_rat01"));
        c.receive_zone_packet_for_test(xtarget_slot(1, 11, "Fippy_Darkpaw000"));
        c.receive_zone_packet_for_test(death_of(10));
        REQUIRE(c.auto_attack());
        A::world(c).target_id = 12;
        c.update();
        c.receive_zone_packet_for_test(death_of(11));
        c.update();
        CHECK(c.auto_attack());
    }
}

TEST_CASE("auto_target_xtarget: with no target, an NPC on the extended target list that attacks becomes the target") {
    // No target: the attacker on the list is taken (a miss counts).
    {
        mq_test::World w("hailstone_auto_target_hit_test");
        auto& c = *w.conn;
        REQUIRE(c.auto_target_xtarget());
        A::world(c).target_id = 0;
        c.receive_zone_packet_for_test(xtarget_slot(0, 10, "a_rat01"));
        c.receive_zone_packet_for_test(xtarget_slot(1, 11, "Fippy_Darkpaw000"));
        c.receive_zone_packet_for_test(hit_on_me(11, 0));
        c.update();
        CHECK(c.world().target_id == 11);
    }
    // A target held: unchanged.
    {
        mq_test::World w("hailstone_auto_target_held_test");
        auto& c = *w.conn;
        c.receive_zone_packet_for_test(xtarget_slot(0, 11, "Fippy_Darkpaw000"));
        c.receive_zone_packet_for_test(hit_on_me(11));
        c.update();
        CHECK(c.world().target_id == 10);
    }
    // An attacker that is not on the list, or a player: nothing.
    {
        mq_test::World w("hailstone_auto_target_off_list_test");
        auto& c = *w.conn;
        A::world(c).target_id = 0;
        c.receive_zone_packet_for_test(hit_on_me(11));
        c.receive_zone_packet_for_test(hit_on_me(12));
        c.update();
        CHECK(c.world().target_id == 0);
    }
    // The target dies: the other attacker still on the list is taken; one only on the list (never hit) is not.
    {
        mq_test::World w("hailstone_auto_target_death_test");
        auto& c = *w.conn;
        c.receive_zone_packet_for_test(xtarget_slot(0, 10, "a_rat01"));
        c.receive_zone_packet_for_test(xtarget_slot(1, 11, "Fippy_Darkpaw000"));
        c.receive_zone_packet_for_test(hit_on_me(10));
        c.receive_zone_packet_for_test(death_of(10));
        c.update();
        CHECK(c.world().target_id == 0);
        A::world(c).target_id = 10; // (a stand-in target, so the next hit only records the attacker)
        c.receive_zone_packet_for_test(hit_on_me(11));
        A::world(c).target_id = 0;
        c.update();
        CHECK(c.world().target_id == 0); // cleared by hand: held until the next hit
        A::world(c).target_id = 11;
        c.receive_zone_packet_for_test(xtarget_slot(2, 14, "a_bat00"));
        w.add(14, "a_bat00", true, 3, {0, 0, 0});
        c.receive_zone_packet_for_test(hit_on_me(14));
        c.receive_zone_packet_for_test(death_of(11));
        c.update();
        CHECK(c.world().target_id == 14);
    }
    // The option off: the original's behaviour.
    {
        mq_test::World w("hailstone_auto_target_disabled_test");
        auto& c = *w.conn;
        c.set_auto_target_xtarget(false);
        A::world(c).target_id = 0;
        c.receive_zone_packet_for_test(xtarget_slot(0, 11, "Fippy_Darkpaw000"));
        c.receive_zone_packet_for_test(hit_on_me(11));
        c.update();
        CHECK(c.world().target_id == 0);
    }
}

TEST_CASE("face_on_attack: the attack key turns the player to the target only when the option is on") {
    mq_test::World w("hailstone_face_on_attack_test");
    auto& c = *w.conn;
    REQUIRE(c.world().target_id == 10);
    c.set_auto_attack(false); // the test world starts attacking
    // On (the default, as the original's AUTOPRIM): turning auto attack on turns the player; turning it off does not.
    REQUIRE(c.face_on_attack());
    CHECK(c.toggle_auto_attack());
    CHECK(c.auto_attack());
    CHECK_FALSE(c.toggle_auto_attack());
    CHECK_FALSE(c.auto_attack());
    // Off: auto attack still goes on, and the heading is left alone.
    c.set_face_on_attack(false);
    CHECK_FALSE(c.toggle_auto_attack());
    CHECK(c.auto_attack());
    CHECK_FALSE(c.toggle_auto_attack());
    // Refused (the player as the target): no attack, no turn.
    c.set_face_on_attack(true);
    A::world(c).target_id = A::world(c).player_id;
    CHECK_FALSE(c.toggle_auto_attack());
    CHECK_FALSE(c.auto_attack());
}

TEST_CASE("/stopdisc and /dismount") {
    mq_test::World w("hailstone_stopdisc_test");
    auto& c = *w.conn;
    drain(c);
    // No discipline: nothing said.
    c.stop_discipline();
    CHECK(drain(c).empty());
    CHECK_FALSE(c.discipline_active());
    // Without a mount.
    c.dismount_command();
    CHECK(drain(c).size() == 1); // 13285
    CHECK(c.mount_id() == 0);
    // A mount race NPC named "<name>`s_Mount00" is the mount once their actors are made (MakeRiderMountUp);
    // another race is not.
    auto actors = [](const hailstone::game::Spawn&) {
        return std::optional<hailstone::game::WorldState::ActorModel>(hailstone::game::WorldState::ActorModel{true, false});
    };
    auto& sp = A::world(c).spawns[40];
    sp.id = 40;
    sp.name = "Tester`s_Mount00";
    sp.is_npc = true;
    sp.race = 1;
    c.smooth(0.0f, {}, actors);
    CHECK(c.mount_id() == 0);
    sp.race = 216;
    sp.actor_made = false;
    c.smooth(0.0f, {}, actors);
    CHECK(c.mount_id() == 40);
    CHECK(c.controlled_id() == 40);
    // /dismount: "Dismounting." and the player gets off.
    drain(c);
    c.dismount_command();
    CHECK(drain(c).size() == 1); // 13284
    CHECK(c.mount_id() == 0);
    CHECK(c.controlled_id() == 0);
    // Frozen.
    A::status(c).stand_state = 102;
    c.dismount_command();
    CHECK(drain(c).size() == 1); // 6159
}

TEST_CASE("CYCLE_XTARGET after the server shrank the list past the cursor ends") {
    // The cursor at slot 7 of 10; the list then shrinks to 5 (or 0) empty slots: the cycle used to look for its start
    // forever. The cursor stays in the list, and the cycle looks at every slot once.
    mq_test::World w("hailstone_xtarget_shrink_test");
    auto& c = *w.conn;
    auto response = [](std::uint32_t max, std::int32_t slot, std::uint32_t id) {
        ByteWriter b;
        b.le<std::uint32_t>(max);
        b.le<std::uint32_t>(id ? 1 : 0);
        if (id) {
            b.le<std::int32_t>(slot);
            b.le<std::uint8_t>(1);
            b.le<std::uint32_t>(id);
            b.fixed_string("a_rat01", 8);
        }
        return net::AppPacket(net::to_wire(net::Op::XTargetResponse), b.take());
    };
    c.receive_zone_packet_for_test(response(10, 7, 10));
    w.conn->world_for_test().target_id = 0;
    c.target_next_xtarget();
    REQUIRE(c.xtargets().current == 7);
    CHECK(c.world().target_id == 10);
    c.receive_zone_packet_for_test(response(5, 0, 0));
    CHECK(c.xtargets().current < 5);
    c.target_next_xtarget(); // no slot holds a spawn
    CHECK(c.xtargets().current < 5);
    c.receive_zone_packet_for_test(response(0, 0, 0));
    c.target_next_xtarget();
    CHECK(c.xtargets().current == 0);
    // A spawn in the shrunk list is still found.
    c.receive_zone_packet_for_test(response(5, 2, 11));
    c.target_next_xtarget();
    CHECK(c.world().target_id == 11);
}

TEST_CASE("a spawn killed while moving stops where it died (PlayerZoneClient::IDied zeroes its velocities)") {
    mq_test::World w("hailstone_death_stops_motion_test");
    auto& c = *w.conn;
    auto& rat = A::world(c).spawns[10];
    rat.velocity = glm::vec3(2.0f, -1.0f, 0.5f);
    rat.speed_run = 0.7f;
    rat.speed_heading = 3.0f;
    rat.fly_mode = 2;
    rat.update_time = 100.0;
    c.receive_zone_packet_for_test(death_of(10));
    CHECK(rat.is_corpse);
    CHECK(rat.velocity == glm::vec3(0.0f));
    CHECK(rat.speed_run == 0.0f);
    CHECK(rat.speed_heading == 0.0f);
    CHECK(rat.fly_mode == 0);
    // Stepped within the 12 s after its last update, the corpse stays put.
    const glm::vec3 at = rat.position;
    const float heading = rat.heading;
    A::world(c).smooth(0.5f, 101.0);
    CHECK(rat.position == at);
    CHECK(rat.heading == heading);
}
