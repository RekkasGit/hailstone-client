// Test vectors recorded from the original eqgame.exe: each <Function>.jsonl
// record holds a
// function's inputs, the values its callees returned (mocks in the original's
// run: in.calls / in.virtual_calls / in.lookups) and its outputs. Each record
// becomes a CharacterSheet plus Character::hooks that replay those callee
// values, and the port's output must equal the original's exactly, under the
// record's x87 precision ("x87-24" or "x87-53").
//
// The directory is HAILSTONE_VECTORS_DIR (tests/vector_files.h); skipped
// without it.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

#include "core/env.h"
#include "game/mechanics/character.h"
#include "game/mechanics/item_sums.h"
#include "game/mechanics/ticks.h"
#include "game/spells.h"
#include "json_lite.h"
#include "vector_files.h"

using namespace hailstone;
using namespace hailstone::game;
using namespace hailstone::game::mechanics;
using hailstone::test::Json;
namespace fs = std::filesystem;

namespace {

// The recorded callee results: a call takes the first unused record of that name whose recorded arguments match
// (then, for a repeated call, a used one).
class Tape {
public:
    void add(const Json& list) {
        for (const Json& c : list.items) entries_.push_back({&c, false});
    }
    const Json* take(const std::string& fn, std::initializer_list<std::pair<const char*, std::int64_t>> args) {
        const Json* fallback = nullptr;
        for (auto& e : entries_) {
            if ((*e.call)["fn"].text != fn) continue;
            bool match = true;
            for (const auto& [k, v] : args)
                if (e.call->has(k) && (*e.call)[k].i64() != v && (*e.call)[k].i32() != static_cast<std::int32_t>(v))
                    match = false;
            if (!match) continue;
            if (!e.used) {
                e.used = true;
                return e.call;
            }
            if (!fallback) fallback = e.call;
        }
        if (!fallback) ++misses;
        return fallback;
    }
    int misses = 0;

private:
    struct Entry {
        const Json* call;
        bool used;
    };
    std::vector<Entry> entries_;
};

std::optional<std::int32_t> ret(const Json* c) {
    if (!c) return std::nullopt;
    if (c->has("ret")) return (*c)["ret"].i32();
    if (c->has("ret_nonnull")) return (*c)["ret_nonnull"].truthy() ? 1 : 0;
    return std::nullopt;
}

struct Record {
    CharacterSheet sheet;
    GameTables tables;
    std::map<std::int32_t, SpellData> spells;
    Tape calls, virtual_calls;
    SpellLookup lookup() {
        return [this](std::int32_t id) -> const SpellData* {
            auto it = spells.find(id);
            return it == spells.end() ? nullptr : &it->second;
        };
    }
};

FpuPrecision precision(const Json& j) {
    return j["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53;
}

void spawn_fields(CharacterSheet& s, const Json& spawn) {
    s.has_spawn = !spawn.is_null();
    if (!s.has_spawn) return;
    s.spawn_type = static_cast<SpawnType>(spawn["type"].i32() & 0xff);
    if (spawn.has("master_id")) s.is_pet = spawn["master_id"].i32() != 0;
    if (spawn.has("mercenary")) s.is_mercenary = spawn["mercenary"].truthy();
    if (spawn.has("summoned")) s.summoned = spawn["summoned"].truthy();
}

// The usual callee hooks of a record's "calls".
void common_hooks(Character& ch, Record& r) {
    Tape* t = &r.calls;
    ch.hooks.total_effect = [t](std::int32_t spa, bool items, std::int32_t sub, bool aa, bool buffs) {
        return ret(t->take("total_effect", {{"spa", spa}, {"include_items", items}, {"subindex", sub},
                                            {"include_aa", aa}, {"include_buffs", buffs}}));
    };
    ch.hooks.cur_hp = [t](int arg, bool update) {
        return ret(t->take("cur_hp", {{"unknown_arg", arg}, {"update", update}}));
    };
    ch.hooks.max_hp = [t](int arg, bool buffs) {
        return ret(t->take("max_hp", {{"unknown_arg", arg}, {"include_buffs", buffs}}));
    };
}

void stat_block(StatBlock& b, const Json& j) {
    b.str = j["str"].i32();
    b.sta = j["sta"].i32();
    b.cha = j["cha"].i32();
    b.dex = j["dex"].i32();
    b.int_ = j["int"].i32();
    b.agi = j["agi"].i32();
    b.wis = j["wis"].i32();
}

void heroic(DerivedStats& d, const Json& h) {
    d.heroic_str = h["str"].i32();
    d.heroic_int = h["int"].i32();
    d.heroic_wis = h["wis"].i32();
    d.heroic_agi = h["agi"].i32();
    d.heroic_dex = h["dex"].i32();
    d.heroic_sta = h["sta"].i32();
    d.heroic_cha = h["cha"].i32();
}

struct Result {
    std::vector<std::pair<std::string, std::string>> mismatches; // field, "got X want Y"
    void check(const std::string& field, std::int64_t got, std::int64_t want) {
        if (got != want) mismatches.push_back({field, std::to_string(got) + " != " + std::to_string(want)});
    }
};

using Checker = std::function<void(const Json& rec, Record& r, Result& res)>;

// ---- the functions ----

void vitals(const Json& rec, Record& r, Result& res, int kind) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    s.fpu = precision(rec);
    s.class_id = static_cast<std::uint8_t>(in["class"].i32());
    s.level = in["level"].i32();
    s.expansions = s.owner_expansions = in["expansion_flags"].u32();
    s.profile_level = in["profile_level"].i32();
    s.profile_type = in["profile_type"].i32();
    s.own_base_hp = in["profile_base_hp"].i32();
    s.own_base_endurance = in["profile_base_endurance"].i32();
    s.own_base_mana = in["profile_base_mana"].i32();
    s.stored_hp = in["profile_hp"].i32();
    s.zoning = in["zoning"].truthy();
    s.leadership_active = in["leadership_active"].truthy();
    spawn_fields(s, in["spawn"]);
    if (s.has_spawn) {
        s.spawn_max_hp = in["spawn"]["hp_max"].i32();
        s.spawn_hp = in["spawn"]["hp_current"].i32();
    }
    for (const Json& row : in["class_level_data"].items) {
        if (row["row"].is_null()) continue;
        const Json& v = row["row"];
        ClassLevelRow cl{v["base_hp"].f64(), v["base_mana"].f64(), v["base_endurance"].f64(), v["hp_regen"].f64(),
                         v["endurance_regen"].f64(), v["hp_per_stamina"].f64(), v["mana_per_stat"].f64(),
                         v["endurance_per_stat"].f64()};
        r.tables.class_levels.set(row["level"].i32(), row["class"].u32(), cl);
    }
    r.calls.add(in["calls"]);
    for (const Json& c : in["calls"].items) {
        if (c["fn"].text == "leadership_hp_bonus") s.leadership_hp_bonus = c["ret"].i32();
        if (c["fn"].text == "leadership_mana_bonus") s.leadership_mana_bonus = c["ret"].i32();
    }
    Character ch(s, r.tables, r.lookup());
    common_hooks(ch, r);
    ch.hooks.cur_hp = nullptr;
    ch.hooks.max_hp = nullptr;
    DerivedStats& d = ch.mutable_stats();
    stat_block(d.stats, in["stats"]);
    stat_block(d.stats_no_buffs, in["stats_no_buffs"]);
    d.hp_bonus = in["hp_bonus"].i32();
    d.mana_bonus = in["mana_bonus"].i32();
    d.endurance_bonus = in["endurance_bonus"].i32();
    d.hp_bonus_no_buffs = in["hp_bonus_no_buffs"].i32();
    d.mana_bonus_no_buffs = in["mana_bonus_no_buffs"].i32();
    d.endurance_bonus_no_buffs = in["endurance_bonus_no_buffs"].i32();
    heroic(d, in["heroic"]);
    std::int32_t v = 0;
    switch (kind) {
    case 0: v = ch.max_hp(in["include_buffs"].truthy(), in["unknown_arg"].i32()); break;
    case 1: v = ch.max_mana(in["include_buffs"].truthy()); break;
    case 2: v = ch.max_endurance(in["include_buffs"].truthy()); break;
    default: v = ch.cur_hp(in["update"].truthy(), in["unknown_arg"].i32()); break;
    }
    const Json& out = rec["out"];
    res.check("value", v, out["value"].i32());
    res.check("profile_hp", ch.stored_hp(), out["profile_hp"].i32());
    if (out.has("spawn_hp_max")) res.check("spawn_hp_max", ch.spawn_hp_max(), out["spawn_hp_max"].i32());
    if (out.has("spawn_hp_current")) res.check("spawn_hp_current", ch.spawn_hp_current(), out["spawn_hp_current"].i32());
}

void total_effect_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    s.class_id = static_cast<std::uint8_t>(in["class"].i32());
    s.level = in["level"].i32();
    spawn_fields(s, in["spawn"]);
    r.calls.add(in["calls"]);
    for (const Json& c : in["calls"].items) {
        if (c["fn"].text == "get_spawn_by_id" && !c["ret_nonnull"].truthy()) s.master_level = -1;
        if (c["fn"].text == "spawn_level") s.master_level = c["ret"].i32() & 0xff;
    }
    Character ch(s, r.tables, r.lookup());
    Tape* t = &r.calls;
    ch.hooks.buff_effect = [t](std::int32_t spa, std::int32_t sub) {
        return ret(t->take("buff_effect", {{"spa", spa}, {"subindex", sub}}));
    };
    ch.hooks.item_effect = [t](std::int32_t spa, std::int32_t sub) {
        return ret(t->take("item_effect", {{"spa", spa}, {"subindex", sub}}));
    };
    ch.hooks.aa_effect = [t](std::int32_t spa, std::int32_t sub) {
        return ret(t->take("aa_effect", {{"spa", spa}, {"subindex", sub}}));
    };
    ch.mutable_stats().attack_speed = in["item_haste"].i32();
    ch.mutable_stats().attack_speed_no_buffs = in["item_haste_no_buffs"].i32();
    std::int32_t v = ch.total_effect(in["spa"].i32(), in["include_items"].truthy(), in["subindex"].i32(),
                                     in["include_aa"].truthy(), in["include_buffs"].truthy());
    res.check("value", v, rec["out"]["value"].i32());
}

