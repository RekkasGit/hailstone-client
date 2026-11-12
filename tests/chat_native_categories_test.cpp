#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "game/chat_native_categories.h"
#include "mock_server/harness.h"
#include "net/social_packets.h"
#include "net/spell_packets.h"

using namespace hailstone;

TEST_CASE("Expanded native chat categories distinguish participants without changing the original rules") {
    constexpr std::array<game::Con, 7> cons = {game::Con::Red, game::Con::Yellow, game::Con::White,
        game::Con::DarkBlue, game::Con::LightBlue, game::Con::Green, game::Con::Gray};
    for (std::size_t i = 0; i < cons.size(); ++i)
        CHECK(game::native_consider_chat_color(cons[i]) == 390 + i);
    CHECK(game::con_chat_color(game::Con::White) == 10); // the native rule and plugin API retain their meaning
    CHECK(game::native_death_chat_color(true, false) == 358);
    CHECK(game::native_death_chat_color(false, false) == 278);
    CHECK(game::native_death_chat_color(false, true) == 277);
    for (std::uint8_t type = 0xfa; type <= 0xfd; ++type) {
        CHECK(game::native_environment_chat_color(type, true, 0, 7) == 361);
        CHECK(game::native_environment_chat_color(type, false, 0, 7) == 362);
    }
    for (std::uint8_t type = 0xf3; type <= 0xfa; ++type) {
        CHECK(game::native_environment_chat_color(type, true, 10, 7) == 329);
        CHECK(game::native_environment_chat_color(type, false, 7, 7) == 363);
        CHECK(game::native_environment_chat_color(type, false, 10, 7) == 364);
        CHECK(game::native_environment_chat_color(type, true, 0, 7) == 361);
    }
    CHECK(game::native_random_chat_color(true, true) == 287);
    CHECK(game::native_random_chat_color(true, false) == 287);
    CHECK(game::native_random_chat_color(false, true) == 360);
    CHECK(game::native_random_chat_color(false, false) == 359);
    CHECK(game::native_spell_hit_chat_color(-20, false, true, false) == 374);
    CHECK(game::native_spell_hit_chat_color(-20, false, false, false) == 375);
    CHECK(game::native_spell_hit_chat_color(-20, true, false, false) == 402);
    CHECK(game::native_spell_hit_chat_color(20, true, false, false) == 283);
    CHECK(game::native_spell_hit_chat_color(20, false, true, false) == 397);
    CHECK(game::native_spell_hit_chat_color(20, false, false, false) == 379);
    CHECK(game::native_spell_hit_chat_color(0, false, false, false) == 380);
    for (std::uint8_t skill : std::array<std::uint8_t, 5>{12, 41, 49, 54, 70}) {
        CHECK(game::native_song_skill(skill));
        CHECK(game::native_spell_hit_chat_color(0, false, false, game::native_song_skill(skill)) == 378);
        CHECK(game::native_spell_hit_chat_color(20, true, false, game::native_song_skill(skill)) == 283);
        CHECK(game::native_spell_hit_chat_color(20, false, true, game::native_song_skill(skill)) == 397);
        CHECK(game::native_spell_hit_chat_color(-20, true, false, game::native_song_skill(skill)) == 402);
        CHECK(game::native_spell_hit_chat_color(-20, false, true, game::native_song_skill(skill)) == 374);
    }
    CHECK_FALSE(game::native_song_skill(24));
}

