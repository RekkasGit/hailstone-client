// Spell gems and buff stacking (game/mechanics/spell_gems.h, find_affect_slot.h) against the original's vectors
// (recorded from eqgame.exe): CSpellBookWnd_GetSpellMemTicksLeft (3000), CSpellBookWnd_FinishMemorizing (600),
// Handle_OP_MemorizeSpell (2500), PcZoneClient_IsLinkedSpellTimerReady (600), EQ_Spell_StackingSpaTables (529),
// CharacterZoneClient_CanUseMemorizedSpellSlot (1500) and CharacterZoneClient_FindAffectSlot (1600).
//
// Each record holds the inputs and the mocked callees the original called, in order, with their arguments and
// answers. The port runs against a replay: each callee it calls must be the record's next call with the same
// arguments, and gets the recorded answer; then every recorded output is compared. Two outputs of
// Handle_OP_MemorizeSpell start from values the record does not hold (the casting gem byte and the cast item's
// dirty byte): they are compared only when the client writes them. FindAffectSlot runs the real stacking rules:
// CalcAffectChange is the client's CalcAffectChangeGeneric (Character::calc_affect_change; the records' spells have
// no distance modifier and no formula 123, 137 or 138).
#include <doctest/doctest.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/mechanics/character.h"
#include "game/mechanics/find_affect_slot.h"
#include "game/mechanics/spell_gems.h"
#include "game/spells.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using hailstone::test::Json;