void spell_duration_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    r.sheet.has_spawn = in["spawn"].truthy();
    r.calls.add(in["calls"]);
    Character ch(r.sheet, r.tables, r.lookup());
    common_hooks(ch, r);
    if (in["spell"].is_null()) {
        res.check("value", 0, rec["out"]["value"].i32()); // a null spell: 0
        return;
    }
    const Json& sp = in["spell"];
    SpellData s;
    s.duration_formula = sp["duration_formula"].u32();
    s.duration_cap = sp["duration_cap"].u32();
    s.spell_type = static_cast<std::uint8_t>(sp["spell_type"].i32());
    for (std::size_t k = 0; k < 12; ++k) {
        s.formula[k] = sp["calc"][k].i32();
        s.effect[k] = sp["spa"][k].i32();
    }
    std::int32_t v = ch.spell_duration(s, static_cast<std::uint8_t>(in["level"].i32()), in["special"].truthy());
    res.check("value", v, rec["out"]["value"].i32());
}

void air_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    r.sheet.race = in["race"].u32();
    r.calls.add(in["calls"]);
    Character ch(r.sheet, r.tables, r.lookup());
    common_hooks(ch, r);
    ch.mutable_stats().stats.sta = in["sta"].i32();
    res.check("value", ch.max_air_supply(), rec["out"]["value"].i32());
}

net::Item ac_item(const Json& j, std::uint32_t deity) {
    net::Item i;
    i.item_class = static_cast<std::uint8_t>(j.has("type") ? j["type"].i32() : 0);
    i.ac = j["ac"].i32();
    i.scaled_value = 100; // no charm scaling: the record's AC is ItemBase_GetAC's result
    if (j.has("type")) {
        i.req_level = j["required_level"].u32();
        i.item_type = static_cast<std::uint8_t>(j["item_type"].i32());
        i.charm_file_id = j["charm_file_id"].u32();
        i.rec_skill = j["recommended_skill"].u32();
        i.rec_level = j["recommended_level"].u32();
        i.prestige = j["prestige"].u32();
        i.races = j["race_ok"].truthy() ? 0xffffffffu : 0u;
        i.deity = j["deity_ok"].truthy() ? 0u : ~(1u << ((deity == 396 ? 1u : deity - 199u) - 1u));
    } else {
        i.races = 0xffffffffu;
    }
    return i;
}

