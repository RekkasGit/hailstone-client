#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "game/attack_check.h"
#include "game/spells.h"
#include "game/world_state.h"

namespace {

// A spells_us.txt line with the given columns set (the others 0, names empty, effects 254).
std::string spell_line(std::initializer_list<std::pair<int, std::string>> set) {
    std::vector<std::string> cols(237, "0");
    for (int i = 1; i <= 8; ++i) cols[static_cast<std::size_t>(i)].clear();
    for (int i = 86; i <= 97; ++i) cols[static_cast<std::size_t>(i)] = "254";
    for (const auto& [c, v] : set) cols[static_cast<std::size_t>(c)] = v;
    std::string line;
    for (std::size_t i = 0; i < cols.size(); ++i) line += (i ? "^" : "") + cols[i];
    return line;
}

} // namespace

TEST_CASE("AllowedToAttack: below level 6 a detrimental spell is refused in a newbie zone (EQWorldData::IsNewbieZone)") {
    hailstone::game::SpellTable table;
    REQUIRE(table.parse(spell_line({{0, "300"}, {1, "Bolt"}, {83, "0"}})) == 1);
    hailstone::game::WorldState world;
    world.player_id = 1;
    for (std::uint32_t id : {1u, 2u}) {
        auto& s = world.spawns[id];
        s.id = id;
        s.race = 1;
        s.level = 3;
        s.pvp = 1;
    }
    hailstone::game::AttackCheckInput in;
    in.world = &world;
    in.spells = &table;
    in.target_id = 2;
    in.spell = 300;
    in.rules.pvp_rules = 6; // the level rules alone
    in.zone = 1;            // qeynos, a newbie zone
    CHECK_FALSE(hailstone::game::allowed_to_attack(in));
    in.zone = 1 | (5u << 16); // an instance of it: the low 15 bits name the zone
    CHECK_FALSE(hailstone::game::allowed_to_attack(in));
    in.zone = 0x3a; // crushbone
    CHECK(hailstone::game::allowed_to_attack(in));
    // At level 6 both, the zone does not matter.
    world.spawns[1].level = world.spawns[2].level = 6;
    in.zone = 1;
    CHECK(hailstone::game::allowed_to_attack(in));
}
