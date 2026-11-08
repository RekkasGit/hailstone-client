// The melee timing rules (game/mechanics/melee.h) against the original's
// vectors (recorded from eqgame.exe): MeleeRange (3000),
// ModifyAttackSpeed (4000, the bag search replayed from out.calls),
// GetHandToHandDelay (3000), CheckDoubleAttack (4000) and CheckDualWield
// (4000, Random() replayed from in.rand); DoAttack (1200, every
// callee replayed from out.calls). Then unit tests of DoAttack's timing and
// DoPassageOfTime's ready flags (no vectors: the disassembly's rules).
#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "game/mechanics/melee.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using hailstone::test::Json;
namespace ml = hailstone::game::mechanics::melee;

namespace {

Fpu fpu_of(const Json& r) {
    return Fpu(r["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53);
}

std::string hex(std::uint32_t v) { return std::format("{:#010x}", v); }

// ---------------------------------------------------------------- MeleeRange

std::string check_range(const Json& r) {
    const Json& in = r["in"];
    auto spawn = [](const Json& j) {
        ml::RangeSpawn s;
        s.melee_radius = hailstone::test::f32_of(j["melee_radius"]);
        s.scale = hailstone::test::f32_of(j["scale"]);
        s.height = hailstone::test::f32_of(j["height"]);
        return s;
    };
    ml::RangeSpawn a = spawn(in["a"]), b = spawn(in["b"]);
    const ml::RangeSpawn* pb = in["b_is_a"].truthy() ? &a : &b;
    float got = ml::melee_range(in["a_present"].truthy() ? &a : nullptr, in["b_present"].truthy() ? pb : nullptr,
                                fpu_of(r));
    std::uint32_t want = hailstone::test::f32_bits(r["out"]["range"]);
    std::uint32_t have = std::bit_cast<std::uint32_t>(got);
    // A NaN's payload as the x87 left it.
    if (have != want) return std::format("range {} want {}", hex(have), hex(want));
    return "";
}

// ---------------------------------------------------------------- ModifyAttackSpeed

class RecordedBags final : public ml::BagQueries {
public:
    explicit RecordedBags(const Json& calls) : calls_(calls) {}
    std::string error;
    bool done() const { return at_ == calls_.items.size(); }

    bool slot_available(std::int32_t slot) override {
        const Json* c = take("slot_available");
        if (c && (*c)["slot"].i32() != slot) error += std::format("slot_available({}) want {}; ", slot, (*c)["slot"].i32());
        return c && ((*c)["ret"].u32() & 0xff) != 0;
    }
    // unk_Container_GetItem is not a recorded call: an item was there when the original asked its type next.
    bool has_item(std::int32_t) override {
        return at_ < calls_.items.size() && calls_.items[at_]["fn"].text == "item_type";
    }
    std::int32_t item_class(std::int32_t) override { return ret(take("item_type")); }
    std::int32_t container_type(std::int32_t) override { return ret(take("container_type")); }
    std::int32_t weight_reduction(std::int32_t) override { return ret(take("weight_reduction")); }

private:
    const Json* take(const char* fn) {
        if (at_ >= calls_.items.size()) {
            error += std::format("extra call {}; ", fn);
            return nullptr;
        }
        const Json& c = calls_.items[at_++];
        if (c["fn"].text != fn) {
            error += std::format("call {} want {}; ", fn, c["fn"].text);
            return nullptr;
        }
        return &c;
    }
    static std::int32_t ret(const Json* c) { return c ? (*c)["ret"].i32() : 0; }
    const Json& calls_;
    std::size_t at_ = 0;
};

std::string check_attack_speed(const Json& r) {
    const Json& in = r["in"];
    RecordedBags bags(r["out"]["calls"]);
    // No "precision": the process's 53-bit default.
    std::uint32_t got =
        ml::modify_attack_speed(in["delay"].u32(), in["with_bags"].u32(), in["haste"].i32(), &bags, fpu_of(r));
    std::uint32_t want = r["out"]["delay"].u32();
    std::string why = bags.error;
    if (!bags.done()) why += "calls left over; ";
    if (got != want) why += std::format("delay {} want {}", got, want);
    return why;
}

// ---------------------------------------------------------------- GetHandToHandDelay

std::string check_hand_to_hand(const Json& r) {
    const Json& in = r["in"];
    ml::HandToHandInput h;
    h.has_character = in["has_character"].truthy();
    h.skill = in["skill"].i32();
    h.hands_item = in["hands_item_present"].truthy();
    h.hands_item_id = in["hands_item_id"].i32();
    h.level = in["level"].i32();
    h.profile_3370 = in["profile_haste_flag"].i32();
    std::int32_t got = ml::hand_to_hand_delay(h).value_or(static_cast<std::int32_t>(0xdeadbeef)); // the sentinel
    std::int32_t want = r["out"]["delay"].i32();
    return got == want ? "" : std::format("delay {} want {}", got, want);
}

// ---------------------------------------------------------------- the rolls

class RecordedRolls final : public ml::RollQueries {
public:
    explicit RecordedRolls(const Json& in) : in_(in) {}
    std::int32_t adjusted_skill(std::int32_t) override { return in_["adjusted_skill"].i32(); }
    std::int32_t skill(std::int32_t) override { return in_["skill"].i32(); }
    std::int32_t level() override { return in_["level"].i32(); }
    std::int32_t total_effect(std::int32_t spa) override {
        auto key = std::format("spa{}", spa);
        return in_[key].i32();
    }
    std::int32_t random() override { return in_["rand"].i32(); }

private:
    const Json& in_;
};

std::string check_double_attack(const Json& r) {
    const Json& in = r["in"];
    RecordedRolls q(in);
    ml::DoubleAttackInput d;
    d.class_id = static_cast<std::uint8_t>(in["class"].i32());
    d.has_spawn = in["has_spawn"].truthy();
    d.do_special_melee = in["do_special_melee"].i32();
    bool got = ml::check_double_attack(d, q);
    bool want = r["out"]["value"].truthy();
    return got == want ? "" : std::format("value {} want {}", got, want);
}

std::string check_dual_wield(const Json& r) {
    const Json& in = r["in"];
    RecordedRolls q(in);
    ml::DualWieldInput d;
    d.primary = in["primary_present"].truthy();
    d.primary_class = in["primary_get_type"].i32();
    d.primary_type = in["primary_item_type"].i32();
    bool got = ml::check_dual_wield(d, q);
    bool want = r["out"]["value"].truthy();
    return got == want ? "" : std::format("value {} want {}", got, want);
}

// ---------------------------------------------------------------- DoAttack (call by call)

// The original's callees replayed from out.calls: each
// query takes the next record, checks its name and arguments and returns its result. Items are the vectors' numbers
// (0..11 the world's, -2 the fist, -1 none), which the client passes around as ItemRefs.
class RecordedAttack final : public ml::AttackQueries {
public:
    RecordedAttack(const Json& in, const Json& calls, const Fpu& f) : in_(in), calls_(calls), f_(f) {}
    std::string error;
    bool done() const { return at_ == calls_.items.size(); }

    bool has_character() override { return in_["player"]["has_character"].truthy(); }
    std::int32_t total_effect(std::int32_t spa) override {
        const Json* c = take("total_effect");
        arg(c, "spa", spa);
        return i32(c);
    }
    void chat(std::uint32_t eqstr, std::int32_t color) override {
        arg(take("get_string"), "id", eqstr);
        const Json* c = take("dsp_chat");
        arg(c, "id", eqstr);
        arg(c, "color", color);
    }
    void token(std::uint32_t id, std::int32_t color) override {
        const Json* c = take("token_string_to_client");
        arg(c, "id", id);
        arg(c, "color", color);
    }
    bool allowed_to_attack(const ml::Spawn&) override { return bad("AllowedToAttack"); }
    bool can_see(const ml::Spawn&) override { return bad("CanSee"); }
    std::uint32_t tick_count() override { return u32(take("get_tick_count")); }
    std::uint32_t now() override { return u32(take("eq_get_time")); }
    bool attack_blocked() override { return in_["attack_blocked"].truthy(); }
    std::uint8_t is_player_spawn() override { return u8(take("is_player_spawn")); }
    std::uint8_t can_melee_attack_now(const ml::Spawn* target) override {
        const Json* c = take("can_melee_attack_now");
        arg(c, "target", target != nullptr);
        return u8(c);
    }
    std::uint32_t spell_affect(std::int32_t spa) override {
        const Json* c = take("get_pc_spell_affect");
        arg(c, "spa", spa);
        return c && (*c)["ret"].truthy() ? 0x6100u : 0u;
    }
    void remove_affect(std::uint32_t) override { take("remove_pc_affect"); }
    void vf_c8() override { take("player_vf_c8"); }
    std::uint8_t can_use_combat_skill(std::int32_t slot, std::int32_t skill) override {
        const Json* c = take("can_use_combat_skill");
        arg(c, "slot", slot);
        arg(c, "skill", static_cast<std::int32_t>(skill));
        return u8(c);
    }
    ml::ItemRef item(std::int32_t slot) override {
        const Json* c = take("get_item");
        arg(c, "slot", slot);
        return i32(c);
    }
    ml::ItemRef weapon_ref_item(std::int32_t slot, bool fist, bool wrapper) override {
        const Json* c = take(wrapper ? "weapon_ref_get_item_wrapper" : "weapon_ref_get_item");
        arg(c, "slot", slot);
        arg(c, "fist", fist);
        return i32(c);
    }
    std::int32_t id_file(ml::ItemRef item) override {
        const Json* c = take("get_id_file");
        arg(c, "item", item);
        arg(c, "kind", 0x22);
        return i32(c);
    }
    void set_held_model(std::int32_t slot, std::int32_t model) override {
        take("model_owner");
        const Json* c = take("set_held_model");
        arg(c, "slot", slot);
        arg(c, "model_item", model);
        arg(c, "a2", 0);
        arg(c, "a3", 1);
    }
    bool auto_fire() override { return in_["autofire"].truthy(); }
    void set_auto_range_attack(bool on) override { arg(take("set_auto_range_attack"), "on", on); }
    std::int32_t base_damage(std::uint32_t skill, const ml::Spawn* target) override {
        const Json* c = take("get_skill_base_damage");
        arg(c, "skill", static_cast<std::int32_t>(skill));
        arg(c, "target", target != nullptr);
        return i32(c);
    }
    void fist_set_definition() override { take("fist_set_definition"); }
    std::int32_t item_type(ml::ItemRef item) override { return item_call("get_item_type", item); }
    std::uint8_t is_weapon(ml::ItemRef item) override {
        return static_cast<std::uint8_t>(item_call("is_weapon", item));
    }
    std::uint32_t item_delay(ml::ItemRef item) override {
        return static_cast<std::uint32_t>(item_call("get_item_skill", item)); // ItemBase_GetDelay
    }
    std::int32_t item_stack(ml::ItemRef) override { return 0; } // DoRangedAttack is a recorded call here
    std::int32_t item_id(ml::ItemRef) override { return 0; }
    std::int32_t item_range(ml::ItemRef item) override {
        const Json* c = take("get_range");
        arg(c, "item", item);
        arg(c, "a0", 1);
        return i32(c);
    }
    std::uint8_t can_use_item(ml::ItemRef item) override {
        const Json* c = take("can_use_item");
        arg(c, "item", item);
        arg(c, "a1", 1);
        arg(c, "a2", 0);
        return u8(c);
    }
    ml::AttackProfile profile() override {
        take("get_current_profile");
        const Json& p = in_["profile"];
        return {p["fist_damage"].i32(), p["fist_delay"].i32(), p["double_attack_level"].i32()};
    }
    std::int32_t reuse_time(std::uint32_t skill) override {
        const Json* c = take("skill_mgr_reuse_time");
        arg(c, "skill", static_cast<std::int32_t>(skill));
        return i32(c);
    }
    void hand_to_hand_delay(std::int32_t& delay, ml::ItemRef hands) override {
        const Json* c = take("get_hand_to_hand_delay");
        arg(c, "item", hands);
        if (!c) return;
        if ((*c)["writes"].truthy()) delay = i32(c);
        else arg(c, "ret", delay); // unwritten: the value it was handed
    }
    std::uint32_t modify_attack_speed(std::uint32_t delay, bool archery) override {
        const Json* c = take("modify_attack_speed");
        arg(c, "delay", static_cast<std::int32_t>(delay));
        arg(c, "archery", archery);
        return u32(c);
    }
    std::uint32_t combat_timer_index(std::uint32_t skill) override {
        const Json* c = take("combat_skill_timer_index");
        arg(c, "skill", static_cast<std::int32_t>(skill));
        return u32(c);
    }
    std::uint32_t skill_reuse_time(std::uint32_t skill) override {
        const Json* c = take("get_skill_reuse_time");
        arg(c, "skill", static_cast<std::int32_t>(skill));
        return u32(c);
    }
    void set_skill_timer_duration(std::uint32_t skill, std::uint32_t ms) override {
        const Json* c = take("set_skill_timer_duration");
        arg(c, "skill", static_cast<std::int32_t>(skill));
        arg(c, "duration", static_cast<std::int32_t>(ms));
    }
    void chat2(std::uint32_t eqstr) override {
        arg(take("get_string"), "id", eqstr);
        arg(take("dsp_chat_2"), "id", eqstr);
    }
    // The fake PlayerDistance: distance + distance_extra (at the precision), unrounded.
    double player_distance(const ml::Spawn* target) override {
        const Json* c = take("player_distance");
        arg(c, "z_scale_bits", 0);
        if (!target) error += "player_distance without a target; ";
        return f_.add(static_cast<double>(hailstone::test::f32_of(in_["distance"])),
                      static_cast<double>(hailstone::test::f32_of(in_["distance_extra"])));
    }
    bool hit_anyway() override { return in_["hit_anyway"].truthy(); }
    std::uint8_t can_i_hit(const ml::Spawn* target, float range) override {
        const Json* c = take("can_i_hit");
        arg(c, "target", target != nullptr);
        arg(c, "range_bits", static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(range)));
        return u8(c);
    }
    void debug_cant_hit() override { take("debug_cant_hit"); }
    void send_attack(const ml::Spawn* target, ml::ItemRef item, std::int32_t slot, std::uint32_t skill) override {
        const Json* c = take("send_combat_ability_attack");
        arg(c, "target", target != nullptr);
        arg(c, "item", item);
        arg(c, "slot", slot);
        arg(c, "skill", static_cast<std::int32_t>(skill));
    }
    std::uint8_t check_double_attack(const ml::Spawn* target, std::int32_t slot) override {
        const Json* c = take("check_double_attack");
        arg(c, "target", target != nullptr);
        arg(c, "slot", slot);
        return u8(c);
    }
    std::uint8_t check_dual_wield(const ml::Spawn* target) override {
        const Json* c = take("check_dual_wield");
        arg(c, "target", target != nullptr);
        return u8(c);
    }
    void ranged_attack(const ml::Spawn* target) override { arg(take("do_ranged_attack"), "target", target != nullptr); }

private:
    bool bad(const char* what) {
        error += std::format("{} called; ", what);
        return false;
    }
    const Json* take(const char* fn) {
        if (at_ >= calls_.items.size()) {
            error += std::format("extra call {}; ", fn);
            return nullptr;
        }
        const Json& c = calls_.items[at_];
        if (c["fn"].text != fn) {
            error += std::format("call {} #{} want {}; ", fn, at_, c["fn"].text);
            at_ = calls_.items.size(); // the rest would only repeat the divergence
            return nullptr;
        }
        ++at_;
        return &c;
    }
    void arg(const Json* c, const char* name, std::int64_t want) {
        if (c && (*c)[name].i64() != want)
            error += std::format("{} #{} {} {} want {}; ", (*c)["fn"].text, at_ - 1, name, want, (*c)[name].i64());
    }
    std::int32_t item_call(const char* fn, ml::ItemRef item) {
        const Json* c = take(fn);
        arg(c, "item", item);
        return i32(c);
    }
    static std::int32_t i32(const Json* c) { return c ? (*c)["ret"].i32() : 0; }
    static std::uint32_t u32(const Json* c) { return c ? (*c)["ret"].u32() : 0; }
    static std::uint8_t u8(const Json* c) { return static_cast<std::uint8_t>(u32(c)); }
    const Json& in_;
    const Json& calls_;
    const Fpu& f_;
    std::size_t at_ = 0;
};

std::string check_do_attack(const Json& r) {
    const Json& in = r["in"];
    const Json& out = r["out"];
    const Fpu f = fpu_of(r);
    const Json& p = in["player"];
    ml::AttackState s;
    s.last_intimidate = p["skill71_time"].u32();
    s.last_secondary = p["secondary_timer"].u32();
    s.last_primary = p["primary_timer"].u32();
    s.last_attack = static_cast<std::uint8_t>(p["last_slot"].u32());
    s.alt_attack = p["ability_pending"].u32();
    for (std::size_t k = 0; k < 4; ++k) {
        s.combat_ticks[k] = p["skill_delays"][k].u32();
        s.combat_used[k] = p["skill_timers"][k].u32();
    }
    const Json& fb = in["fist_before"];
    s.fist.present = in["fist_definition"].truthy();
    s.fist.damage = fb["damage"].i32();
    s.fist.damage_copy = fb["damage_copy"].i32();
    s.fist.delay = static_cast<std::uint8_t>(fb["skill"].u32());
    s.fist.item_type = static_cast<std::uint8_t>(fb["item_type"].u32());
    ml::Spawn me;
    me.type = static_cast<std::uint8_t>(p["spawn_type"].u32());
    me.stand_state = static_cast<std::uint8_t>(p["stance"].u32());
    me.is_attacking = p["pet_state"].i32();
    me.do_special_melee = p["always_swing"].i32();
    me.body_wet = static_cast<std::uint8_t>(p["team"].u32());
    ml::Spawn target;
    target.body_wet = static_cast<std::uint8_t>(in["target_fields"]["team"].u32());
    target.always_hit = static_cast<std::uint8_t>(in["target_fields"]["flag_251"].u32());

    RecordedAttack q(in, out["calls"], f);
    bool got = ml::do_attack(s, me, in["target"].truthy() ? &target : nullptr, in["slot"].u32(), in["skill"].u32(), q, f);
    std::string why = q.error;
    if (!q.done() && why.empty()) why += "calls left over; ";
    auto cmp = [&](const char* what, std::int64_t have, std::int64_t want) {
        if (have != want) why += std::format("{} {} want {}; ", what, have, want);
    };
    cmp("value", got, out["value"].truthy());
    const Json& o = out["player"];
    cmp("skill71_time", static_cast<std::int32_t>(s.last_intimidate), o["skill71_time"].i32());
    cmp("pet_state", me.is_attacking, o["pet_state"].i32());
    cmp("secondary_timer", static_cast<std::int32_t>(s.last_secondary), o["secondary_timer"].i32());
    cmp("primary_timer", static_cast<std::int32_t>(s.last_primary), o["primary_timer"].i32());
    cmp("last_slot", s.last_attack, o["last_slot"].i32());
    cmp("ability_pending", static_cast<std::int32_t>(s.alt_attack), o["ability_pending"].i32());
    for (std::size_t k = 0; k < 4; ++k) {
        cmp("skill_delays", static_cast<std::int32_t>(s.combat_ticks[k]), o["skill_delays"][k].i32());
        cmp("skill_timers", static_cast<std::int32_t>(s.combat_used[k]), o["skill_timers"][k].i32());
    }
    const Json& fo = out["fist"];
    cmp("fist damage", s.fist.damage, fo["damage"].i32());
    cmp("fist damage_copy", s.fist.damage_copy, fo["damage_copy"].i32());
    cmp("fist skill", s.fist.delay, fo["skill"].i32());
    cmp("fist item_type", s.fist.item_type, fo["item_type"].i32());
    return why;
}


} // namespace

TEST_CASE("MeleeRange vectors (__get_melee_range)") { hailstone::test::each_vector("MeleeRange.jsonl", check_range); }
TEST_CASE("ModifyAttackSpeed vectors") { hailstone::test::each_vector("ModifyAttackSpeed.jsonl", check_attack_speed); }
TEST_CASE("GetHandToHandDelay vectors") { hailstone::test::each_vector("GetHandToHandDelay.jsonl", check_hand_to_hand); }
TEST_CASE("CheckDoubleAttack vectors") { hailstone::test::each_vector("CheckDoubleAttack.jsonl", check_double_attack); }
TEST_CASE("CheckDualWield vectors") { hailstone::test::each_vector("CheckDualWield.jsonl", check_dual_wield); }
TEST_CASE("DoAttack vectors (call by call)") { hailstone::test::each_vector("DoAttack.jsonl", check_do_attack); }

// ---------------------------------------------------------------- DoAttack and the swing timers (unit tests)

namespace {

struct FakeItem {
    std::uint8_t delay = 0;
    std::int32_t item_type = 0;
    std::int32_t damage = 0;
    std::int32_t stack = 1, id = 0, range = 0;
};

// A player with a 1H slasher of delay 30 in the primary and nothing in the secondary; no haste. Item references
// are the slots.
class FakeAttack final : public ml::AttackQueries {
public:
    std::uint32_t t = 0;
    std::map<std::int32_t, FakeItem> items;
    ml::AttackState* state = nullptr; // the fist's definition
    const ml::Spawn* me = nullptr;
    bool dual_wield = false;
    bool either_hand = false; // backstab_either_hand
    std::vector<std::int32_t> swings;
    std::vector<std::uint32_t> messages;