void calc_ac_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    s.class_id = static_cast<std::uint8_t>(in["class"].i32());
    s.base_ac = in["base_ac"].i32();
    s.level = in["level"].i32();
    s.ignores_item_restrictions = in["skip_use_checks"].truthy();
    s.prestige_limit = in["prestige_limit"].i32();
    spawn_fields(s, in["spawn"]);
    for (std::size_t k = 0; k < 22; ++k)
        if (!in["worn"][k].is_null()) s.possessions[k] = ac_item(in["worn"][k], s.deity);
    std::size_t e = 0;
    for (const char* list : {"items_0042def0", "items_0042df20", "items_virtual_4c", "items_virtual_54"})
        for (const Json& it : in[list].items) {
            if (!it.is_null()) s.extra_items[e] = ac_item(it, s.deity);
            ++e;
        }
    r.calls.add(in["calls"]);
    r.virtual_calls.add(in["virtual_calls"]);
    Character ch(s, r.tables, r.lookup());
    common_hooks(ch, r);
    Tape* t = &r.calls;
    Tape* v = &r.virtual_calls;
    ch.hooks.sum_worn_item_ac = [t](std::int32_t type) { return ret(t->take("sum_worn_item_ac", {{"item_type", type}})); };
    ch.hooks.class_ac_bonus = [t](std::int32_t agi) { return ret(t->take("class_ac_bonus", {{"agility", agi}})); };
    ch.hooks.recommended_item_ac = [v](std::int32_t ac, std::uint8_t) {
        return ret(v->take("recommended_item_ac", {{"ac", ac}}));
    };
    ch.hooks.skill_with_item_mods = [v](std::uint32_t skill, bool) {
        return ret(v->take("adjusted_skill", {{"skill", skill}}));
    };
    ch.mutable_stats().stats.agi = in["agility"].i32();
    ch.mutable_stats().stats_no_buffs.agi = in["agility_no_buffs"].i32();
    res.check("value", ch.calc_ac(in["no_cap"].truthy(), in["extra"].truthy()), rec["out"]["value"].i32());
}

void weight_inputs(const Json& in, CharacterSheet& s, DerivedStats& d) {
    spawn_fields(s, in["spawn"]);
    s.level = in["level"].i32();
    s.slowdown_any_level = in["cb_byte_0x460"].i32() != 0;
    s.encumbered_state = in["encumbered_message_state"].i32();
    d.weight = in["carried_weight"].i32();
    d.stats.str = in["str"].i32();
    for (const Json& c : in["calls"].items) {
        if (c["fn"].text == "is_bazaar_zone") s.bazaar_zone = c["ret"].truthy();
        if (c["fn"].text == "is_barter_zone") s.barter_zone = c["ret"].truthy();
        if (c["fn"].text == "drunkenness") s.drunkenness = c["ret"].i32();
    }
}

void calculate_weight_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    r.sheet.fpu = precision(rec);
    DerivedStats d;
    weight_inputs(in, r.sheet, d);
    r.calls.add(in["calls"]);
    Character ch(r.sheet, r.tables, r.lookup());
    common_hooks(ch, r);
    ch.mutable_stats() = d;
    float v = ch.calculate_weight();
    float want = rec["out"]["value"].f32();
    std::uint32_t a, b;
    std::memcpy(&a, &v, 4);
    std::memcpy(&b, &want, 4);
    res.check("value bits", a, b);
    res.check("encumbered_message_state", ch.encumbered_state(), rec["out"]["encumbered_message_state"].i32());
}

void calc_agi_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    s.fpu = precision(rec);
    DerivedStats d;
    weight_inputs(in, s, d);
    s.base_agi = in["base_agi"].i32();
    s.expansions = s.owner_expansions = in["expansion_flags"].u32();
    d.items.agi = in["agi"].i32();
    d.items_no_buffs.agi = in["agi_no_buffs"].i32();
    d.heroic_agi = in["heroic_agi"].i32();
    r.calls.add(in["calls"]);
    r.virtual_calls.add(in["virtual_calls"]);
    Character ch(s, r.tables, r.lookup());
    common_hooks(ch, r);
    Tape* v = &r.virtual_calls;
    ch.hooks.cap_stat = [v](std::int32_t value, int stat) {
        return ret(v->take("cap_stat", {{"value", value}, {"stat", stat}}));
    };
    ch.mutable_stats() = d;
    std::int32_t value = ch.calc_agi(in["include_buffs"].truthy());
    const Json& out = rec["out"];
    res.check("value", value, out["value"].i32());
    res.check("uncapped_agi", ch.stats().uncapped.agi, out["uncapped_agi"].i32());
    res.check("encumbered_message_state", ch.encumbered_state(), out["encumbered_message_state"].i32());
}

void adjusted_skill_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    auto skill = in["skill"].u32();
    s.race = in["race"].u32();
    s.class_id = static_cast<std::uint8_t>(in["class"].i32());
    s.profile_type = in["profile_type"].i32();
    if (skill < 100 && !in["skill_value"].is_null()) s.skills[skill] = in["skill_value"].i32();
    s.profile_null = in["profile_null"].truthy();
    spawn_fields(s, in["spawn"]);
    s.level = in["level"].i32();
    s.highest_level = in["highest_level"].i32();
    r.calls.add(in["calls"]);
    Character ch(s, r.tables, r.lookup());
    Tape* t = &r.calls;
    ch.hooks.racial_skill_bonus = [t](std::uint32_t sk) { return ret(t->take("racial_skill_bonus", {{"skill", sk}})); };
    ch.hooks.skill_cap = [t](std::int32_t level, std::int32_t cls, std::int32_t sk) {
        return ret(t->take("skill_cap", {{"level", level}, {"class", cls}, {"skill", sk}}));
    };
    ch.hooks.skill_min_level = [t](std::uint32_t sk, std::uint32_t cls) {
        return ret(t->take("skill_min_level", {{"skill", sk}, {"class", cls}}));
    };
    res.check("value", ch.adjusted_skill(skill), rec["out"]["value"].i32());
}

void skill_cap_check(const Json& rec, Record& r, Result& res, bool with_effects) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    const Json& c = in["character"];
    if (!c.is_null()) {
        s.class_id = static_cast<std::uint8_t>(c["class"].i32());
        s.race = c["race"].u32();
    }
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, std::int32_t> cells;
    for (const Json& cell : in["skill_cap_table"].items)
        cells[{cell["class"].u32(), cell["skill"].u32(), cell["level"].u32()}] = cell["cap"].i32();
    r.calls.add(in["calls"]);
    Character ch(s, r.tables, r.lookup());
    common_hooks(ch, r);
    Tape* t = &r.calls;
    ch.hooks.max_level = [t]() { return ret(t->take("max_level", {})); };
    ch.hooks.racial_skill_bonus = [t](std::uint32_t sk) { return ret(t->take("racial_skill_bonus", {{"skill", sk}})); };
    ch.hooks.skill_cap_cell = [&cells](std::uint32_t cls, std::uint32_t sk, std::uint32_t level) -> std::optional<std::int32_t> {
        auto it = cells.find({cls, sk, level});
        return it == cells.end() ? 0 : it->second;
    };
    bool items = in["include_items"].truthy(), aa = in["include_aa"].truthy(), buffs = in["include_buffs"].truthy();
    std::int32_t v;
    if (with_effects) {
        v = c.is_null() ? 0 : ch.skill_cap_with_effects(in["skill"].u32(), items, aa, buffs);
    } else {
        const Json& entry = in["skill_entry"];
        ch.hooks.skill_entry = [&entry](std::uint32_t) -> std::optional<std::int32_t> {
            return entry.is_null() ? -1 : (entry["level_capped_skill"].truthy() ? 1 : 0);
        };
        v = ch.skill_cap(in["level"].i32(), in["class"].i32(), in["skill"].i32(), !c.is_null(), items, aa, buffs);
    }
    res.check("value", v, rec["out"]["value"].i32());
}