namespace {

// The record's calls, consumed in order.
struct Replay {
    const Json& calls;
    std::size_t at = 0;
    std::string error;
    explicit Replay(const Json& c) : calls(c) {}
    const Json* next(const std::string& fn) {
        if (!error.empty()) return nullptr;
        if (at >= calls.items.size()) {
            error = "extra call " + fn;
            return nullptr;
        }
        const Json& c = calls.items[at++];
        if (c["fn"].text != fn) {
            error = "call " + std::to_string(at - 1) + ": " + fn + " instead of " + c["fn"].text;
            return nullptr;
        }
        return &c;
    }
    // A call whose arguments must match.
    const Json* expect(const std::string& fn, std::initializer_list<std::pair<const char*, std::int64_t>> args) {
        const Json* c = next(fn);
        if (!c) return nullptr;
        for (auto& [k, v] : args) {
            if ((*c)[k].i64() != v) {
                error = fn + " " + k + " " + std::to_string(v) + " instead of " + std::to_string((*c)[k].i64());
                return nullptr;
            }
        }
        return c;
    }
    std::int64_t ret(const Json* c) const { return c ? (*c)["ret"].i64() : 0; }
    std::string finish() {
        if (error.empty() && at != calls.items.size())
            error = "missing call " + calls.items[at]["fn"].text;
        return error;
    }
};

std::string diff(const char* what, std::int64_t got, std::int64_t want) {
    return got == want ? "" : std::string(what) + " " + std::to_string(got) + " instead of " + std::to_string(want);
}

// ---------------------------------------------------------------- memorising

class TickReplay final : public gems::MemorizeQueries {
public:
    TickReplay(Replay& r, std::int32_t level) : r_(r), level_(level) {}
    std::uint8_t spell_level_needed(std::int32_t, std::uint8_t class_id) override {
        return static_cast<std::uint8_t>(r_.ret(r_.expect("spell_level_needed", {{"class", class_id}})));
    }
    std::int32_t level() override { return level_; }
    std::int32_t calc_dex() override { return static_cast<std::int32_t>(r_.ret(r_.expect("calc_dex", {{"with_caps", 1}}))); }
    std::int32_t calc_int() override { return static_cast<std::int32_t>(r_.ret(r_.expect("calc_int", {{"with_caps", 1}}))); }
    std::int32_t adjusted_skill(std::int32_t skill) override {
        return static_cast<std::int32_t>(r_.ret(r_.expect("get_adjusted_skill", {{"skill", skill}})));
    }
    bool has_alternate_ability(std::int32_t aa) override {
        return r_.ret(r_.expect("has_alternate_ability", {{"aa", aa}, {"b", 0}, {"c", 0}})) != 0;
    }
    void push_pending_reason() override { r_.next("push_pending_reason"); }
    bool send(const gems::MemorizePacket& p) override {
        packet = p;
        return r_.ret(r_.expect("send", {{"channel", 4}, {"size", 0x12}})) != 0;
    }
    void close_connection() override { r_.next("close_connection"); }
    void debug_log() override { r_.next("debug_log"); }
    void cursor_attachment_vf94() override { vf94 = true; }
    std::optional<gems::MemorizePacket> packet;
    bool vf94 = false;

private:
    Replay& r_;
    std::int32_t level_;
};

std::string check_ticks(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    gems::MemorizeBook book{in["mem_gem"].i32(), in["mem_spell"].i32(), in["ticks_left"].i32()};
    gems::MemorizeWorld w;
    w.class_id = static_cast<std::uint8_t>(in["class"].i64());
    w.now = in["now"].u32();
    w.last_tick = in["last_tick"].u32();
    w.item_pending = in["item_pending"].i32();
    w.pending_reasons = in["pending_reasons"].u32();
    w.connected = in["connected"].truthy();
    w.quiet_net_log = in["quiet_net_log"].truthy();
    Replay r(in["calls"]);
    TickReplay q(r, in["level"].i32());
    std::int32_t value = gems::get_spell_mem_ticks_left(book, w, q);
    if (auto e = r.finish(); !e.empty()) return e;
    std::string e = diff("value", value, out["value"].i64()) + diff("ticks_left", book.ticks_left, out["ticks_left"].i64()) +
                    diff("last_tick", w.last_tick, out["last_tick"].u32()) + diff("mem_gem", book.gem, out["mem_gem"].i64()) +
                    diff("mem_spell", book.spell, out["mem_spell"].i64()) +
                    diff("item_pending", w.item_pending, out["item_pending"].i64());
    bool notes_vf94 = !in["notes"].items.empty();
    if (notes_vf94 != q.vf94) e += " cursor vf94";
    const Json& p = out["packet"];
    if (p.is_null() != !q.packet) return e + " packet presence";
    if (q.packet)
        e += diff("packet slot", q.packet->slot, p["slot"].i64()) + diff("packet spell", q.packet->spell_id, p["spell_id"].i64()) +
             diff("packet scribing", q.packet->scribing, p["scribing"].i64()) +
             diff("opcode", gems::kOpMemorizeSpell, p["opcode"].i64());
    return e;
}

class FinishRecord final : public gems::FinishQueries {
public:
    explicit FinishRecord(const Json& in) : in_(in) {}
    void set_memorized(std::uint32_t, std::int32_t spell) override { memorized = spell; }
    void say(std::uint32_t eqstr, std::int32_t, std::uint32_t color) override {
        calls.push_back("build_string " + std::to_string(eqstr));
        calls.push_back("dsp_chat " + std::to_string(color));
    }
    std::int32_t recast_time(std::int32_t) override { return in_["recast_time"].i32(); }
    void set_gem_timer(std::uint32_t, std::uint32_t ready) override { timer = ready; }
    void hide_book() override { hidden = true; }
    void continue_set_mem() override { calls.push_back("continue_set_mem"); }
    bool cast_window() override { return in_["cast_window"].truthy(); }
    void update_spell_gems(std::int32_t gem) override { calls.push_back("cast_window_update_gems " + std::to_string(gem)); }
    std::int32_t memorized = 0;
    std::uint32_t timer = 0;
    bool hidden = false;
    std::vector<std::string> calls;

private:
    const Json& in_;
};

std::string check_finish(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    FinishRecord q(in);
    std::int32_t mode = in["mode"].i32();
    gems::finish_memorizing(mode, in["keep_open"].truthy(), in["gem"].u32(), in["spell_id"].i32(), in["now"].u32(), q);
    std::vector<std::string> want;
    for (const Json& c : out["calls"].items) {
        std::string s = c["fn"].text;
        if (c.has("eqstr")) s += " " + std::to_string(c["eqstr"].i64());
        if (c.has("color")) s += " " + std::to_string(c["color"].i64());
        if (c.has("gem")) s += " " + std::to_string(c["gem"].i64());
        want.push_back(s);
    }
    std::string e = diff("gem_timer", q.timer, out["gem_timer"].u32()) + diff("memorized", q.memorized, out["memorized"].i64()) +
                    diff("mode", mode, out["mode"].i64()) + diff("book_hidden", q.hidden, out["book_hidden"].truthy());
    if (want != q.calls) e += " calls";
    return e;
}

class HandlerReplay final : public gems::MemorizeHandlerQueries {
public:
    HandlerReplay(Replay& r, const Json& in) : r_(r), in_(in) {
        item_ready = in["item_ready_time"].u32();
        recovery = in["recovery_timer"].u32();
        if (in.has("gem_timer")) gem_timer = in["gem_timer"].u32();
    }
    void pop_pending_reason() override { r_.next("pop_pending_reason"); }
    bool book_window() override { return in_["book_window"].truthy(); }
    bool cast_window() override { return in_["cast_window"].truthy(); }
    void finish_scribing(std::int32_t slot, std::int32_t spell) override {
        r_.expect("finish_scribing", {{"book_slot", slot}, {"spell_id", spell}});
    }
    void finish_memorizing(std::int32_t gem, std::int32_t spell) override {
        r_.expect("finish_memorizing", {{"gem", gem}, {"spell_id", spell}});
    }
    void forget_memorized_spell(std::int32_t gem) override { r_.expect("forget_memorized_spell", {{"gem", gem}}); }
    bool local_pc() override { return true; }
    bool local_player() override { return true; }
    void set_memorized_spell(std::int32_t gem, std::int32_t spell) override {
        r_.expect("set_memorized_spell", {{"gem", gem}, {"spell_id", spell}});
    }
    bool is_cast_item_valid() override { return r_.ret(r_.next("is_cast_item_valid")) != 0; }
    void get_cast_item() override { r_.next("get_cast_item"); }
    std::int32_t item_cast_type() override { return in_["item_cast_type"].i32(); }
    std::int32_t item_recast_time(std::int32_t index) override {
        return static_cast<std::int32_t>(r_.ret(r_.expect("item_recast_time", {{"index", index}})));
    }
    std::uint32_t item_ready_time() override { return item_ready; }
    void set_item_ready_time(std::uint32_t ready, bool changed) override {
        item_ready = ready;
        if (changed) dirty_set = true;
    }
    bool is_valid_spell_id(std::int32_t id) override {
        return r_.ret(r_.expect("is_valid_spell_id", {{"spell_id", id}})) != 0;
    }
    std::optional<std::pair<std::int32_t, std::int32_t>> spell_times(std::int32_t) override {
        if (in_["recast_time"].is_null()) return std::nullopt;
        return std::make_pair(in_["recast_time"].i32(), in_["recovery_time"].i32());
    }
    std::int32_t total_effect(std::int32_t spa) override {
        return static_cast<std::int32_t>(r_.ret(r_.expect("total_effect", {{"spa", spa}, {"a", 1}, {"b", 0}, {"c", 1}, {"d", 1}})));
    }
    void set_gem_timer(std::int32_t gem, std::uint32_t ready) override {
        if (gem == in_["slot"].i32()) gem_timer = ready;
        else wrong_gem = true;
    }
    void set_cast_slot(std::uint8_t s) override { cast_slot = s; }
    void set_recovery(std::uint32_t ready) override { recovery = ready; }
    std::uint8_t class_id() override { return static_cast<std::uint8_t>(in_["class"].i64()); }
    std::int32_t next_bard_twist_gem() override { return static_cast<std::int32_t>(r_.ret(r_.next("next_bard_twist_gem"))); }
    void stop_current_spell_cast(std::int32_t reason) override {
        r_.expect("stop_current_spell_cast", {{"reason", reason}});
    }
    void cast_gem(std::int32_t gem) override { r_.expect("cast_gem", {{"gem", gem}}); }

