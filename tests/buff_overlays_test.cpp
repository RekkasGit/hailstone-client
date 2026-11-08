// The numbers over the buff and song buttons (game/buff_overlays.h, CBuffWindow::PostDraw @0x6426a0).
#include <doctest/doctest.h>

#include <vector>

#include "game/buff_overlays.h"
#include "game/spells.h"

using namespace hailstone;

namespace {

game::SpellData spell(std::uint32_t id) {
    game::SpellData s;
    s.id = id;
    s.effect.fill(254);
    return s;
}

game::BuffSlot slot(std::uint32_t spell, std::uint32_t hits = 0) {
    game::BuffSlot b;
    b.spell = spell;
    b.num_hits = hits;
    return b;
}

} // namespace

TEST_CASE("buff time overlay: the largest part of SecondsToHMS, none under a second or without a duration") {
    auto s = spell(10);
    s.duration_formula = 3;
    CHECK(game::buff_time_overlay(s, 7199000)->text == "1h"); // 1 h 59 min 59 s
    CHECK(game::buff_time_overlay(s, 3600000)->text == "1h");
    CHECK(game::buff_time_overlay(s, 3599999)->text == "59m");
    CHECK(game::buff_time_overlay(s, 61000)->text == "1m");
    CHECK(game::buff_time_overlay(s, 59999)->text == "59s");
    CHECK(game::buff_time_overlay(s, 1000)->text == "1s");
    CHECK(game::buff_time_overlay(s, 1000)->argb == 0xffffffff);
    CHECK_FALSE(game::buff_time_overlay(s, 999));
    CHECK_FALSE(game::buff_time_overlay(s, -1)); // permanent
    s.duration_formula = 0;
    CHECK_FALSE(game::buff_time_overlay(s, 60000));
}

TEST_CASE("spell counters: 0x75ef10 sums the counter slots of every effect holding the spell") {
    auto s = spell(20);
    s.effect[0] = game::kSpaDiseaseCounter;
    s.base[0] = 4;
    s.effect[2] = game::kSpaPoisonCounter;
    s.base[2] = 6;
    s.effect[3] = game::kSpaCorruptionCounter;
    s.base[3] = 2;
    s.effect[5] = game::kSpaCurseCounter;
    s.base[5] = 8;
    std::vector<game::BuffSlot> effects{slot(20), slot(21), slot(20)};
    effects[0].slot_data[0] = 3;
    effects[0].slot_data[2] = 5;
    effects[0].slot_data[3] = 1;
    effects[0].slot_data[5] = 7;
    effects[1].slot_data[0] = 99; // another spell
    effects[2].slot_data[0] = 1;
    auto c = game::spell_counters(s, effects);
    CHECK(c.base == std::array<std::int32_t, 4>{4, 8, 16, 12}); // corruption, disease, curse, poison
    CHECK(c.left == std::array<std::int32_t, 4>{1, 4, 7, 5});
}

TEST_CASE("buff counter overlay: the original's pick and colours") {
    game::SpellCounters c;
    CHECK_FALSE(game::buff_counter_overlay(c));
    c.base = {0, 5, 0, 0};
    c.left = {0, 3, 0, 0};
    auto o = game::buff_counter_overlay(c);
    REQUIRE(o);
    CHECK(o->text == "3");
    CHECK(o->argb == 0xffffff00); // disease
    // Corruption with a base in 1..999998 starts the pick with its counters left; a later base below that value
    // takes over (the base is compared with the value).
    c.base = {4, 5, 0, 0};
    c.left = {6, 2, 0, 0};
    o = game::buff_counter_overlay(c);
    CHECK(o->text == "2");
    CHECK(o->argb == 0xffffff00);
    c.left = {3, 2, 0, 0};
    o = game::buff_counter_overlay(c);
    CHECK(o->text == "3");
    CHECK(o->argb == 0xffff8c00); // corruption
    c.base = {0, 0, 9, 3};
    c.left = {0, 0, 4, 1};
    o = game::buff_counter_overlay(c);
    CHECK(o->text == "1"); // curse 4 picked, then poison's base 3 < 4
    CHECK(o->argb == 0xff00ff00);
    c.base = {0, 0, 9, 0};
    o = game::buff_counter_overlay(c);
    CHECK(o->argb == 0xffff0000); // curse
    // A corruption base past 999998 leaves 999999, drawn when nothing else takes over.
    c.base = {1000000, 0, 0, 0};
    c.left = {5, 0, 0, 0};
    CHECK(game::buff_counter_overlay(c)->text == "999999");
    // Nothing left: not drawn.
    c.base = {0, 4, 0, 0};
    c.left = {0, 0, 0, 0};
    CHECK_FALSE(game::buff_counter_overlay(c));
}

TEST_CASE("buff limit overlay: the last effect's hits left, only for a spell with a hit count") {
    auto s = spell(30);
    std::vector<game::BuffSlot> effects{slot(30, 4), slot(31, 9), slot(30, 2)};
    CHECK_FALSE(game::buff_limit_overlay(s, effects));
    s.hit_count = 5;
    auto o = game::buff_limit_overlay(s, effects);
    REQUIRE(o);
    CHECK(o->text == "2");
    CHECK(o->argb == 0xffffffff);
    effects[2].num_hits = 0;
    CHECK_FALSE(game::buff_limit_overlay(s, effects)); // the last one counts, even at 0
}