void regen_check(const Json& rec, Record& r, Result& res, int kind) {
    const Json& in = rec["in"];
    CharacterSheet& s = r.sheet;
    s.fpu = precision(rec);
    s.level = in["level"].i32();
    s.expansions = in["expansion_flags"].u32();
    s.class_id = static_cast<std::uint8_t>(in["class"].i32());
    s.sitting = (in["standstate"].i32() & 0xff) == 110;
    s.food = in["hunger"].i32();
    s.drink = in["thirst"].i32();
    s.innate_3258 = in["innate_skill_10"].i32();
    s.mana_regen_sitting = in["profile_mana_regen_meditating"].i32();
    s.mana_regen_standing = in["profile_mana_regen"].i32();
    spawn_fields(s, in["spawn"]);
    const Json& sp = in["spawn"];
    bool speed_zero = false;
    if (s.has_spawn) {
        s.spawn_sitting = (sp["stand_state"].i32() & 0xff) == 110;
        s.hp_regen_modifier = sp["hp_regen_mod"].f32();
        s.endurance_regen_modifier = sp["end_regen_mod"].f32();
        s.mana_regen_modifier = sp["mana_regen_mod"].f32();
        speed_zero = sp["speed_run"].f32() == 0.0f;
        s.sitting_ms = in["now"].u32() - sp["sit_start_time"].u32();
    }
    s.has_owner = !in["owner"].is_null();
    if (s.has_owner) {
        const Json& o = in["owner"];
        s.owner_status = o["status"].i32();
        s.owner_expansions = o["expansion_flags"].u32();
        s.owner_food = o["hunger"].i32();
        s.owner_drink = o["thirst"].i32();
    }
    s.buffs.clear();
    for (const Json& b : in["buffs"].items) {
        SheetBuff sb;
        sb.type = b["active"].truthy() ? 2 : 0;
        sb.spell_id = b["spell_id"].i32();
        s.buffs.push_back(sb);
        if (!b["spell_category"].is_null()) {
            SpellData d;
            d.effect.fill(254);
            d.global_group = b["spell_category"].i32();
            r.spells[sb.spell_id] = d;
        }
    }
    s.tutorial = in["tutorial"].truthy();
    s.attacking = in["attacking"].truthy();
    s.running = in["run_walk_state"].i32() == 1 && !speed_zero;
    s.leadership_active = in["leadership_active"].truthy();
    s.fast_regen_hp_divisor = in["fast_hp_divisor"].i32();
    s.fast_regen_mana_divisor = in["fast_mana_divisor"].i32();
    s.fast_regen_endurance_divisor = in["fast_endurance_divisor"].i32();
    for (const Json& c : in["calls"].items) {
        const std::string& fn = c["fn"].text;
        if (fn == "leadership_hp_regen_bonus") s.leadership_hp_regen_bonus = c["ret"].i32();
        if (fn == "can_med_on_horse") s.can_med_on_horse = c["ret"].truthy();
        if (fn == "is_resting_for_fast_regen") s.fast_regen_resting = c["ret"].truthy();
    }
    r.calls.add(in["calls"]);
    Character ch(s, r.tables, r.lookup());
    common_hooks(ch, r);
    Tape* t = &r.calls;
    ch.hooks.item_effect = [t](std::int32_t spa, std::int32_t sub) {
        return ret(t->take("item_effect", {{"spa", spa}, {"subindex", sub}}));
    };
    ch.hooks.aa_effect = [t](std::int32_t spa, std::int32_t sub) {
        return ret(t->take("aa_effect", {{"spa", spa}, {"subindex", sub}}));
    };
    ch.hooks.heroic_sta_bonus = [t]() { return ret(t->take("heroic_sta_bonus", {})); };
    ch.hooks.base_hp_regen = [t]() { return ret(t->take("base_hp_regen", {})); };
    ch.hooks.base_endurance_regen = [t]() { return ret(t->take("base_endurance_regen", {})); };
    ch.hooks.max_effects = [t]() { return ret(t->take("max_effects", {})); };
    ch.hooks.is_valid_spell_id = [t](std::int32_t id) -> std::optional<bool> {
        if (auto v = ret(t->take("is_valid_spell_id", {{"spell_id", id}}))) return *v != 0;
        return std::nullopt;
    };
    ch.hooks.has_spell_affect = [t](std::int32_t spa) -> std::optional<bool> {
        if (auto v = ret(t->take("has_spell_affect", {{"spa", spa}}))) return *v != 0;
        return std::nullopt;
    };
    ch.hooks.adjusted_skill = [t](std::uint32_t skill) { return ret(t->take("adjusted_skill", {{"skill", skill}})); };
    std::int32_t meditate = in["meditate_skill"].i32();
    ch.hooks.skill_with_item_mods = [meditate](std::uint32_t, bool) -> std::optional<std::int32_t> { return meditate; };
    std::int32_t max_mana = in["max_mana"].i32(), max_end = in["max_endurance"].i32();
    ch.hooks.max_mana = [max_mana](bool) -> std::optional<std::int32_t> { return max_mana; };
    ch.hooks.max_endurance = [max_end](bool) -> std::optional<std::int32_t> { return max_end; };
    DerivedStats& d = ch.mutable_stats();
    d.weight = in["carried_weight"].i32();
    d.stats.str = in["str"].i32();
    d.hp_regen = in["hp_regen_bonus"].i32();
    d.mana_regen = in["mana_regen_bonus"].i32();
    d.endurance_regen = in["endurance_regen_bonus"].i32();
    heroic(d, in["heroic"]);
    bool items = in["include_items"].truthy(), no_rest = in["no_rest"].truthy();
    const Json& out = rec["out"];
    if (kind == 0) {
        // The vectors call GetHPRegen's flag "capped"; it is the bleeding flag DoPassageOfTime prints 0x318d for.
        bool bleeding = true; // the harness fills the flag with 0xcc; paths that return early leave it
        std::int32_t v = ch.hp_regen(items, in["capped_out"].truthy() ? &bleeding : nullptr, no_rest);
        res.check("value", v, out["value"].i32());
        if (out.has("capped")) res.check("capped", bleeding, out["capped"].truthy());
    } else if (kind == 1) {
        res.check("value", ch.mana_regen(items, no_rest), out["value"].i32());
    } else {
        res.check("value", ch.endurance_regen(items, no_rest), out["value"].i32());
    }
}