    bool has_character() override { return true; }
    std::int32_t total_effect(std::int32_t) override { return 0; }
    void chat(std::uint32_t eqstr, std::int32_t) override { messages.push_back(eqstr); }
    void token(std::uint32_t id, std::int32_t) override { messages.push_back(id); }
    bool allowed_to_attack(const ml::Spawn&) override { return true; }
    bool can_see(const ml::Spawn&) override { return true; }
    std::uint32_t tick_count() override { return t; }
    std::uint32_t now() override { return t; }
    bool attack_blocked() override { return false; }
    std::uint8_t is_player_spawn() override { return 1; }
    std::uint8_t can_melee_attack_now(const ml::Spawn*) override { return 1; }
    std::uint32_t spell_affect(std::int32_t) override { return 0; }
    void remove_affect(std::uint32_t) override {}
    void vf_c8() override {}
    std::uint8_t can_use_combat_skill(std::int32_t, std::int32_t) override { return 1; }
    ml::ItemRef item(std::int32_t slot) override { return items.count(slot) ? slot : ml::kNoItem; }
    ml::ItemRef weapon_ref_item(std::int32_t slot, bool fist, bool) override { return fist ? ml::kFistItem : item(slot); }
    std::int32_t id_file(ml::ItemRef item) override { return item; }
    void set_held_model(std::int32_t, std::int32_t) override {}
    bool auto_fire() override { return false; }
    void set_auto_range_attack(bool) override {}
    std::int32_t base_damage(std::uint32_t, const ml::Spawn*) override { return 3; }
    void fist_set_definition() override {}
    std::int32_t item_type(ml::ItemRef item) override {
        return item == ml::kFistItem ? state->fist.item_type : item == ml::kNoItem ? 0 : items[item].item_type;
    }
    std::uint8_t is_weapon(ml::ItemRef item) override {
        std::int32_t damage = item == ml::kFistItem ? state->fist.damage : item == ml::kNoItem ? 0 : items[item].damage;
        return damage > 0 ? 1 : 0;
    }
    std::uint32_t item_delay(ml::ItemRef item) override {
        return item == ml::kFistItem ? state->fist.delay : item == ml::kNoItem ? 0 : items[item].delay;
    }
    std::int32_t item_range(ml::ItemRef item) override { return item >= 0 ? items[item].range : 0; }
    std::int32_t item_stack(ml::ItemRef item) override { return item >= 0 ? items[item].stack : 0; }
    std::int32_t item_id(ml::ItemRef item) override { return item >= 0 ? items[item].id : 0; }
    std::uint8_t can_use_item(ml::ItemRef) override { return 1; }
    ml::AttackProfile profile() override { return {}; }
    std::int32_t reuse_time(std::uint32_t skill) override { return skill == 0x1c ? 3500 : 0; }
    void hand_to_hand_delay(std::int32_t& delay, ml::ItemRef) override { delay = 3800; }
    std::uint32_t modify_attack_speed(std::uint32_t delay, bool) override { return delay; }
    std::uint32_t combat_timer_index(std::uint32_t skill) override { return skill == 52 ? 1 : 0; }
    std::uint32_t skill_reuse_time(std::uint32_t) override { return 6000; }
    void set_skill_timer_duration(std::uint32_t, std::uint32_t) override {}
    void chat2(std::uint32_t eqstr) override { messages.push_back(eqstr); }
    double player_distance(const ml::Spawn*) override { return 0.0; }
    bool hit_anyway() override { return false; }
    std::uint8_t can_i_hit(const ml::Spawn* target, float range) override {
        return ml::can_i_hit(*me, target, range, *this, Fpu{}) ? 1 : 0;
    }
    void debug_cant_hit() override {}
    void send_attack(const ml::Spawn*, ml::ItemRef item, std::int32_t slot, std::uint32_t) override {
        swings.push_back(slot);
        sent_items.push_back(item);
    }
    std::vector<ml::ItemRef> sent_items;
    std::uint8_t check_double_attack(const ml::Spawn*, std::int32_t) override { return 0; }
    std::uint8_t check_dual_wield(const ml::Spawn*) override { return dual_wield ? 1 : 0; }
    void ranged_attack(const ml::Spawn*) override {}
    bool backstab_either_hand() override { return either_hand; }
};

FakeItem sword(std::uint8_t delay) { return {delay, 0, 8}; } // 1H slashing
FakeItem dagger() { return {20, 2, 6}; }                     // 1H piercing

ml::Spawn spawn_at(std::uint8_t type, float y, float x) {
    ml::Spawn s;
    s.type = type;
    s.y = y;
    s.x = x;
    s.height = 6.0f;
    s.melee_radius = 5.0f;
    return s;
}

} // namespace

TEST_CASE("DoAttack: the first swing waits a whole delay, then keeps the cadence") {
    FakeAttack q;
    q.items[0xd] = sword(30);
    ml::AttackState s;
    ml::Spawn me = spawn_at(0, 0, 0);    // heading 0: facing +y
    ml::Spawn rat = spawn_at(1, 5, 0);
    q.state = &s;
    q.me = &me;
    const Fpu f;
    // Attack off for a while: the primary flag is up.
    q.t = 10000;
    ml::swing_ready(s, q.t, q);
    CHECK(s.primary_ready);
    // Attack on: DoPassageOfTime clears the flags and restarts the timers.
    ml::attack_on_reset(s, true, q);
    CHECK_FALSE(s.primary_ready);
    CHECK(s.last_primary == 10000);
    ml::swing_ready(s, q.t, q);
    CHECK_FALSE(s.primary_ready);
    q.t = 12999;
    ml::swing_ready(s, q.t, q);
    CHECK_FALSE(s.primary_ready);
    q.t = 13000;
    ml::swing_ready(s, q.t, q);
    REQUIRE(s.primary_ready);
    s.primary_ready = false; // ProcessControls
    CHECK(ml::do_attack(s, me, &rat, 0xd, 0, q, f));
    CHECK(q.swings == std::vector<std::int32_t>{0xd});
    CHECK(s.last_primary == 13000);
    // Too early: no swing, the timer stays (DoAttack still returns 1).
    q.t = 13500;
    CHECK(ml::do_attack(s, me, &rat, 0xd, 0, q, f));
    CHECK(q.swings.size() == 1);
    CHECK(s.last_primary == 13000);
    // A frame late: the cadence keeps last + delay.
    q.t = 16016;
    CHECK(ml::do_attack(s, me, &rat, 0xd, 0, q, f));
    CHECK(s.last_primary == 16000);
    // Two delays late: it restarts at now.
    q.t = 22500;
    CHECK(ml::do_attack(s, me, &rat, 0xd, 0, q, f));
    CHECK(s.last_primary == 22500);
    CHECK(q.swings.size() == 3);
    CHECK(q.messages.empty());
}

TEST_CASE("DoAttack: range, facing and stand refusals print the original's messages") {
    FakeAttack q;
    q.items[0xd] = sword(30);
    ml::AttackState s;
    ml::Spawn me = spawn_at(0, 0, 0);
    q.state = &s;
    q.me = &me;
    const Fpu f;
    q.t = 5000;
    ml::Spawn far = spawn_at(1, 40, 0); // range max(14, 7.5) + 2 = 16
    CHECK_FALSE(ml::do_attack(s, me, &far, 0xd, 0, q, f));
    CHECK(q.messages == std::vector<std::uint32_t>{124});
    CHECK(s.last_primary == 3000); // the swing was taken (last 0 + delay: within two delays)
    q.t = 10000;
    ml::Spawn behind = spawn_at(1, -5, 0);
    CHECK_FALSE(ml::do_attack(s, me, &behind, 0xd, 0, q, f));
    CHECK(q.messages.back() == 108);
    q.t = 15000;
    ml::Spawn rat = spawn_at(1, 5, 0);
    me.stand_state = 110;
    CHECK_FALSE(ml::do_attack(s, me, &rat, 0xd, 0, q, f));
    CHECK(q.messages.back() == 12369);
    CHECK(q.swings.empty());
}

TEST_CASE("DoAttack: Backstab needs a piercer in the primary, or either hand with the server switch") {
    const Fpu f;
    ml::Spawn rat = spawn_at(1, 5, 0);
    auto backstab = [&](FakeAttack& q, std::uint8_t type) {
        ml::AttackState s;
        ml::Spawn me = spawn_at(type, 0, 0);
        q.state = &s;
        q.me = &me;
        q.t = 5000;
        return ml::do_attack(s, me, &rat, 100, 8, q, f);
    };
    SUBCASE("stock: a piercer in the secondary alone is refused with 12874") {
        FakeAttack q;
        q.items[0xd] = sword(30);
        q.items[0xe] = dagger();
        CHECK_FALSE(backstab(q, 0));
        CHECK(q.messages == std::vector<std::uint32_t>{12874});
        CHECK(q.swings.empty());
    }
    SUBCASE("stock: a piercer in the primary is sent") {
        FakeAttack q;
        q.items[0xd] = dagger();
        CHECK(backstab(q, 0));
        CHECK(q.swings == std::vector<std::int32_t>{100});
    }
    SUBCASE("stock: an NPC is not checked") {
        FakeAttack q;
        CHECK(backstab(q, 1));
        CHECK(q.swings == std::vector<std::int32_t>{100});
    }
    SUBCASE("switch on: a piercer in the secondary will do, with or without a primary") {
        FakeAttack q;
        q.either_hand = true;
        q.items[0xd] = sword(30);
        q.items[0xe] = dagger();
        CHECK(backstab(q, 0));
        CHECK(q.messages.empty());
        CHECK(q.swings == std::vector<std::int32_t>{100});
        FakeAttack bare;
        bare.either_hand = true;
        bare.items[0xe] = dagger();
        CHECK(backstab(bare, 0));
    }
    SUBCASE("switch on: no piercer in either hand is still refused") {
        FakeAttack q;
        q.either_hand = true;
        q.items[0xd] = sword(30);
        q.items[0xe] = sword(30);
        CHECK_FALSE(backstab(q, 0));
        CHECK(q.messages == std::vector<std::uint32_t>{12874});
    }
}

TEST_CASE("Swing timers: the fist, the secondary slot and the ranged delay after a ranged attack") {
    FakeAttack q;
    ml::AttackState s;
    q.state = &s;
    // Empty hands: the fist's GetReuseTime(28) for both slots.
    q.t = 3499;
    ml::swing_ready(s, q.t, q);
    CHECK_FALSE(s.primary_ready);
    CHECK_FALSE(s.secondary_ready);
    q.t = 3500;
    ml::swing_ready(s, q.t, q);
    CHECK(s.primary_ready);
    CHECK(s.secondary_ready);
    // DoAttack's hand-to-hand delay is GetHandToHandDelay's (3800): too early at 3500.
    ml::Spawn me = spawn_at(0, 0, 0), rat = spawn_at(1, 5, 0);
    q.me = &me;
    CHECK(ml::do_attack(s, me, &rat, 0xd, 0, q, Fpu{}));
    CHECK(q.swings.empty());
    q.t = 3800;
    CHECK(ml::do_attack(s, me, &rat, 0xd, 0, q, Fpu{}));
    CHECK(q.swings == std::vector<std::int32_t>{0xd});
    // The secondary swings only on a dual-wield roll.
    q.dual_wield = true;
    CHECK(ml::do_attack(s, me, &rat, 0xe, 0, q, Fpu{}));
    CHECK(q.swings == std::vector<std::int32_t>{0xd, 0xe});
    // After a ranged attack the primary waits for the bow's delay.
    ml::AttackState r;
    q.state = &r;
    q.items[0xb] = {50, 5, 10}; // a bow of delay 50
    r.last_attack = 0xb;
    q.t = 4000;
    ml::swing_ready(r, q.t, q);
    CHECK_FALSE(r.primary_ready);
    q.t = 5000;
    ml::swing_ready(r, q.t, q);
    CHECK(r.primary_ready);
}

TEST_CASE("HandleAmmo and DoRangedAttack") {
    FakeAttack q;
    ml::AttackState s;
    q.state = &s;
    ml::Spawn me = spawn_at(0, 0, 0);
    ml::Spawn rat = spawn_at(1, 50, 0);
    q.me = &me;
    // No range item: nothing.
    CHECK_FALSE(ml::do_ranged_attack(me, &rat, q));
    // A bow (5) with arrows (0x1b) stacked: 1, the arrow is shot.
    q.items[0xb] = {30, 5, 5, 1, 100, 200};
    q.items[0x16] = {0, 0x1b, 5, 20, 101, 30};
    CHECK(ml::handle_ammo(me, q) == 1);
    CHECK(ml::do_ranged_attack(me, &rat, q));
    REQUIRE(q.swings.size() == 1);
    CHECK(q.swings[0] == 0xb);
    CHECK(q.sent_items[0] == 0x16);
    // Without a target: false after the ammo check.
    CHECK_FALSE(ml::do_ranged_attack(me, nullptr, q));
    // Out of arrows: "You have run out of ammo!!" (0x324b) and nothing sent.
    q.items[0x16].stack = 0;
    q.messages.clear();
    CHECK(ml::handle_ammo(me, q) == 2);
    CHECK(q.messages == std::vector<std::uint32_t>{0x324b});
    q.items.erase(0x16);
    CHECK(ml::handle_ammo(me, q) == 2);
    // A thrown stack (0x13): above 1 it is thrown as type 0x13 (2), a single one as the range item (0).
    q.items[0xb] = {30, 0x13, 5, 5, 300, 50};
    CHECK(ml::handle_ammo(me, q) == 2);
    q.swings.clear();
    q.sent_items.clear();
    CHECK(ml::do_ranged_attack(me, &rat, q));
    CHECK(q.sent_items == std::vector<ml::ItemRef>{0xb});
    q.items[0xb].stack = 1;
    CHECK(ml::handle_ammo(me, q) == 0);
    // Another range item: 0 with a range, else 2.
    q.items[0xb] = {30, 0x14, 5, 1, 400, 100};
    CHECK(ml::handle_ammo(me, q) == 0);
    q.items[0xb].range = 0;
    CHECK(ml::handle_ammo(me, q) == 2);
}