    std::uint32_t item_ready = 0, recovery = 0;
    std::optional<std::uint32_t> gem_timer;
    std::optional<std::uint8_t> cast_slot;
    bool dirty_set = false, wrong_gem = false;

private:
    Replay& r_;
    const Json& in_;
};

std::string check_handler(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    Replay r(in["calls"]);
    HandlerReplay q(r, in);
    std::int32_t pending = in["item_pending"].i32();
    std::int32_t v = gems::handle_op_memorize_spell(in["scribing"].i32(), in["slot"].i32(), in["spell_id"].i32(),
                                                    in["reduction"].i32(), in["now"].u32(), pending, q);
    if (auto e = r.finish(); !e.empty()) return e;
    std::string e = diff("value", v, out["value"].i64()) + diff("recovery_timer", q.recovery, out["recovery_timer"].u32()) +
                    diff("item_ready_time", q.item_ready, out["item_ready_time"].u32()) +
                    diff("item_pending", pending, out["item_pending"].i64());
    if (out.has("gem_timer")) e += diff("gem_timer", q.gem_timer.value_or(0), out["gem_timer"].u32());
    else if (q.gem_timer && !in.has("gem_timer") && q.wrong_gem) e += " gem outside the record";
    if (q.cast_slot) e += diff("cast_slot", *q.cast_slot, out["cast_slot"].i64());
    if (q.dirty_set) e += diff("item_dirty", 1, out["item_dirty"].i64());
    return e;
}

std::string check_linked(const Json& rec) {
    const Json& in = rec["in"];
    Replay r(in["calls"]);
    std::optional<std::int32_t> index;
    if (!in["reuse_timer_index"].is_null()) index = in["reuse_timer_index"].i32();
    bool ready = gems::is_linked_spell_timer_ready(index, in["now"].u32(), [&](std::int32_t i) {
        return static_cast<std::uint32_t>(r.ret(r.expect("linked_timer", {{"index", i}})));
    });
    if (auto e = r.finish(); !e.empty()) return e;
    return diff("ready", ready, rec["out"]["ready"].truthy());
}

std::string check_stacking(const Json& rec) {
    std::int32_t spa = rec["in"]["spa"].i32();
    return diff("stacking", affect::is_spa_stacking(spa), rec["out"]["stacking"].truthy()) +
           diff("ignored", affect::is_spa_ignored_by_stacking(spa), rec["out"]["ignored"].truthy());
}

std::string check_slot(const Json& rec) {
    const Json& in = rec["in"];
    Replay r(in["calls"]);
    auto cached = [&](const char* fn) {
        return [&r, fn] { return static_cast<std::int32_t>(r.ret(r.expect(fn, {{"spa", gems::kSpaSpellSlots}, {"b", 0}}))); };
    };
    gems::SpellSlotTotals t{cached("cached_aa_effect"), cached("cached_buff_effect"), cached("cached_item_effect")};
    bool usable = gems::can_use_memorized_spell_slot(in["profile"].truthy(), in["profile_340c"].i32(), in["gem"].i32(), t);
    if (auto e = r.finish(); !e.empty()) return e;
    return diff("usable", usable, rec["out"]["usable"].truthy());
}

// ---------------------------------------------------------------- FindAffectSlot

struct AffectWorld final : affect::AffectQueries {
    explicit AffectWorld(const Json& in) : in_(in), replay(in["calls"]), ch(sheet, tables, lookup()) {
        const Json& t = in["target"];
        target_spawn.type = static_cast<std::uint8_t>(t["type"].i64());
        target_spawn.flag_208 = t["mercenary"].truthy() ? 1 : 0;
        target_spawn.owner_38c = t["flag_38c"].truthy() ? 1 : 0;
        self.cache_valid = 1;
        self.spawn = t["spawn"].truthy() ? &target_spawn : nullptr;
        if (!in["caster"].is_null()) {
            const Json& c = in["caster"];
            caster.type = static_cast<std::uint8_t>(c["type"].i64());
            caster.spawn_id = c["id"].u32();
            caster.class_id = static_cast<std::uint8_t>(c["class"].i64());
            caster_pc.cache_valid = 1;
            caster_pc.spawn = &caster_pc_spawn; // a player (+0x125 0)
            caster.character = c["has_pc"].truthy() ? &caster_pc : nullptr;
        }
        for (const Json& b : in["buffs"].items) {
            auto s = static_cast<std::size_t>(b["slot"].i64());
            hit::Affect& a = affects[s];
            a.type = 1; // any non-zero Type
            a.level = static_cast<std::uint8_t>(b["level"].i64());
            a.spell_id = b["spell_id"].i32();
            for (std::size_t k = 0; k < 12 && k < b["slot_data"].items.size(); ++k) a.slot_data[k] = b["slot_data"][k].i32();
            caster_ids[s] = b["caster_id"].u32();
            spawn_kind[caster_ids[s]] = b["caster_spawn"].text;
        }
        for (const Json& sp : in["spells"].items) {
            std::int32_t id = sp["id"].i32();
            hit::Spell& h = spells[id];
            hailstone::game::SpellData& d = data[id];
            h.id = id;
            d.id = static_cast<std::uint32_t>(id);
            h.attrib.fill(0xfe);
            d.effect.fill(0xfe);
            for (std::size_t k = 0; k < sp["effects"].items.size() && k < 12; ++k) {
                const Json& e = sp["effects"][k];
                h.attrib[k] = d.effect[k] = e[0].i32();
                h.base[k] = d.base[k] = e[1].i32();
                h.max[k] = d.max[k] = e[2].i32();
                h.calc[k] = d.formula[k] = e[3].i32();
            }
            h.spell_type = d.spell_type = static_cast<std::uint8_t>(sp["spell_type"].i64());
            h.class_level[8] = static_cast<std::uint8_t>(sp["bard_level"].i64());
            h.duration_formula = d.duration_formula = sp["duration_type"].u32();
            h.short_buff = sp["short_buff"].truthy() ? 1 : 0;
            h.not_stackable_dot = sp["not_stackable_dot"].truthy() ? 1 : 0;
            h.no_dispell = sp["no_dispell"].truthy() ? 1 : 0;
            h.is_skill = sp["is_skill"].truthy() ? 1 : 0;
            h.stacks_with_self = sp["stacks_with_self"].truthy() ? 1 : 0;
            h.no_remove = sp["no_remove"].truthy() ? 1 : 0;
        }
        for (std::size_t s = 0; s < affects.size(); ++s) before[s] = affects[s];
    }
    SpellLookup lookup() {
        return [this](std::int32_t id) -> const hailstone::game::SpellData* {
            auto it = data.find(id);
            return it == data.end() ? nullptr : &it->second;
        };
    }
    const hit::Spell* spell_by_id(std::int32_t id) override {
        auto it = spells.find(id);
        return it == spells.end() ? nullptr : &it->second;
    }
    hit::Affect* effect_at(std::int32_t slot) override { return &affects[static_cast<std::size_t>(slot)]; }
    std::uint32_t buff_caster_id(std::int32_t slot) override { return caster_ids[static_cast<std::size_t>(slot)]; }
    std::uint8_t get_level(hit::Spawn*) override { return static_cast<std::uint8_t>(replay.ret(replay.next("caster_level"))); }
    void recache_spell_effects(hit::Character*) override { replay.error = "ReCacheSpellEffects called"; }
    std::int32_t cached(const char* fn, std::int32_t spa) {
        return static_cast<std::int32_t>(replay.ret(replay.expect(fn, {{"spa", spa}, {"b", 0}})));
    }
    std::int32_t cached_buff_effect(hit::Character*, std::int32_t spa) override { return cached("cached_buff_effect", spa); }
    std::int32_t cached_aa_effect(hit::Character*, std::int32_t spa) override { return cached("cached_aa_effect", spa); }
    std::int32_t cached_item_effect(hit::Character*, std::int32_t spa) override { return cached("cached_item_effect", spa); }
    std::uint32_t extra_buff_slots() override { return in_["target"]["extra_buff_slots"].u32(); }
    std::uint8_t spawn_vf_dc(hit::Spawn*) override { return in_["target"]["no_drop"].truthy() ? 1 : 0; }
    hit::Spawn* spawn_by_id(std::uint32_t id) override {
        auto it = spawn_kind.find(id);
        if (it == spawn_kind.end()) {
            replay.error = "GetSpawnByID of an unknown caster id";
            return nullptr;
        }
        if (it->second == "self") return &target_spawn;
        if (it->second == "other") return &other;
        return nullptr;
    }
    void remove_affect(hit::Affect* a) override { removed.push_back(static_cast<int>(a - affects.data())); }
    std::int32_t calc_affect_change(const hit::Spell* s, std::uint32_t level, std::uint32_t slot, std::int32_t) override {
        const hailstone::game::SpellData& d = data.at(s->id);
        auto k = static_cast<std::uint8_t>(slot);
        return ch.calc_affect_change(d, level, k, nullptr, k < 12 ? d.base[k] : 0, true);
    }