void focus_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    r.virtual_calls.add(in["lookups"]);
    Character ch(r.sheet, r.tables, r.lookup());
    Tape* v = &r.virtual_calls;
    auto found = [](const Json* c, bool with_value) -> std::optional<FocusFound> {
        if (!c) return std::nullopt;
        FocusFound f;
        f.found = (*c)["ret"].truthy();
        f.percent = (*c)["percent"].i32();
        f.extra = (*c)["extra"].i32();
        if (with_value) f.value = (*c)["focus_value"].i32();
        return f;
    };
    ch.hooks.best_item_focus = [v, found](std::int32_t type) { return found(v->take("best_item_focus", {{"focus_type", type}}), false); };
    ch.hooks.best_buff_focus = [v, found](std::int32_t type, bool) { return found(v->take("best_buff_focus", {{"focus_type", type}}), true); };
    ch.hooks.best_aa_focus = [v, found](std::int32_t type) { return found(v->take("best_aa_focus", {{"focus_type", type}}), true); };
    ch.hooks.item_spell_id = [v](std::int32_t extra) -> std::optional<std::pair<std::int32_t, bool>> {
        const Json* c = v->take("item_spell_id", {{"extra", extra}});
        if (!c) return std::nullopt;
        return std::pair<std::int32_t, bool>{(*c)["ret"].i32(), (*c)["valid"].truthy()};
    };
    ch.hooks.spell_focus_base = [v](std::int32_t id) -> std::optional<std::pair<bool, std::int32_t>> {
        const Json* c = v->take("get_spell", {{"spell_id", id}});
        if (!c) return std::nullopt;
        return std::pair<bool, std::int32_t>{(*c)["ret"].truthy(), (*c)["focus_value"].i32()};
    };
    SpellData cast;
    cast.cast_time = in["cast_time"].u32();
    res.check("value", ch.focus_casting_time_modifier(cast, in["buff_arg"].truthy()), rec["out"]["value"].i32());
}

// ProcessAllStats' item sums (game/mechanics/item_sums.h): the recorded item
// getters, lookups and answers in, the sums and copies out. The Calc*
// functions it ends with are mocked in the record (each is checked on its own).
class RecordedItemSums final : public ItemSumQueries {
public:
    RecordedItemSums(const Json& in, const Json& notes, Record& r) : in_(in), notes_(notes), r_(r) {}
    std::int32_t level() override { return ret(r_.virtual_calls.take("level", {})).value_or(0); }
    bool any_race_deity() override { return ret(r_.virtual_calls.take("any_race_deity", {})).value_or(0) != 0; }
    std::int32_t race() override { return in_["race"].i32(); }
    bool has_pc_base() override { return ret(r_.virtual_calls.take("pc_base", {})).value_or(0) != 0; }
    std::int32_t deity() override { return ret(r_.calls.take("deity", {})).value_or(0); }
    std::int32_t limit(std::int32_t which) override {
        return ret(r_.virtual_calls.take("limit", {{"which", which}})).value_or(-1);
    }
    std::uint8_t item_type_to_skill(std::int32_t type) override {
        return static_cast<std::uint8_t>(ret(r_.calls.take("item_type_to_skill", {{"item_type", type}})).value_or(0));
    }
    std::int32_t recommended_scale(const StatItem&, std::int32_t value, std::uint32_t skill) override {
        // The test's fake (FakeScale: value * 3 / 4 + (skill & 7)); the recorded notes, in call order, must
        // agree with it.
        std::int32_t fake = value * 3 / 4 + static_cast<std::int32_t>(skill & 7);
        while (next_note_ < notes_.items.size() && notes_[next_note_]["fn"].text != "recommended_scale") ++next_note_;
        if (next_note_ < notes_.items.size()) {
            const Json& c = notes_[next_note_++];
            if (c["value"].i32() != value || c["skill"].u32() != skill || c["ret"].i32() != fake) ++scale_mismatches;
        }
        return fake;
    }
    std::uint32_t damage_slot(std::int32_t skill) override {
        char key[8];
        std::snprintf(key, sizeof key, "%02x", static_cast<unsigned>(skill));
        return in_["damage_bonus_slot"][key].u32();
    }

    int scale_mismatches = 0;

private:
    const Json& in_;
    const Json& notes_;
    std::size_t next_note_ = 0;
    Record& r_;
};