TEST_CASE("Stock RoF2 packets produce the expanded native chat categories once") {
    test::Rig rig;
    std::ofstream(rig.dir.path() / "eqstr_us.txt", std::ios::app)
        << "12187 YOU\n12118 FALL %1\n12130 DROWN %1\n12133 THORNS\n12134 THORNS %1\n"
        << "1086 RANDOM %1\n1087 ROLL %1 %2 %3\n12113 SLAIN %1\n12239 CON %1 %2 %3\n";
    rig.conn.reload_game_files();
    REQUIRE(rig.to_zone());
    rig.chat();

    net::CombatDamage hit;
    hit.target = 7; hit.type = 0xfc; hit.damage = 30;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 101; hit.type = 0xfb;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 7; hit.source = 101; hit.type = 0xf9; hit.damage = -10;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 101; hit.source = 7;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.source = 102;
    rig.server.zone.send_reliable(net::encode(hit));
    rig.server.zone.send_reliable(net::encode(net::Consider{7, 101, 5, 6, 0}));
    rig.server.zone.send_reliable(net::encode(net::RandomReply{1, 100, 20, "Gennro"}));
    rig.server.zone.send_reliable(net::encode(net::RandomReply{1, 100, 20, "Somebody"}));
    net::Death death;
    death.spawn_id = 101; death.killer_id = 7;
    rig.server.zone.send_reliable(net::encode(death));
    rig.server.add_spawn(test::make_spawn("Another", 103, 0, 1, -285, -148, 3.5f));
    death.spawn_id = 103;
    rig.server.zone.send_reliable(net::encode(death));
    rig.server.chat("Marker", 8, "native-category-marker");

    std::vector<net::ChatEvent> lines;
    REQUIRE(rig.until([&] {
        auto batch = rig.chat();
        for (auto& line : batch) lines.push_back(std::move(line));
        return std::any_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.text.find("native-category-marker") != std::string::npos;
        });
    }));
    auto count = [&](std::uint32_t color) {
        return std::count_if(lines.begin(), lines.end(), [&](const auto& line) { return line.color == color; });
    };
    for (std::uint32_t color : {361u, 362u, 329u, 363u, 364u, 396u, 358u, 278u}) CHECK(count(color) == 1);
    CHECK(count(287) == 2); // each roll remains the same two native lines
    CHECK(count(359) == 2);
    auto con = std::find_if(lines.begin(), lines.end(), [](const auto& line) { return line.color == 396; });
    REQUIRE(con != lines.end());
    CHECK(con->rgb == 0); // the user's per-category palette supplies the colour
}

TEST_CASE("Native spell landing and interruption packets retain text with their specific categories") {
    test::Rig rig;
    REQUIRE(rig.to_zone());
    rig.chat();
    auto spells = std::make_shared<game::SpellTable>();
    auto record = [](std::uint32_t id, std::uint8_t skill) {
        std::vector<std::string> fields(237, "0");
        fields[0] = std::to_string(id);
        fields[1] = "Fixture spell";
        fields[4] = "CAST SELF";
        fields[5] = " CAST OTHER";
        fields[6] = "LANDED SELF";
        fields[7] = " LANDED OTHER";
        fields[83] = "1";
        fields[100] = std::to_string(skill);
        std::string text;
        for (const auto& field : fields) {
            if (!text.empty()) text += '^';
            text += field;
        }
        return text + '\n';
    };
    REQUIRE(spells->parse(record(10, 41) + record(11, 24)) == 2);
    rig.conn.set_spell_table_for_test(spells);
    auto bard = test::make_spawn("Bard", 103, 0, 1, -285, -148, 3.5f);
    bard.class_ = 8;
    rig.server.add_spawn(bard);
    rig.server.zone.send_reliable(net::encode(net::BeginCast{10, 103, 200}));
    net::CombatDamage hit;
    hit.target = 7; hit.source = 103; hit.type = 231; hit.spell_id = 10;
    rig.server.zone.send_reliable(net::encode(hit));
    rig.server.zone.send_reliable(net::encode(net::InterruptCast{103, 123, {}}));
    rig.server.zone.send_reliable(net::encode(net::InterruptCast{7, 123, {}}));
    rig.server.zone.send_reliable(net::encode(net::InterruptCast{0, 123, {}}));
    hit.spell_id = 11; hit.damage = 20;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.damage = -20;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 101; hit.source = 7;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.source = 103;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.damage = 20; hit.source = 7;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.source = 103;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 7; hit.damage = 0;
    rig.server.zone.send_reliable(net::encode(hit));
    rig.server.chat("Marker", 8, "native-spell-marker");
    std::vector<net::ChatEvent> lines;
    REQUIRE(rig.until([&] {
        auto batch = rig.chat();
        for (auto& line : batch) lines.push_back(std::move(line));
        return std::any_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.text.find("native-spell-marker") != std::string::npos;
        });
    }));
    auto count = [&](std::uint32_t color) {
        return std::count_if(lines.begin(), lines.end(), [&](const auto& line) { return line.color == color; });
    };
    CHECK(count(378) == 2);
    CHECK(count(289) == 2);
    for (std::uint32_t color : {406u, 283u, 402u, 374u, 375u, 397u, 379u, 380u}) CHECK(count(color) == 1);
}