    const Json& in_;
    Replay replay;
    CharacterSheet sheet;
    GameTables tables;
    std::map<std::int32_t, hit::Spell> spells;
    std::map<std::int32_t, hailstone::game::SpellData> data;
    Character ch;
    hit::Character self, caster_pc;
    hit::Spawn target_spawn, caster, caster_pc_spawn, other;
    std::array<hit::Affect, affect::kAffectSlots> affects{}, before{};
    std::array<std::uint32_t, affect::kAffectSlots> caster_ids{};
    std::map<std::uint32_t, std::string> spawn_kind;
    std::vector<int> removed;
};

bool same(const hit::Affect& a, const hit::Affect& b) {
    return a.type == b.type && a.level == b.level && a.byte_2 == b.byte_2 && a.modifier == b.modifier &&
           a.spell_id == b.spell_id && a.duration == b.duration && a.slot_data == b.slot_data;
}

std::string check_find_affect_slot(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    auto w = std::make_unique<AffectWorld>(in);
    std::int32_t slot = 0x5a5a5a5a;
    hit::Spawn* caster = in["caster"].is_null() ? nullptr : &w->caster;
    hit::Affect* r = affect::find_affect_slot(&w->self, in["spell_id"].i32(), caster, &slot,
                                              in["keep_existing"].truthy() ? 1u : 0u, in["caster_level"].u32(), *w);
    if (auto e = w->replay.finish(); !e.empty()) return e;
    std::string e = diff("slot", slot, out["slot"].i64());
    if (out["effect"].is_null() != (r == nullptr)) e += " effect presence";
    else if (r) e += diff("effect", r - w->affects.data(), out["effect"].i64());
    std::vector<int> removed;
    for (const Json& n : out["removed"].items) {
        removed.push_back(n["slot"].i32());
        if (n["b"].i64() != 0 || n["c"].i64() != 0) e += " RemoveAffect arguments";
    }
    if (removed != w->removed) e += " removed";
    std::vector<int> reset, want_reset;
    for (std::size_t s = 0; s < w->affects.size(); ++s)
        if (!same(w->affects[s], w->before[s])) reset.push_back(static_cast<int>(s));
    for (const Json& n : out["reset_buffs"].items) want_reset.push_back(n.i32());
    if (reset != want_reset) e += " reset buffs";
    return e;
}

} // namespace