void process_all_stats_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    r.calls.add(out["calls"]);
    r.virtual_calls.add(out["virtual_calls"]);
    static const char* const kNames[kItemStatCount] = {
        "str", "sta", "cha", "dex", "int", "agi", "wis", "sv_magic", "sv_cold", "sv_fire", "sv_poison", "sv_disease",
        "sv_corruption", "hp", "mana", "endurance", "combat_effects", "shielding", "spell_shield", "avoidance",
        "accuracy", "stun_resist", "dot_shielding", "damage_shield_mitigation", "heroic_str", "heroic_int",
        "heroic_wis", "heroic_agi", "heroic_dex", "heroic_sta", "heroic_cha", "heroic_sv_magic", "heroic_sv_fire",
        "heroic_sv_cold", "heroic_sv_disease", "heroic_sv_poison", "heroic_sv_corruption", "heal_amount",
        "spell_damage", "clairvoyance", "strike_through", "attack", "hp_regen", "mana_regen", "endurance_regen",
        "damage_shield", "potion_slots", "damage_bonus_0a", "damage_bonus_08", "damage_bonus_15", "damage_bonus_17",
        "damage_bonus_1a", "damage_bonus_1e", "damage_bonus_26", "damage_bonus_34", "damage_bonus_4a"};
    std::vector<StatItem> items;
    for (const Json& j : in["items"].items) {
        StatItem it;
        it.kind = j["kind"].i32();
        it.required_level = j["required_level"].i32();
        it.races = j["races"].u32();
        it.deities = j["deities"].u32();
        it.prestige = j["prestige"].i32();
        it.charm_file_id = j["charm_file_id"].i32();
        it.recommended_skill = j["recommended_skill"].i32();
        it.recommended_level = j["recommended_level"].i32();
        it.item_type = j["item_type"].i32();
        for (int k = 0; k < kItemStatCount; ++k) it.stats[static_cast<std::size_t>(k)] = j["stats"][kNames[k]].i32();
        items.push_back(it);
    }
    auto lookups = [&](const char* name) {
        std::vector<const StatItem*> v;
        for (const Json& slot : in["slots"][name].items)
            v.push_back(slot.is_null() ? nullptr : &items[slot.u32() % items.size()]);
        return v;
    };
    auto worn = lookups("worn"), b = lookups("source_b"), c = lookups("source_c"), o4c = lookups("own_4c"),
         o54 = lookups("own_54");
    RecordedItemSums q(in, out["virtual_calls"], r);
    ItemSums sums;
    sum_item_stats(sums, ItemSources{worn, b, c, o4c, o54}, q);
    // The base sums as CalcSTR(0) saw them: all items and worn only.
    static const char* const kBase[14] = {"str", "sta", "cha", "dex", "int", "agi", "wis", "sv_poison", "sv_magic",
                                          "sv_disease", "sv_corruption", "sv_fire", "sv_cold", "sv_physical"};
    for (std::uint32_t k = 0; k < 14; ++k) {
        res.check(std::string("all ") + kBase[k], sums.base_at(0x4a4 + k * 4), out["base_stat_sums"][kBase[k]]["all"].i32());
        res.check(std::string("worn ") + kBase[k], sums.base_at(0x510 + k * 4), out["base_stat_sums"][kBase[k]]["worn"].i32());
    }
    if (const Json* haste = r.calls.take("calc_item_haste", {}))
        res.check("item haste argument", sums.at(0x124), (*haste)["item_haste"].i32());
    res.check("scaler calls in order", q.scale_mismatches, 0);
    // The adjuster: the recorded notes, in call order (the recording's fake adjuster: value / 2 + kind * adjust_factor).
    const Json& bonus = out["bonus"];
    std::int32_t factor = in["adjust_factor"].i32();
    Tape* vt = &r.virtual_calls;
    derive_item_bonuses(sums, [vt, factor, &res](std::int32_t value, int kind) {
        std::int32_t fake = value / 2 + kind * factor;
        auto note = ret(vt->take("adjust_bonus", {{"value", value}, {"kind", kind}}));
        res.check("adjust_bonus recorded", note.value_or(fake), fake);
        return fake;
    });
    static const char* const kOwn[] = {"hp", "mana", "endurance", "combat_effects", "shielding", "spell_shield",
                                       "avoidance", "accuracy", "stun_resist", "strike_through"};
    for (std::uint32_t k = 0; k < 10; ++k) res.check(kOwn[k], sums.at(0x24 + k * 4), bonus[kOwn[k]].i32());
    for (std::uint32_t k = 0; k < 9; ++k) {
        res.check("damage_bonus", sums.at(0x4c + k * 4), bonus["damage_bonus"][k].i32());
        res.check("damage_bonus_raw", sums.at(0x70 + k * 4), bonus["damage_bonus_raw"][k].i32());
    }
    static const char* const kMore[] = {
        "dot_shielding", "damage_shield_mitigation", "combat_effects_raw", "spell_shield_raw", "shielding_raw",
        "damage_shield_raw", "dot_shielding_raw", "damage_shield_mitigation_raw", "avoidance_raw", "accuracy_raw",
        "stun_resist_raw", "strike_through_raw", "heroic_str", "heroic_int", "heroic_wis", "heroic_agi", "heroic_dex",
        "heroic_sta", "heroic_cha", "heroic_sv_magic", "heroic_sv_fire", "heroic_sv_cold", "heroic_sv_disease",
        "heroic_sv_poison", "heroic_sv_corruption", "heal_amount", "spell_damage", "heal_amount_dot",
        "spell_damage_dot", "clairvoyance", "attack", "hp_regen", "mana_regen", "endurance_regen", "damage_shield"};
    for (std::uint32_t k = 0; k < std::size(kMore); ++k) res.check(kMore[k], sums.at(0x98 + k * 4), bonus[kMore[k]].i32());
    res.check("potion_slots", sums.at(0x128), bonus["potion_slots"].i32());
    static const char* const kWorn[] = {"hp", "mana", "endurance", "combat_effects", "shielding", "spell_shield",
                                        "avoidance", "accuracy", "stun_resist", "dot_shielding", "strike_through",
                                        "attack", "hp_regen", "mana_regen", "endurance_regen", "damage_shield",
                                        "damage_shield_mitigation", "attack_speed", "potion_slots"};
    for (std::uint32_t k = 0; k < std::size(kWorn); ++k)
        res.check(std::string("worn_only ") + kWorn[k], sums.at(0x12c + k * 4), bonus["worn_only"][kWorn[k]].i32());
    for (std::uint32_t k = 0; k < 9; ++k)
        res.check("worn_only damage_bonus", sums.at(0x178 + k * 4), bonus["worn_only"]["damage_bonus"][k].i32());
}

void mana_regen_tick_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    r.calls.add(rec["out"]["calls"]);
    std::int32_t current = ret(r.calls.take("cur_mana", {})).value_or(0);
    Tape* t = &r.calls;
    std::int32_t mana = apply_mana_regen_tick(in["mana"].i32(), current, in["max_mana"].i32(),
                                              [t] { return ret(t->take("mana_regen", {})).value_or(0); });
    res.check("mana", mana, rec["out"]["mana"].i32());
}

void endurance_upkeep_check(const Json& rec, Record& r, Result& res) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    r.virtual_calls.add(out["virtual_calls"]);
    Tape* vt = &r.virtual_calls;
    EnduranceUpkeep u = apply_endurance_upkeep(in["endurance"].i32(), in["endurance_upkeep"].i32(),
                                               [vt](std::int32_t) { return ret(vt->take("endurance", {})).value_or(0); },
                                               !in["spawn_type"].is_null() && in["spawn_type"].i32() == 0);
    res.check("endurance", u.endurance, out["endurance"].i32());
    bool removed = false;
    for (const Json& c : out["calls"].items) removed = removed || c["fn"].text == "remove_pc_affect";
    res.check("discipline removed", u.drop_discipline, removed);
}

// ProcessAffects (game/mechanics/ticks.h): the callees' answers replay from
// the record; the test's CalcAffectChange and CalcTickHPChange can drop the
// spawn mid-call (in.drop_spawn_at).
class RecordedAffects final : public AffectQueries {
public:
    RecordedAffects(const Json& rec, std::vector<AffectBuff>& buffs, const std::map<std::int32_t, AffectSpell>& spells,
                    int drop_after)
        : rec_(rec), buffs_(buffs), spells_(spells), drop_after_(drop_after) {
        calls_.add(rec["out"]["calls"]);
        virtuals_.add(rec["out"]["virtual_calls"]);
        spawn_ = !rec["in"]["spawn"].is_null();
    }
    std::vector<std::string> events;

