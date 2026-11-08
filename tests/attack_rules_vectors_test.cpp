// PlayerZoneClient_AllowedToAttack.jsonl (the recorded vectors, from
// eqgame.exe) against game/mechanics/attack_rules.h: the result and
// every callee asked, in order (the callees' answers are the record's).
#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>

#include "game/mechanics/attack_rules.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using hailstone::test::Json;

namespace {

class RecordedAttack final : public AttackQueries {
public:
    RecordedAttack(const Json& calls, std::array<AttackSpawn, 5>& spawns, std::array<AttackSpell, 3>& spells)
        : calls_(calls), spawns_(spawns), spells_(spells) {}
    std::string error;
    bool done() const { return at_ == calls_.items.size(); }

    const AttackSpawn* attacker_of(const AttackSpawn& s) override { return spawn_ret(next("attacker_of", &s)); }
    std::uint8_t level(const AttackSpawn& s) override { return static_cast<std::uint8_t>(ret(next("get_level", &s))); }
    std::uint32_t top_master_id(const AttackSpawn& s) override { return ret(next("get_top_master_id", &s)); }
    bool is_vehicle_race(const AttackSpawn& s) override { return ret(next("is_vehicle_race", &s)) != 0; }
    bool is_gm(const AttackSpawn& s) override { return ret(next("is_gm", &s)) != 0; }
    bool is_valid_spell_id(std::int32_t id) override { return ret(next_id("is_valid_spell_id", id)) != 0; }
    const AttackSpell* spell_by_id(std::int32_t id) override {
        const Json* c = next_id("spell_by_id", id);
        if (!c) return nullptr;
        std::int32_t k = (*c)["ret"].i32();
        return k < 0 ? nullptr : &spells_[static_cast<std::size_t>(k)];
    }
    bool spell_affects(const AttackSpell& s, std::int32_t attribute) override {
        const Json* c = next("spell_affects");
        if (!c) return false;
        if ((*c)["spell"].i32() != spell_index(&s) || (*c)["attribute"].i32() != attribute)
            error += std::format("spell_affects({}, {}) want ({}, {}); ", spell_index(&s), attribute,
                                 (*c)["spell"].i32(), (*c)["attribute"].i32());
        return ret(c) != 0;
    }
    const AttackSpawn* spawn_by_id(std::uint32_t id) override {
        return spawn_ret(next_id("get_spawn_by_id", static_cast<std::int32_t>(id)));
    }
    bool not_on_same_deity(const AttackSpawn& self, const AttackSpawn& other, const AttackSpell* spell) override {
        const Json* c = next("not_on_same_deity");
        if (!c) return false;
        if ((*c)["spawn"].i32() != index(&self) || (*c)["other"].i32() != index(&other) ||
            (*c)["spell"].i32() != (spell ? spell_index(spell) : -1))
            error += "not_on_same_deity arguments; ";
        return ret(c) != 0;
    }
    bool has_owner(const AttackSpawn& s) override { return ret(next("target_owner_nonnull", &s)) != 0; }
    bool is_corpse(const AttackSpawn& s) override { return ret(next("is_corpse", &s)) != 0; }
    bool is_self_or_group_member(const AttackSpawn& s) override {
        return ret(next("is_self_or_group_member", &s)) != 0;
    }
    bool is_newbie_zone(std::uint32_t zone) override {
        return ret(next_id("is_newbie_zone", static_cast<std::int32_t>(zone), "zone")) != 0;
    }

private:
    int index(const AttackSpawn* s) const {
        for (std::size_t k = 0; k < spawns_.size(); ++k)
            if (s == &spawns_[k]) return static_cast<int>(k);
        return -1;
    }
    int spell_index(const AttackSpell* s) const {
        for (std::size_t k = 0; k < spells_.size(); ++k)
            if (s == &spells_[k]) return static_cast<int>(k);
        return -1;
    }
    const Json* next(const char* fn, const AttackSpawn* spawn = nullptr) {
        if (at_ >= calls_.items.size() || calls_[at_]["fn"].text != fn) {
            error += std::format("call {}: {} expected, record has {}; ", at_, fn,
                                 at_ < calls_.items.size() ? calls_[at_]["fn"].text : "nothing");
            at_ = calls_.items.size() + 1;
            return nullptr;
        }
        const Json* c = &calls_[at_++];
        if (spawn && (*c)["spawn"].i32() != index(spawn))
            error += std::format("{}: spawn {} want {}; ", fn, index(spawn), (*c)["spawn"].i32());
        return c;
    }
    const Json* next_id(const char* fn, std::int32_t id, const char* key = "id") {
        const Json* c = next(fn);
        if (c && (*c)[key].i32() != id) error += std::format("{}: {} {} want {}; ", fn, key, id, (*c)[key].i32());
        return c;
    }
    static std::uint32_t ret(const Json* c) { return c ? (*c)["ret"].u32() : 0; }
    const AttackSpawn* spawn_ret(const Json* c) {
        if (!c) return &spawns_[0];
        std::int32_t k = (*c)["ret"].i32();
        return k < 0 ? nullptr : &spawns_[static_cast<std::size_t>(k)];
    }
    const Json& calls_;
    std::array<AttackSpawn, 5>& spawns_;
    std::array<AttackSpell, 3>& spells_;
    std::size_t at_ = 0;
};

std::string check_allowed(const Json& rec) {
    const Json& in = rec["in"];
    std::array<AttackSpawn, 5> spawns;
    std::array<AttackCharacter, 5> characters;
    std::array<AttackSpell, 3> spells;
    for (std::size_t k = 0; k < 5; ++k) {
        const Json& s = in["spawns"][k];
        AttackSpawn& d = spawns[k];
        d.type = static_cast<std::uint8_t>(s["type"].i32());
        d.id = s["id"].u32();
        d.master_id = s["master_id"].u32();
        d.body_wet = static_cast<std::uint8_t>(s["body_wet"].i32());
        d.in_pvp_area = s["in_pvp_area"].truthy();
        d.pvp = s["pvp"].truthy();
        d.guild_id = s["guild_id"].i32();
        d.zone = s["zone"].u32();
        d.level_cap = s["level_cap"].i32();
        d.deity = s["deity"].u32();
        if (s["has_character"].truthy()) {
            characters[k].level_capped = s["level_capped"].truthy();
            characters[k].spawn = &spawns[static_cast<std::size_t>(s["capped_character_spawn"].i32())];
            d.character = &characters[k];
        }
    }
    for (std::size_t k = 0; k < 3; ++k) {
        spells[k].target_type = in["spells"][k]["target_type"].i32();
        spells[k].beneficial = static_cast<std::uint8_t>(in["spells"][k]["beneficial"].i32());
    }
    AttackRules rules{in["pvp_rules"].i32(), in["dueling"].truthy(), in["duel_target_id"].u32()};
    RecordedAttack q(rec["out"]["calls"], spawns, spells);
    bool r = allowed_to_attack(spawns[0], in["target"].truthy() ? &spawns[2] : nullptr, in["spell_id"].i32(), rules, q);
    if (!q.error.empty()) return q.error;
    if (!q.done()) return "calls left over";
    if (r != rec["out"]["value"].truthy()) return std::format("value {}", r);
    return {};
}

} // namespace

TEST_CASE("vectors: PlayerZoneClient_AllowedToAttack (the PvP rules)") {
    hailstone::test::each_vector("PlayerZoneClient_AllowedToAttack.jsonl", check_allowed);
}

TEST_CASE("NotOnSameDeity: alignment groups") {
    AttackSpawn a, b;
    a.deity = 201; // group 0
    b.deity = 203; // group 0
    CHECK_FALSE(not_on_same_deity(&a, &b, true, false));
    b.deity = 202; // group 1
    CHECK(not_on_same_deity(&a, &b, true, false));
    CHECK_FALSE(not_on_same_deity(&a, &b, false, true)); // a GM target without a spell
    a.deity = 140; // agnostic: never
    CHECK_FALSE(not_on_same_deity(&a, &b, true, false));
}