TEST_CASE("vectors: CSpellBookWnd::GetSpellMemTicksLeft") {
    hailstone::test::each_vector("CSpellBookWnd_GetSpellMemTicksLeft.jsonl", check_ticks);
}
TEST_CASE("vectors: CSpellBookWnd::FinishMemorizing") {
    hailstone::test::each_vector("CSpellBookWnd_FinishMemorizing.jsonl", check_finish);
}
TEST_CASE("vectors: Handle_OP_MemorizeSpell") {
    hailstone::test::each_vector("Handle_OP_MemorizeSpell.jsonl", check_handler);
}
TEST_CASE("vectors: PcZoneClient_IsLinkedSpellTimerReady") {
    hailstone::test::each_vector("PcZoneClient_IsLinkedSpellTimerReady.jsonl", check_linked);
}
TEST_CASE("vectors: EQ_Spell stacking SPA tables") {
    hailstone::test::each_vector("EQ_Spell_StackingSpaTables.jsonl", check_stacking);
}
TEST_CASE("vectors: CharacterZoneClient::CanUseMemorizedSpellSlot") {
    hailstone::test::each_vector("CharacterZoneClient_CanUseMemorizedSpellSlot.jsonl", check_slot);
}
TEST_CASE("vectors: CharacterZoneClient::FindAffectSlot (real stacking rules)") {
    hailstone::test::each_vector("CharacterZoneClient_FindAffectSlot.jsonl", check_find_affect_slot);
}