    int max_effects() override { return rec_["in"]["max_effects"].i32(); }
    AffectBuff& effect(int index) override { return buffs_[static_cast<std::size_t>(index) % buffs_.size()]; }
    bool valid_spell(std::int32_t id) override { return flag(calls_.take("is_valid_spell_id", {{"spell_id", id}})); }
    const AffectSpell* spell(std::int32_t id) override {
        auto it = spells_.find(id);
        return it == spells_.end() ? nullptr : &it->second;
    }
    void cache_buff_slot_effects(int index) override { events.push_back("cache " + std::to_string(index)); }
    bool countdown_held(const AffectSpell*) override { return flag(calls_.take("is_buff_countdown_held", {})); }
    bool has_affect(std::int32_t spa) override { return flag(calls_.take("has_spell_affect", {{"spa", spa}})); }
    bool tick_varying(std::int32_t spa) override { return flag(calls_.take("is_tick_varying", {{"spa", spa}})); }
    std::int32_t affect_change(const AffectSpell&, const AffectBuff&, int slot, int index) override {
        std::int32_t v = ret(calls_.take("calc_affect_change", {{"slot", slot}, {"index", index}})).value_or(0);
        dropped_call();
        return v;
    }
    std::int32_t cur_endurance() override { return ret(calls_.take("cur_endurance", {})).value_or(0); }
    std::int32_t cur_mana() override { return ret(calls_.take("cur_mana", {})).value_or(0); }
    void remove_affect(AffectBuff&, int a, int b) override {
        events.push_back("remove_affect " + std::to_string(a) + " " + std::to_string(b));
    }
    bool spawn_present() override { return spawn_; }
    bool has_mount() override { return flag(calls_.take("get_mount", {})); }
    bool find_summon(bool) override { return flag(calls_.take("find_spawn_by_name", {})); }
    void ride_mount() override { events.push_back("mount_found"); }
    void view_eye() override { events.push_back("set_view_actor"); }
    std::int32_t spell_level_needed(const AffectSpell*, int cls) override {
        return ret(calls_.take("spell_level_needed", {{"class", cls}})).value_or(0);
    }
    void chat(std::uint32_t id) override { events.push_back("string " + std::to_string(id)); }
    std::int32_t rand() override { return ret(calls_.take("rand", {})).value_or(0); }
    void stop_cast() override { events.push_back("stop_cast 1"); }
    bool hp_tick_allowed(std::int32_t base2) override {
        return flag(virtuals_.take("hp_tick_allowed", {{"base2", base2}}));
    }
    std::int32_t tick_hp_change(int, const AffectBuff&, int slot, const AffectSpell&) override {
        std::int32_t v = ret(calls_.take("calc_tick_hp_change", {{"slot", slot}})).value_or(0);
        dropped_call();
        return v;
    }
    bool timers_on_hold() override { return flag(calls_.take("are_buff_timers_on_hold", {})); }
    bool spell_has_spa(const AffectSpell&, std::int32_t spa) override {
        return flag(calls_.take("spell_has_spa", {{"spa", spa}}));
    }
    std::int32_t recheck(std::int32_t id) override {
        return ret(calls_.take("spell_recheck", {{"spell_id", id}})).value_or(0);
    }
    void end_stun() override { events.push_back("end_stun"); }
    void modify_cur_hp(std::int32_t amount) override { events.push_back("modify_cur_hp " + std::to_string(amount)); }

private:
    static bool flag(const Json* c) { return ret(c).value_or(0) != 0; }
    void dropped_call() {
        if (++drop_calls_ == drop_after_) spawn_ = false;
    }
    const Json& rec_;
    std::vector<AffectBuff>& buffs_;
    const std::map<std::int32_t, AffectSpell>& spells_;
    Tape calls_, virtuals_;
    int drop_after_, drop_calls_ = 0;
    bool spawn_ = true;
};

// The recorded events ProcessAffects' port reports, in order.
std::vector<std::string> recorded_affect_events(const Json& out) {
    std::vector<std::string> v;
    for (const Json& c : out["calls"].items) {
        const std::string& fn = c["fn"].text;
        if (fn == "cache_buff_slot_effects") v.push_back("cache " + std::to_string(c["index"].i32()));
        else if (fn == "remove_affect") v.push_back("remove_affect " + std::to_string(c["arg1"].i32()) + " " + std::to_string(c["arg2"].i32()));
        else if (fn == "set_view_actor" || fn == "end_stun") v.push_back(fn);
        else if (fn == "string") v.push_back("string " + std::to_string(c["id"].i32()));
        else if (fn == "stop_cast") v.push_back("stop_cast " + std::to_string(c["arg"].i32()));
        else if (fn == "modify_cur_hp") v.push_back("modify_cur_hp " + std::to_string(c["amount"].i32()));
    }
    for (const Json& c : out["virtual_calls"].items)
        if (c["fn"].text == "mount_found") v.push_back("mount_found");
    return v;
}

void process_affects_check(const Json& rec, Record&, Result& res) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    std::map<std::int32_t, AffectSpell> spells;
    for (const Json& j : in["spells"].items) {
        AffectSpell sp;
        sp.id = j["id"].i32();
        sp.duration_formula = j["duration_formula"].i32();
        sp.float_224 = j["float_224"].f32();
        sp.float_21c = j["float_21c"].f32();
        sp.drain_removes = static_cast<std::uint8_t>(j["drain_removes"].i32());
        sp.uninterruptible = j["uninterruptible"].truthy();
        for (std::size_t k = 0; k < 12; ++k) {
            sp.spa[k] = j["spa"][k].i32();
            sp.base[k] = j["base"][k].i32();
            sp.base2[k] = j["base2"][k].i32();
        }
        spells[sp.id] = sp;
    }
    const std::vector<std::string> recorded_events = recorded_affect_events(out);
    int drop_after = in["drop_spawn_at"].i32(); // the n-th CalcAffectChange / CalcTickHPChange call drops the spawn
    {
        Result attempt;
        std::vector<AffectBuff> buffs;
        for (const Json& j : in["buffs"].items)
            buffs.push_back({static_cast<std::uint8_t>(j["type"].i32()), static_cast<std::uint8_t>(j["caster_level"].i32()),
                             j["spell_id"].i32(), j["duration"].i32()});
        AffectState st;
        st.class_id = static_cast<std::uint8_t>(in["profile"]["class"].i32());
        st.mana = in["profile"]["mana"].i32();
        st.endurance = in["profile"]["endurance"].i32();
        const Json& sp = in["spawn"];
        st.spawn_type = static_cast<std::uint8_t>(sp["type"].i32());
        st.speed_run = sp["speed_run"].f32();
        st.speed_multiplier = sp["float_18"].f32();
        st.casting_spell_id = sp.is_null() ? -1 : sp["casting_spell_id"].i32();
        st.hold_flag = static_cast<std::uint8_t>(sp["hold_flag"].i32());
        st.tick_cache_valid = static_cast<std::uint8_t>(in["tick_cache_valid"].i32());
        st.controlled_present = in["controlled_player"].truthy();
        RecordedAffects q(rec, buffs, spells, drop_after);
        std::vector<std::string> want_events = recorded_events;
        attempt.check("value", process_affects(st, q), out["value"].i32());
        for (std::size_t k = 0; k < buffs.size(); ++k)
            attempt.check("buff " + std::to_string(k) + " duration", buffs[k].duration, out["buffs"][k]["duration"].i32());
        attempt.check("mana", st.mana, out["profile"]["mana"].i32());
        attempt.check("endurance", st.endurance, out["profile"]["endurance"].i32());
        if (st.stats_dirty) attempt.check("stats_dirty", 1, out["stats_dirty"].i32());
        attempt.check("tick_cache_valid", st.tick_cache_valid, out["tick_cache_valid"].i32());
        if (out.has("spawn_hold_flag")) attempt.check("spawn_hold_flag", st.hold_flag, out["spawn_hold_flag"].i32());
        attempt.check("controlled_is_eye", st.controlled_is_eye, out["controlled_is_eye"].truthy());
        // The record keeps the mocked calls and the virtual calls in separate lists: the
        // virtual one (mount_found) is compared apart from the others' order.
        std::vector<std::string> got_events;
        std::int64_t mounts = 0;
        for (const auto& e : q.events) (e == "mount_found" ? ++mounts : (got_events.push_back(e), 0));
        attempt.check("mount_found", mounts, std::count(want_events.begin(), want_events.end(), "mount_found"));
        std::erase(want_events, "mount_found");
        attempt.check("events", static_cast<std::int64_t>(got_events.size()), static_cast<std::int64_t>(want_events.size()));
        for (std::size_t k = 0; k < got_events.size() && k < want_events.size(); ++k)
            if (got_events[k] != want_events[k]) attempt.mismatches.push_back({"event " + std::to_string(k), got_events[k] + " != " + want_events[k]});
        res = attempt;
    }
}

std::map<std::string, Checker> checkers() {
    std::map<std::string, Checker> m;
    m["CharacterZoneClient::Max_HP"] = [](const Json& j, Record& r, Result& res) { vitals(j, r, res, 0); };
    m["CharacterZoneClient::Max_Mana"] = [](const Json& j, Record& r, Result& res) { vitals(j, r, res, 1); };
    m["CharacterZoneClient::Max_Endurance"] = [](const Json& j, Record& r, Result& res) { vitals(j, r, res, 2); };
    m["CharacterZoneClient::Cur_HP"] = [](const Json& j, Record& r, Result& res) { vitals(j, r, res, 3); };
    m["CharacterZoneClient::TotalEffect"] = total_effect_check;
    m["CharacterZoneClient::SpellDuration"] = spell_duration_check;
    m["PcClient::MaxAirSupply"] = air_check;
    m["CharacterZoneClient_CalcAC"] = calc_ac_check;
    m["EQ_Character::CalculateWeight"] = calculate_weight_check;
    m["CharacterZoneClient_CalcAGI"] = calc_agi_check;
    m["CharacterZoneClient::GetAdjustedSkill"] = adjusted_skill_check;
    m["CSkillMgr::GetSkillCap"] = [](const Json& j, Record& r, Result& res) { skill_cap_check(j, r, res, false); };
    m["CSkillMgr_GetSkillCapWithEffects"] = [](const Json& j, Record& r, Result& res) { skill_cap_check(j, r, res, true); };
    m["CharacterZoneClient::GetHPRegen"] = [](const Json& j, Record& r, Result& res) { regen_check(j, r, res, 0); };
    m["CharacterZoneClient::GetManaRegen"] = [](const Json& j, Record& r, Result& res) { regen_check(j, r, res, 1); };
    m["CharacterZoneClient::GetEnduranceRegen"] = [](const Json& j, Record& r, Result& res) { regen_check(j, r, res, 2); };
    m["CharacterZoneClient::GetFocusCastingTimeModifier"] = focus_check;
    m["CharacterZoneClient::ProcessAllStats"] = process_all_stats_check;
    m["CharacterZoneClient_ApplyManaRegenTick"] = mana_regen_tick_check;
    m["CharacterZoneClient_ApplyEnduranceUpkeep"] = endurance_upkeep_check;
    m["CharacterZoneClient::ProcessAffects"] = process_affects_check;
    return m;
}

std::optional<fs::path> vectors_dir() { return hailstone::test::vector_dir(); }

} // namespace

TEST_CASE("Character mechanics test vectors from the original client") {
    auto dir = vectors_dir();
    std::error_code ec;
    if (!dir || !fs::is_directory(*dir, ec)) {
        MESSAGE("no recorded test vectors (HAILSTONE_VECTORS_DIR); skipped");
        return;
    }
    auto table = checkers();
    int files = 0;
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(*dir))
        if (entry.path().extension() == ".jsonl") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        std::ifstream f(path);
        std::string line;
        int n = 0, checked = 0, failed = 0, misses = 0;
        std::map<std::string, int> by_precision_fail;
        std::string first_failure;
        while (std::getline(f, line)) {
            ++n;
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            hailstone::test::normalise_recorded_names(line);
            Json j;
            try {
                j = hailstone::test::parse_json(line);
            } catch (const std::exception& e) {
                FAIL(path.filename().string() << ":" << n << " not JSON: " << e.what());
                continue;
            }
            auto it = table.find(j["fn"].text);
            if (it == table.end()) break; // another port's vectors (movement)
            Record r;
            Result res;
            it->second(j, r, res);
            ++checked;
            misses += r.calls.misses + r.virtual_calls.misses;
            if (!res.mismatches.empty()) {
                ++failed;
                ++by_precision_fail[j["precision"].text.empty() ? "-" : j["precision"].text];
                if (first_failure.empty() && hailstone::core::env("DUMP"))
                    for (auto& mm : res.mismatches) MESSAGE(n << " " << mm.first << " " << mm.second);
                if (first_failure.empty())
                    first_failure = std::to_string(n) + ": " + res.mismatches[0].first + " " + res.mismatches[0].second;
            }
        }
        if (!checked) continue;
        ++files;
        std::ostringstream extra;
        for (const auto& [p, c] : by_precision_fail) extra << " " << p << ":" << c;
        MESSAGE(path.filename().string() << ": " << (checked - failed) << "/" << checked << " pass"
                                         << (failed ? " (failures by precision" + extra.str() + "; first " + first_failure + ")" : "")
                                         << (misses ? " [" + std::to_string(misses) + " callee lookups not recorded]" : ""));
        CHECK(failed == 0);
    }
    MESSAGE(files << " character mechanics vector files checked from " << dir->string());
}
