// The hailstone module: a Lua script as a plugin (docs/plugins.md "Lua plugins"). What a compiled plugin does
// through sdk/nmsc_plugin.h, a script does through this table, held to the same capabilities:
//   extend:  hailstone.hooks{...} (the core's questions, as NmscHooks), set_active, set_flag, set_player_classes,
//            set_character_classes, set_pet_list, set_vitals
//   packets: hailstone.on_packet(stream, direction, opcode, fn), hailstone.send_packet(stream, opcode, data)
//   ui:      hailstone.ui_function(name, fn), hailstone.option{...}, hailstone.menu(label, command), and the
//            script folder's ui/<pack>/ windows
//   read:    hailstone.player(), spawn(id), spawns(), spell(id), item(slot), buff(i), xtarget(i), eqstr(id, ...),
//            zone_name(id), autoskill_list(), alt_abilities()
//   actions: hailstone.right_click_item(slot [, class]), close_spell_book(), clear_spell_recovery(),
//            set_window_pet(id), inspect_item(id [, augments]), set_max_camera_distance(d), hold_autoskill(on),
//            use_ability(kind, name), train_aa(group)
//   always:  hailstone.chat(text [, color]), server_dir(), server_rule(section, key), character_dir(),
//            max_camera_distance()
// The core calls the script's functions on the script's own state, outside its coroutine, with its capabilities and
// within kHookMs; while the script itself is running (a call back into it) they give the stock answer. An error in
// one ends the script, as an error in its main chunk does. A script that gave the client any of these stays after its
// main chunk returns, until it is stopped.
#include <algorithm>
#include <cctype>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "game/chat_text.h"
#include "game/item_links.h"
#include "game/server_ext_chain.h"
#include "game/spells.h"
#include "game/zone_names.h"
#include "mq/lua_script.h"
#include "net/game_connection.h"
#include "net/social_packets.h"
#include "plugin/lua_value.h"
#include "plugin/policy.h"
#include "plugin/ui_bridge.h"

#define NMSC_HOST
#include "nmsc_plugin.h"

namespace nmsc::mq {

namespace {

constexpr std::int64_t kHookMs = 50; // a hook, packet or UI function call runs to its end within this

void runaway_hook(lua_State* L, lua_Debug*) {
    Script& s = Script::of(L);
    auto ran = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s.slice_start).count();
    if (ran >= s.runaway_ms) luaL_error(L, "ran longer than %d ms", static_cast<int>(s.runaway_ms));
}

// The script can be called now: running, not stopping, and not inside its own code.
bool usable(const Script& s) {
    return s.L && !s.stop_requested && s.busy == 0 && s.status == Script::Status::Running;
}

// Calls the function under its `nargs` arguments on the script's main thread, with its capabilities, within kHookMs.
// False (nothing left on the stack, and the script ends) on an error.
bool call_script(Script& s, int nargs, int nres) {
    lua_State* L = s.L;
    ++s.busy;
    plugin::CallerScope scope(s.caps, s.name);
    const std::int64_t runaway = s.runaway_ms;
    const Clock::time_point start = s.slice_start;
    s.runaway_ms = kHookMs;
    s.slice_start = Clock::now();
    lua_sethook(L, runaway_hook, LUA_MASKCOUNT, 1000);
    const int st = lua_pcall(L, nargs, nres, 0);
    lua_sethook(L, nullptr, 0, 0);
    s.runaway_ms = runaway;
    s.slice_start = start;
    --s.busy;
    if (st == LUA_OK) return true;
    const char* e = lua_tostring(L, -1);
    std::string why = e ? e : "error";
    lua_pop(L, 1);
    s.fail(why);
    return false;
}

void need_cap(lua_State* L, std::uint32_t cap, const char* what) { need_caps(L, cap, what); }

net::GameConnection* writable(Script& s) { return s.host->env.connection ? s.host->env.connection() : nullptr; }
const net::GameConnection* readable(Script& s) {
    return s.host->env.context ? s.host->env.context().conn : nullptr;
}

void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_num(lua_State* L, const char* k, lua_Number v) {
    lua_pushnumber(L, v);
    lua_setfield(L, -2, k);
}
void set_str(lua_State* L, const char* k, const std::string& v) {
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v) {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}

// An item as a table. With the connection (an item the player carries), also how long its click's recast still has
// to go.
void push_item(lua_State* L, const net::Item& it, const net::GameConnection* c = nullptr) {
    lua_createtable(L, 0, 22);
    set_int(L, "id", it.id);
    set_str(L, "name", it.name);
    set_int(L, "type", it.item_type);
    set_int(L, "item_class", it.item_class);
    set_int(L, "classes", it.classes);
    set_int(L, "races", it.races);
    set_int(L, "icon", it.icon);
    set_int(L, "stack", it.stack_count);
    set_int(L, "charges", it.charges);
    auto spell = [](std::int32_t id) { return id > 0 ? id : 0; };
    set_int(L, "click_spell", spell(it.effects[net::kEffectClick].effect));
    set_int(L, "scroll_spell", spell(it.effects[net::kEffectScroll].effect));
    set_int(L, "req_level", it.req_level);
    set_int(L, "bag_slots", it.is_bag() ? it.bag_slots : 0);
    // The click effect (block 0): its type (as the item record carries it), level, cast time (ms), recast (s), the
    // recast's timer group and the charges it starts with (-1: unlimited).
    const net::ItemEffect& click = it.effects[net::kEffectClick];
    if (!click.present()) return;
    set_int(L, "click_type", click.type);
    set_int(L, "click_level", click.level);
    set_int(L, "click_cast_time", click.cast_time());
    set_int(L, "click_recast", static_cast<std::int32_t>(click.recast_delay()));
    set_int(L, "click_recast_type", click.recast_type());
    set_int(L, "click_max_charges", click.max_charges());
    if (c) {
        // The later of the item's own recast and its group's (GameConnection::item_recast_timer, time(0) seconds).
        const auto ready_at = static_cast<std::int64_t>(c->item_recast_timer(&it));
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        set_int(L, "click_ready_in", ready_at > now ? ready_at - now : 0);
    }
}

void push_spawn(lua_State* L, const game::Spawn& s) {
    lua_createtable(L, 0, 18);
    set_int(L, "id", s.id);
    set_str(L, "name", s.name);
    set_int(L, "level", s.level);
    set_int(L, "class", s.class_id);
    set_int(L, "race", s.race);
    set_int(L, "owner", s.pet_owner_id);
    set_int(L, "guild", s.guild_id);
    set_bool(L, "npc", s.is_npc);
    set_bool(L, "corpse", s.is_corpse);
    set_num(L, "x", s.position.x);
    set_num(L, "y", s.position.y);
    set_num(L, "z", s.position.z);
    set_num(L, "heading", s.heading);
    set_int(L, "hp_percent", s.hp_percent);
    set_int(L, "rider", s.rider_id);
    set_bool(L, "mercenary", s.mercenary);
    set_bool(L, "gm", s.gm);
    set_int(L, "body_type", s.properties.empty() ? 0 : s.properties[0]);
}

void push_slot(lua_State* L, const net::ItemSlot& slot) {
    lua_createtable(L, 0, 4);
    set_int(L, "type", slot.type);
    set_int(L, "slot", slot.slot);
    set_int(L, "sub", slot.sub);
    set_int(L, "aug", slot.aug);
}

} // namespace

// The script's hook table, switches and vitals as one server extension (game/server_ext.h), a member of the chain.
// Detached when the script goes: the chain may keep it a little longer, and the item display its level line.
class LuaExtension final : public game::ServerExtension {
public:
    explicit LuaExtension(Script& s) : s_(&s) {
        rules_.ctx = this;
        rules_.gate = [](const void* ctx, std::int32_t skill, std::uint32_t mask,
                         std::int32_t profile_skill) -> std::optional<bool> {
            const auto& x = *static_cast<const LuaExtension*>(ctx);
            if (!x.hook("skill_gate")) return std::nullopt;
            lua_pushinteger(x.L(), skill);
            lua_pushinteger(x.L(), mask);
            lua_pushinteger(x.L(), profile_skill);
            return x.run(3) ? x.bool_result() : std::nullopt;
        };
        rules_.spoof_class = [](const void* ctx, std::int32_t skill, std::uint32_t mask) -> std::uint32_t {
            const auto& x = *static_cast<const LuaExtension*>(ctx);
            if (!x.hook("skill_spoof_class")) return 0;
            lua_pushinteger(x.L(), skill);
            lua_pushinteger(x.L(), mask);
            return x.run(2) ? static_cast<std::uint32_t>(x.int_result().value_or(0)) : 0;
        };
        rules_.tracking_allowed = [](const void* ctx, std::uint8_t class_id, std::uint32_t mask, std::uint32_t skill) {
            const auto& x = *static_cast<const LuaExtension*>(ctx);
            if (x.hook("tracking_allowed")) {
                lua_pushinteger(x.L(), class_id);
                lua_pushinteger(x.L(), mask);
                lua_pushinteger(x.L(), skill);
                if (x.run(3))
                    if (auto r = x.bool_result()) return *r;
            }
            return game::tracking_allowed()(class_id, mask, skill);
        };
    }
    void detach() { s_ = nullptr; }
    // Active while the script says so and still has the 'extend' capability (the server's rules may change).
    bool active() const {
        return s_ && s_->active && !s_->stop_requested && (s_->caps & plugin::kCapExtend) == plugin::kCapExtend;
    }
    const char* name() const override { return s_ ? s_->name.c_str() : "stopped script"; }

    bool retry_zone_entry(net::GameConnection&) override {
        return hook("zone_entry_retry") && run(0) && bool_result().value_or(false);
    }
    bool handler_command(net::GameConnection&, std::string_view handler, const std::string& args) override {
        if (!hook("game_command")) return false;
        lua_pushlstring(L(), handler.data(), handler.size());
        lua_pushlstring(L(), args.data(), args.size());
        return run(2) && bool_result().value_or(false);
    }
    std::optional<std::vector<std::string>> who_lines(const net::GameConnection&,
                                                      const net::WhoResponse& r) const override {
        if (!hook("who_lines")) return std::nullopt;
        lua_State* l = L();
        lua_createtable(l, 0, 6);
        lua_createtable(l, static_cast<int>(r.entries.size()), 0);
        for (std::size_t i = 0; i < r.entries.size(); ++i) {
            const auto& e = r.entries[i];
            lua_createtable(l, 0, 9);
            set_str(l, "name", e.name);
            set_str(l, "guild", e.guild);
            set_str(l, "zone_name", e.zone_name);
            set_str(l, "account", e.account);
            set_int(l, "flags", e.flags);
            set_int(l, "class", e.class_);
            set_int(l, "level", e.level);
            set_int(l, "race", e.race);
            set_int(l, "zone_id", e.zone_id);
            lua_rawseti(l, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_setfield(l, -2, "entries");
        set_str(l, "line", r.line);
        set_int(l, "title_string", r.title_string);
        set_int(l, "footer_string", r.footer_string);
        set_int(l, "footer_count", r.footer_count);
        if (!run(1)) return std::nullopt;
        std::optional<std::vector<std::string>> out;
        if (lua_istable(l, -1)) {
            out.emplace();
            const lua_Unsigned n = lua_rawlen(l, -1);
            for (lua_Unsigned i = 1; i <= n; ++i) {
                lua_rawgeti(l, -1, static_cast<lua_Integer>(i));
                const char* t = lua_tostring(l, -1);
                out->emplace_back(t ? t : "");
                lua_pop(l, 1);
            }
        }
        lua_pop(l, 1);
        return out;
    }

    std::uint8_t cast_class(const net::GameConnection&) const override {
        if (!hook("cast_class") || !run(0)) return 0;
        return static_cast<std::uint8_t>(std::clamp<lua_Integer>(int_result().value_or(0), 0, 255));
    }
    std::optional<bool> is_spellcaster(const net::GameConnection&) const override {
        return hook("is_spellcaster") && run(0) ? bool_result() : std::nullopt;
    }
    std::optional<std::uint8_t> spell_level_needed(const net::GameConnection&, const game::SpellData& s,
                                                   std::uint32_t class_id) const override {
        if (!hook("spell_level_needed")) return std::nullopt;
        lua_pushinteger(L(), s.id);
        lua_pushinteger(L(), class_id);
        if (!run(2)) return std::nullopt;
        auto r = int_result();
        if (!r) return std::nullopt;
        return static_cast<std::uint8_t>(std::clamp<lua_Integer>(*r, 0, 255));
    }
    bool song_blocked(const net::GameConnection&, std::uint8_t raw_bard_level) const override {
        if (!hook("song_blocked")) return false;
        lua_pushinteger(L(), raw_bard_level);
        return run(1) && bool_result().value_or(false);
    }
    bool own_begin_cast_line(const net::GameConnection&, const game::SpellData& s) const override {
        if (!hook("own_begin_cast_line")) return false;
        lua_pushinteger(L(), s.id);
        return run(1) && bool_result().value_or(false);
    }
    std::optional<bool> can_use_gem(const net::GameConnection&, int gem) const override {
        if (!hook("can_use_gem")) return std::nullopt;
        lua_pushinteger(L(), gem);
        return run(1) ? bool_result() : std::nullopt;
    }
    std::int32_t combat_ability_timer(const net::GameConnection& c, const game::SpellData& s) const override {
        if (hook("ability_timer_id")) {
            lua_pushinteger(L(), s.id);
            if (run(1))
                if (auto r = int_result()) return static_cast<std::int32_t>(*r);
        }
        return ServerExtension::combat_ability_timer(c, s);
    }
    std::optional<std::uint32_t> discipline_reuse_timer(const net::GameConnection&, std::int32_t id) const override {
        if (!hook("discipline_reuse")) return std::nullopt;
        lua_pushinteger(L(), id);
        if (!run(1)) return std::nullopt;
        auto r = int_result();
        return r ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(*r)) : std::nullopt;
    }
    std::optional<bool> before_use_skill(net::GameConnection&, std::uint32_t skill) override {
        if (!hook("before_use_skill")) return std::nullopt;
        lua_pushinteger(L(), skill);
        return run(1) ? bool_result() : std::nullopt;
    }
    std::optional<bool> before_combat_ability(net::GameConnection&, const game::SpellData& s) override {
        if (!hook("before_combat_ability")) return std::nullopt;
        lua_pushinteger(L(), s.id);
        return run(1) ? bool_result() : std::nullopt;
    }
    void after_combat_ability(net::GameConnection&, const game::SpellData& s, bool used) override {
        if (!hook("after_combat_ability")) return;
        lua_pushinteger(L(), s.id);
        lua_pushboolean(L(), used);
        if (run(1)) lua_pop(L(), 1);
    }
    std::optional<std::string> combat_effect_label(const net::GameConnection&) const override {
        return hook("combat_effect_label") && run(0) ? string_result() : std::nullopt;
    }
    // map_spawn(spawn): falsy hides it, true shows it as the defaults say, a table changes them (color, layer, label).
    std::optional<game::MapSpawnMark> map_spawn(const net::GameConnection&, const game::Spawn& s,
                                                const game::MapSpawnMark& defaults) const override {
        if (!hook("map_spawn")) return std::nullopt;
        push_spawn(L(), s);
        if (!run(1)) return std::nullopt;
        lua_State* l = L();
        std::optional<game::MapSpawnMark> out;
        if (lua_istable(l, -1)) {
            game::MapSpawnMark m = defaults;
            if (lua_getfield(l, -1, "color") == LUA_TNUMBER) m.rgb = static_cast<std::uint32_t>(lua_tointeger(l, -1)) & 0xffffffu;
            lua_pop(l, 1);
            if (lua_getfield(l, -1, "layer") == LUA_TNUMBER) m.layer = static_cast<int>(std::clamp<lua_Integer>(lua_tointeger(l, -1), 0, 3));
            lua_pop(l, 1);
            if (lua_getfield(l, -1, "label") == LUA_TSTRING) m.label = lua_tostring(l, -1);
            lua_pop(l, 1);
            out = std::move(m);
        } else if (lua_toboolean(l, -1)) {
            out = defaults;
        }
        lua_pop(l, 1);
        return out;
    }
    bool reuse_focus_dropped(const net::GameConnection&, const game::SpellData& s) const override {
        if (!hook("reuse_focus_dropped")) return false;
        lua_pushinteger(L(), s.id);
        return run(1) && bool_result().value_or(false);
    }
    bool can_use_all_items(const net::GameConnection&) const override { return flag(NMSC_FLAG_ITEMS_ANY_CLASS); }
    bool wide_item_links(const net::GameConnection&) const override { return flag(NMSC_FLAG_WIDE_ITEM_LINKS); }
    std::uint32_t guild_member_classes(const net::GameConnection&, std::uint32_t class_value) const override {
        if (!hook("guild_member_classes")) return 0;
        lua_pushinteger(L(), class_value);
        return run(1) ? static_cast<std::uint32_t>(int_result().value_or(0)) : 0;
    }
    std::uint8_t hide_check_class(const net::GameConnection&, std::uint8_t class_id) const override {
        return flag(NMSC_FLAG_HIDE_ANY_CLASS) ? std::uint8_t{9} : class_id; // 9 Rogue: sneaking keeps Hide
    }
    bool aa_any_level(const net::GameConnection&) const override { return flag(NMSC_FLAG_AA_ANY_LEVEL); }
    bool aa_train_all(const net::GameConnection&) const override { return flag(NMSC_FLAG_AA_TRAIN_ALL); }
    bool augment_classes(const net::GameConnection&) const override { return flag(NMSC_FLAG_AUGMENT_CLASSES); }
    bool research_classes(const net::GameConnection&) const override { return flag(NMSC_FLAG_RESEARCH_CLASSES); }
    bool focus_classes(const net::GameConnection&) const override { return flag(NMSC_FLAG_FOCUS_CLASSES); }
    bool backstab_either_hand(const net::GameConnection&) const override {
        return flag(NMSC_FLAG_BACKSTAB_EITHER_HAND);
    }
    std::vector<std::uint8_t> max_vitals_classes(const net::GameConnection&, std::uint8_t class_id) const override {
        std::uint32_t mask = 0;
        if (hook("vitals_classes")) {
            lua_pushinteger(L(), class_id);
            if (run(1)) mask = static_cast<std::uint32_t>(int_result().value_or(0));
        }
        if (!mask) return {class_id};
        std::vector<std::uint8_t> out;
        for (std::uint8_t k = 1; k <= 16; ++k)
            if ((mask >> (k - 1)) & 1u) out.push_back(k);
        return out;
    }
    game::CharacterVitals character_vitals(const net::GameConnection&) const override {
        return s_ && s_->vitals ? *s_->vitals : game::CharacterVitals{};
    }
    bool item_right_click(net::GameConnection&, const net::ItemSlot& slot) override {
        if (!hook("item_right_click")) return false;
        push_slot(L(), slot);
        return run(1) && bool_result().value_or(false);
    }
    const game::SkillRules* skill_rules(const net::GameConnection&) const override {
        return has("skill_gate") || has("skill_spoof_class") || has("tracking_allowed") ? &rules_ : nullptr;
    }
    game::LevelNeededLine level_needed_line(const net::GameConnection&) const override {
        if (!has("item_level_line")) return {};
        std::shared_ptr<LuaExtension> self = s_->extension; // kept by the item display: detached when the script goes
        return [self](const game::SpellData& s) -> std::optional<std::string> {
            if (!self->hook("item_level_line")) return std::nullopt;
            lua_pushinteger(self->L(), s.id);
            return self->run(1) ? self->string_result() : std::nullopt;
        };
    }
    std::optional<std::string> merchant_level(const net::GameConnection&, const net::Item& item) const override {
        if (!hook("list_level")) return std::nullopt;
        push_item(L(), item);
        return run(1) ? string_result() : std::nullopt;
    }
    void label_text(const net::GameConnection&, int id, std::string& text) const override {
        if (!hook("label")) return;
        lua_pushinteger(L(), id);
        lua_pushlstring(L(), text.data(), text.size());
        if (!run(2)) return;
        if (auto t = string_result()) text = *t;
    }
    bool hide_xtarget_slot(const net::GameConnection&, int slot) const override {
        if (!hook("hide_xtarget_slot")) return false;
        lua_pushinteger(L(), slot);
        return run(1) && bool_result().value_or(false);
    }

private:
    lua_State* L() const { return s_->L; }
    bool flag(std::uint32_t f) const { return s_ && (s_->flags & f) != 0; }
    // Pushes hooks[name] when it is a function and the script can be called now.
    bool hook(const char* name) const {
        if (!s_ || !usable(*s_) || s_->hooks_ref == LUA_NOREF) return false;
        lua_State* l = s_->L;
        lua_rawgeti(l, LUA_REGISTRYINDEX, s_->hooks_ref);
        lua_getfield(l, -1, name);
        lua_remove(l, -2);
        if (lua_isfunction(l, -1)) return true;
        lua_pop(l, 1);
        return false;
    }
    bool has(const char* name) const {
        if (!hook(name)) return false;
        lua_pop(s_->L, 1);
        return true;
    }
    // Runs the pushed hook with `nargs` arguments; one result is left on the stack when true.
    bool run(int nargs) const { return call_script(*s_, nargs, 1); }
    // The result, popped: nil is no answer.
    std::optional<lua_Integer> int_result() const {
        std::optional<lua_Integer> r;
        if (lua_isinteger(L(), -1)) r = lua_tointeger(L(), -1);
        else if (lua_isnumber(L(), -1)) r = static_cast<lua_Integer>(lua_tonumber(L(), -1));
        lua_pop(L(), 1);
        return r;
    }
    std::optional<bool> bool_result() const {
        std::optional<bool> r;
        if (!lua_isnil(L(), -1)) r = lua_toboolean(L(), -1) != 0;
        lua_pop(L(), 1);
        return r;
    }
    std::optional<std::string> string_result() const {
        std::optional<std::string> r;
        if (lua_type(L(), -1) == LUA_TSTRING) r = lua_tostring(L(), -1);
        lua_pop(L(), 1);
        return r;
    }

    Script* s_;
    game::SkillRules rules_{};
};

namespace {

// The script's member of the chain, made when it first extends the client.
void ensure_extension(Script& s) {
    s.registered = true;
    if (s.extension || !s.host->env.extensions) return;
    s.extension = std::make_shared<LuaExtension>(s);
    std::weak_ptr<LuaExtension> ext = s.extension;
    s.chain_id = s.host->env.extensions->add(s.extension, [ext](const net::GameConnection&) {
        auto x = ext.lock();
        return x && x->active();
    });
}

// ---------------------------------------------------------------- extend

// hailstone.hooks{ name = function(...) ... end, ... }: the core's questions (NmscHooks' names).
int l_hooks(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.hooks");
    luaL_checktype(L, 1, LUA_TTABLE);
    Script& s = Script::of(L);
    if (s.hooks_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s.hooks_ref);
    lua_pushvalue(L, 1);
    s.hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ensure_extension(s);
    return 0;
}

int l_set_active(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.set_active");
    Script& s = Script::of(L);
    s.active = lua_toboolean(L, 1) != 0;
    ensure_extension(s);
    return 0;
}

// hailstone.set_flag(name, on): wide_item_links, items_any_class, hide_any_class, aa_any_level, aa_train_all,
// augment_classes, research_classes, focus_classes, backstab_either_hand.
int l_set_flag(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.set_flag");
    static const std::pair<const char*, std::uint32_t> kFlags[] = {
        {"wide_item_links", NMSC_FLAG_WIDE_ITEM_LINKS}, {"items_any_class", NMSC_FLAG_ITEMS_ANY_CLASS},
        {"hide_any_class", NMSC_FLAG_HIDE_ANY_CLASS},   {"aa_any_level", NMSC_FLAG_AA_ANY_LEVEL},
        {"aa_train_all", NMSC_FLAG_AA_TRAIN_ALL},       {"augment_classes", NMSC_FLAG_AUGMENT_CLASSES},
        {"research_classes", NMSC_FLAG_RESEARCH_CLASSES}, {"focus_classes", NMSC_FLAG_FOCUS_CLASSES},
        {"backstab_either_hand", NMSC_FLAG_BACKSTAB_EITHER_HAND}};
    std::string name = luaL_checkstring(L, 1);
    const bool on = lua_isnone(L, 2) || lua_toboolean(L, 2) != 0;
    Script& s = Script::of(L);
    for (const auto& [n, bit] : kFlags)
        if (name == n) {
            if (on) s.flags |= bit;
            else s.flags &= ~bit;
            ensure_extension(s);
            return 0;
        }
    return luaL_error(L, "hailstone.set_flag: no switch named '%s'", name.c_str());
}

int l_set_player_classes(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.set_player_classes");
    if (auto* c = writable(Script::of(L))) c->set_player_classes(static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_set_character_classes(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.set_character_classes");
    const char* name = luaL_checkstring(L, 1);
    auto mask = static_cast<std::uint32_t>(luaL_checkinteger(L, 2));
    if (auto* c = writable(Script::of(L))) c->set_character_classes(name, mask);
    return 0;
}

// hailstone.set_pet_list{ {spawn = id, class = c}, ... }
int l_set_pet_list(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.set_pet_list");
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<game::PetInfo> list;
    const lua_Unsigned n = std::min<lua_Unsigned>(lua_rawlen(L, 1), 256);
    for (lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, static_cast<lua_Integer>(i));
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "spawn");
            lua_getfield(L, -2, "class");
            list.push_back({static_cast<std::uint32_t>(lua_tointeger(L, -2)), static_cast<std::uint32_t>(lua_tointeger(L, -1))});
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }
    if (auto* c = writable(Script::of(L))) c->set_pet_list(std::move(list));
    return 0;
}

// hailstone.set_vitals{max_hp =, cur_hp =, max_mana =, cur_mana =, max_endurance =, cur_endurance =} or nil.
int l_set_vitals(lua_State* L) {
    need_cap(L, plugin::kCapExtend, "hailstone.set_vitals");
    Script& s = Script::of(L);
    ensure_extension(s);
    if (lua_isnoneornil(L, 1)) {
        s.vitals.reset();
        return 0;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    game::CharacterVitals v;
    auto field = [&](const char* k, std::optional<std::int32_t>& out) {
        lua_getfield(L, 1, k);
        if (lua_isnumber(L, -1)) out = static_cast<std::int32_t>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    };
    field("max_hp", v.max_hp);
    field("cur_hp", v.cur_hp);
    field("max_mana", v.max_mana);
    field("cur_mana", v.cur_mana);
    field("max_endurance", v.max_endurance);
    field("cur_endurance", v.cur_endurance);
    s.vitals = v;
    // As a compiled plugin's set_vitals: the player's current points now too (negative values count as 0).
    if (net::GameConnection* c = writable(s)) {
        net::PlayerStatus& ps = c->player_status_mut();
        auto points = [](std::int32_t x) { return static_cast<std::uint32_t>(std::max(x, 0)); };
        if (v.cur_hp) ps.hp = points(*v.cur_hp);
        if (v.max_hp) ps.max_hp = points(*v.max_hp);
        if (v.cur_mana) ps.mana = points(*v.cur_mana);
        if (v.max_mana) ps.max_mana = points(*v.max_mana);
        if (v.cur_endurance) ps.endurance = points(*v.cur_endurance);
        if (v.max_endurance) ps.max_endurance = points(*v.max_endurance);
    }
    return 0;
}

// ---------------------------------------------------------------- packets

plugin::Stream stream_arg(lua_State* L, int i) {
    std::string v = luaL_checkstring(L, i);
    if (v == "world") return plugin::Stream::World;
    if (v == "zone") return plugin::Stream::Zone;
    luaL_error(L, "stream must be 'world' or 'zone'");
    return plugin::Stream::Zone;
}

// hailstone.on_packet(stream, direction, opcode, fn): fn(opcode, data) for each such packet ('in' from the server,
// 'out' to it; opcode 0: all). It returns true to drop the packet, or a string to put in its place.
int l_on_packet(lua_State* L) {
    need_cap(L, plugin::kCapPackets, "hailstone.on_packet");
    const plugin::Stream stream = stream_arg(L, 1);
    std::string d = luaL_checkstring(L, 2);
    if (d != "in" && d != "out") return luaL_error(L, "direction must be 'in' or 'out'");
    const auto opcode = static_cast<std::uint16_t>(luaL_checkinteger(L, 3));
    luaL_checktype(L, 4, LUA_TFUNCTION);
    Script& s = Script::of(L);
    if (!s.host->env.hooks) return 0;
    lua_pushvalue(L, 4);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Script* sp = &s;
    s.host->env.hooks->on_packet(s.owner, stream, d == "in" ? plugin::Direction::In : plugin::Direction::Out, opcode,
                                 [sp, ref](net::AppPacket& p) {
                                     if (!usable(*sp) || !(sp->caps & plugin::kCapPackets)) return plugin::Flow::Continue;
                                     lua_State* l = sp->L;
                                     lua_rawgeti(l, LUA_REGISTRYINDEX, ref);
                                     lua_pushinteger(l, p.opcode);
                                     lua_pushlstring(l, reinterpret_cast<const char*>(p.data.data()), p.data.size());
                                     if (!call_script(*sp, 2, 1)) return plugin::Flow::Continue;
                                     plugin::Flow flow = plugin::Flow::Continue;
                                     if (lua_type(l, -1) == LUA_TSTRING) {
                                         std::size_t n = 0;
                                         const char* data = lua_tolstring(l, -1, &n);
                                         if (n <= 0x100000) {
                                             p.data.resize(n);
                                             std::copy_n(reinterpret_cast<const std::byte*>(data), n, p.data.begin());
                                         }
                                     } else if (lua_toboolean(l, -1)) {
                                         flow = plugin::Flow::Drop;
                                     }
                                     lua_pop(l, 1);
                                     return flow;
                                 });
    s.registered = true;
    return 0;
}

// hailstone.send_packet(stream, opcode, data)
int l_send_packet(lua_State* L) {
    need_cap(L, plugin::kCapPackets, "hailstone.send_packet");
    const plugin::Stream stream = stream_arg(L, 1);
    const auto opcode = static_cast<std::uint16_t>(luaL_checkinteger(L, 2));
    std::size_t n = 0;
    const char* data = luaL_optlstring(L, 3, "", &n);
    Script& s = Script::of(L);
    if (!s.host->env.send || n > 0x100000) return 0;
    net::AppPacket p(opcode);
    p.data.resize(n);
    std::copy_n(reinterpret_cast<const std::byte*>(data), n, p.data.begin());
    s.host->env.send(stream, p);
    return 0;
}

// ---------------------------------------------------------------- ui

// hailstone.ui_function(name, fn): eq.Plugin("<script>").<name>(...) in the Lua UI calls fn(...); its result goes
// back (plain values and tables).
int l_ui_function(lua_State* L) {
    need_cap(L, plugin::kCapUi, "hailstone.ui_function");
    std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    Script& s = Script::of(L);
    if (!s.host->env.ui) return 0;
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Script* sp = &s;
    s.host->env.ui->add_function(s.name, name, [sp, ref](const std::vector<plugin::UiValue>& args) {
        if (!usable(*sp) || !(sp->caps & plugin::kCapUi)) return plugin::UiValue{};
        lua_State* l = sp->L;
        lua_rawgeti(l, LUA_REGISTRYINDEX, ref);
        for (const auto& a : args) plugin::push_value(l, a);
        if (!call_script(*sp, static_cast<int>(args.size()), 1)) return plugin::UiValue{};
        plugin::UiValue r = plugin::to_value(l, -1);
        lua_pop(l, 1);
        return r;
    }, plugin::UiBridge::Kind::Script);
    s.registered = true;
    return 0;
}

// hailstone.option{name =, label =, min = 0, max = 1, get = function() return v end, set = function(v) end}
int l_option(lua_State* L) {
    need_cap(L, plugin::kCapUi, "hailstone.option");
    luaL_checktype(L, 1, LUA_TTABLE);
    Script& s = Script::of(L);
    if (!s.host->env.ui) return 0;
    plugin::UiBridge::Option o;
    o.plugin = s.name;
    o.kind = plugin::UiBridge::Kind::Script;
    lua_getfield(L, 1, "name");
    o.name = luaL_checkstring(L, -1);
    lua_getfield(L, 1, "label");
    o.label = lua_isstring(L, -1) ? lua_tostring(L, -1) : o.name;
    lua_getfield(L, 1, "min");
    o.min = static_cast<int>(luaL_optinteger(L, -1, 0));
    lua_getfield(L, 1, "max");
    o.max = std::max(o.min, static_cast<int>(luaL_optinteger(L, -1, 1)));
    lua_pop(L, 4);
    lua_getfield(L, 1, "get");
    luaL_checktype(L, -1, LUA_TFUNCTION);
    const int get = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, 1, "set");
    luaL_checktype(L, -1, LUA_TFUNCTION);
    const int set = luaL_ref(L, LUA_REGISTRYINDEX);
    Script* sp = &s;
    o.get = [sp, get]() -> int {
        if (!usable(*sp)) return 0;
        lua_rawgeti(sp->L, LUA_REGISTRYINDEX, get);
        if (!call_script(*sp, 0, 1)) return 0;
        int v = lua_isboolean(sp->L, -1) ? (lua_toboolean(sp->L, -1) ? 1 : 0) : static_cast<int>(lua_tointeger(sp->L, -1));
        lua_pop(sp->L, 1);
        return v;
    };
    o.set = [sp, set](int v) {
        if (!usable(*sp)) return;
        lua_rawgeti(sp->L, LUA_REGISTRYINDEX, set);
        lua_pushinteger(sp->L, v);
        call_script(*sp, 1, 0);
    };
    s.host->env.ui->add_option(std::move(o));
    s.registered = true;
    return 0;
}

// hailstone.menu(label, command)
int l_menu(lua_State* L) {
    need_cap(L, plugin::kCapUi, "hailstone.menu");
    Script& s = Script::of(L);
    std::string label = luaL_checkstring(L, 1), command = luaL_checkstring(L, 2);
    if (s.host->env.ui) s.host->env.ui->add_menu({s.name, label, command, plugin::UiBridge::Kind::Script});
    s.registered = true;
    return 0;
}

// ---------------------------------------------------------------- read

int l_player(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.player");
    const net::GameConnection* c = readable(Script::of(L));
    if (!c) return 0;
    const auto& ps = c->player_status();
    lua_createtable(L, 0, 20);
    set_int(L, "spawn_id", c->world().player_id);
    set_str(L, "name", c->character_name());
    set_int(L, "level", ps.level);
    if (const auto& p = c->profile()) {
        set_int(L, "race", p->race);
        set_int(L, "class", p->class_);
    }
    set_int(L, "visible_class", c->visible_class());
    set_int(L, "class_mask", c->player_class_mask());
    set_int(L, "cur_hp", ps.hp);
    set_int(L, "max_hp", ps.max_hp);
    set_int(L, "cur_mana", ps.mana);
    set_int(L, "max_mana", ps.max_mana);
    set_int(L, "cur_endurance", ps.endurance);
    set_int(L, "max_endurance", ps.max_endurance);
    set_int(L, "target_id", c->world().target_id);
    set_int(L, "zone_id", c->zone_id());
    set_int(L, "game_state", c->hook_game_state());
    set_bool(L, "in_group", c->group().active);
    set_bool(L, "in_raid", c->raid().active);
    set_bool(L, "spell_book_open", c->spell_book_open());
    set_int(L, "buff_slots", static_cast<lua_Integer>(c->spell_state().buffs.size()));
    set_int(L, "xtarget_slots", static_cast<lua_Integer>(c->xtargets().slots.size()));
    const auto& spells = c->spell_state();
    set_int(L, "casting_spell_id", spells.casting.casting() ? spells.casting.spell : 0);
    set_int(L, "scribing_spell_id", spells.scribing ? spells.scribing->spell : 0);
    set_int(L, "stand_state", ps.stand_state);
    set_int(L, "pet_id", c->pets().focused);
    set_int(L, "filter_pc_spells", c->chat_filters().settings[game::kFilterPcSpells]);
    set_bool(L, "in_zone", c->in_zone());
    set_int(L, "aa_points", c->alt_adv().points);
    set_int(L, "aa_spent", c->alt_adv().spent);
    return 1;
}

int l_spawn(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.spawn");
    const net::GameConnection* c = readable(Script::of(L));
    if (!c) return 0;
    auto it = c->world().spawns.find(static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
    if (it == c->world().spawns.end()) return 0;
    push_spawn(L, it->second);
    return 1;
}

// hailstone.spawns(): every spawn in the zone, by id.
int l_spawns(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.spawns");
    const net::GameConnection* c = readable(Script::of(L));
    lua_newtable(L);
    if (!c) return 1;
    std::vector<const game::Spawn*> all;
    for (const auto& [id, s] : c->world().spawns) all.push_back(&s);
    std::sort(all.begin(), all.end(), [](const game::Spawn* a, const game::Spawn* b) { return a->id < b->id; });
    lua_Integer i = 0;
    for (const game::Spawn* s : all) {
        push_spawn(L, *s);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

int l_spell(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.spell");
    const net::GameConnection* c = readable(Script::of(L));
    const game::SpellData* s = c ? c->spell_table().get(static_cast<std::uint32_t>(luaL_checkinteger(L, 1))) : nullptr;
    if (!s) return 0;
    lua_createtable(L, 0, 12);
    set_int(L, "id", s->id);
    set_str(L, "name", s->name);
    lua_createtable(L, 16, 0);
    for (int i = 0; i < 16; ++i) {
        lua_pushinteger(L, s->class_level[static_cast<std::size_t>(i)]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "class_level");
    set_int(L, "timer_id", s->reuse_timer_index);
    set_int(L, "cast_time", s->cast_time);
    set_int(L, "recovery_time", s->recovery_time);
    set_int(L, "recast_time", s->recast_time);
    set_int(L, "mana", s->mana);
    set_int(L, "target_type", s->target_type);
    set_int(L, "skill", s->skill);
    set_bool(L, "is_skill", s->is_skill);
    set_int(L, "gem_icon", s->gem_icon);
    return 1;
}

// hailstone.item(slot [, sub]) or hailstone.item{type =, slot =, sub =, aug =}: the item in an inventory slot.
int l_item(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.item");
    net::ItemSlot slot;
    if (lua_istable(L, 1)) {
        auto field = [&](const char* k, std::int16_t def) {
            lua_getfield(L, 1, k);
            auto v = static_cast<std::int16_t>(luaL_optinteger(L, -1, def));
            lua_pop(L, 1);
            return v;
        };
        slot = net::ItemSlot{field("type", 0), field("slot", -1), field("sub", -1), field("aug", -1), 0, 0};
    } else {
        slot = net::ItemSlot::possessions(static_cast<std::int16_t>(luaL_checkinteger(L, 1)),
                                          static_cast<std::int16_t>(luaL_optinteger(L, 2, -1)));
    }
    const net::GameConnection* c = readable(Script::of(L));
    const net::Item* it = c ? c->inventory().at(slot) : nullptr;
    if (!it) return 0;
    push_item(L, *it, c);
    return 1;
}

// hailstone.buff(i), 1-based: {spell_id, tics, level, caster_id}; nil for an empty slot.
int l_buff(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.buff");
    const net::GameConnection* c = readable(Script::of(L));
    const lua_Integer i = luaL_checkinteger(L, 1);
    if (!c || i < 1 || static_cast<std::size_t>(i) > c->spell_state().buffs.size()) return 0;
    const game::BuffSlot& b = c->spell_state().buffs[static_cast<std::size_t>(i - 1)];
    if (b.empty()) return 0;
    lua_createtable(L, 0, 4);
    set_int(L, "spell_id", b.spell);
    set_int(L, "tics", b.tics);
    set_int(L, "level", b.level);
    set_int(L, "caster_id", b.caster_id);
    return 1;
}

// hailstone.xtarget(i), 1-based: {type, status, spawn_id, name}.
int l_xtarget(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.xtarget");
    const net::GameConnection* c = readable(Script::of(L));
    const lua_Integer i = luaL_checkinteger(L, 1);
    if (!c || i < 1 || static_cast<std::size_t>(i) > c->xtargets().slots.size()) return 0;
    const game::XTargetSlot& x = c->xtargets().slots[static_cast<std::size_t>(i - 1)];
    lua_createtable(L, 0, 4);
    set_int(L, "type", x.type);
    set_int(L, "status", x.status);
    set_int(L, "spawn_id", x.spawn_id);
    set_str(L, "name", x.name);
    return 1;
}

// hailstone.eqstr(id, ...): the game string with %1.. filled; nil when there is none.
int l_eqstr(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.eqstr");
    const net::GameConnection* c = readable(Script::of(L));
    const auto id = static_cast<std::uint32_t>(luaL_checkinteger(L, 1));
    if (!c || !c->strings().has(id)) return 0;
    std::vector<std::string> args;
    for (int i = 2; i <= lua_gettop(L) && args.size() < 16; ++i) {
        args.emplace_back(luaL_tolstring(L, i, nullptr));
        lua_pop(L, 1);
    }
    std::string r = c->strings().format(id, args);
    lua_pushlstring(L, r.data(), r.size());
    return 1;
}

// hailstone.zone_name(id): the zone's short name; nil for an unknown id.
int l_zone_name(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.zone_name");
    std::string name = game::zone_short_name(static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
    if (name.empty()) lua_pushnil(L);
    else lua_pushlstring(L, name.data(), name.size());
    return 1;
}

// hailstone.server(): {name, host, port, world, world_host}, the server this login is on (nmsc_plugin.h
// server_info); nil before the world login.
int l_server(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.server");
    const net::GameConnection* c = readable(Script::of(L));
    if (!c || c->ticket().world_host.empty()) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 5);
    if (const auto saved = c->saved_server()) {
        set_str(L, "name", saved->name);
        set_str(L, "host", saved->host);
        set_int(L, "port", saved->port);
    }
    set_str(L, "world", c->ticket().server_name);
    set_str(L, "world_host", c->ticket().world_host);
    return 1;
}

// ---------------------------------------------------------------- the rest of the client

// hailstone.chat(text [, color]): a chat line in one of the game's chat colours (15 yellow, ...; 0 the default).
int l_chat(lua_State* L) {
    std::string text = luaL_checkstring(L, 1);
    const auto color = static_cast<std::uint32_t>(luaL_optinteger(L, 2, 0));
    Script& s = Script::of(L);
    if (net::GameConnection* c = writable(s)) c->ui_message(std::move(text), 0, color);
    else if (s.host->env.print) s.host->env.print(text);
    return 0;
}

// A folder of the script's own, created now, as a string; nil when `dir` is empty.
int push_dir(lua_State* L, const fs::path& dir) {
    if (dir.empty()) {
        lua_pushnil(L);
        return 1;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string d = dir.string();
    lua_pushlstring(L, d.data(), d.size());
    return 1;
}

// hailstone.server_dir(): servers/<profile>/plugin-config/<script>/; nil before the world login.
int l_server_dir(lua_State* L) {
    Script& s = Script::of(L);
    const plugin::Policy* policy = s.host->env.policy;
    if (!policy || !policy->current() || policy->current()->dir.empty()) return push_dir(L, {});
    return push_dir(L, policy->current()->dir / "plugin-config" / s.name);
}

// hailstone.hold_autoskill(on): the Ability Book's autoskill uses nothing while this script holds it (the player's
// switch is left as it is); the hold goes when the script stops. For a script that fights itself (actions).
int l_hold_autoskill(lua_State* L) {
    need_cap(L, plugin::kCapActions, "hailstone.hold_autoskill");
    Script& s = Script::of(L);
    if (s.host->env.hold_autoskill) s.host->env.hold_autoskill(s.name, lua_toboolean(L, 1) != 0);
    return 0;
}

// hailstone.use_ability(kind, name): a skill ("skill"), discipline ("disc") or activated AA ("aa") of the player's,
// by name (a discipline's also without its rank), an AA's group or a skill's number, used as its hot button is
// (the Ability Book's path, so the game's and the server's rules hold); false when the player has none such.
int l_use_ability(lua_State* L) {
    need_cap(L, plugin::kCapActions, "hailstone.use_ability");
    const std::string kind = luaL_checkstring(L, 1);
    const std::string name = luaL_checkstring(L, 2);
    std::optional<AbilityKind> k;
    if (kind == "skill") k = AbilityKind::Skill;
    else if (kind == "disc") k = AbilityKind::Discipline;
    else if (kind == "aa") k = AbilityKind::AltAbility;
    if (!k) return luaL_argerror(L, 1, "'skill', 'disc' or 'aa'");
    Script& s = Script::of(L);
    lua_pushboolean(L, s.host->env.use_ability && s.host->env.use_ability(*k, name));
    return 1;
}

// hailstone.autoskill_list(): the player's autoskill list in order, {key ("aa:<group>", "disc:<group>",
// "skill:<id>" ...; game/ability_book.h), name, endurance, mana, target_hp_below, target_hp_above, self_hp_below,
// when ("fighting" | "always" | "resting"), keep_buff} each; {} before a character is in the world (read).
int l_autoskill_list(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.autoskill_list");
    Script& s = Script::of(L);
    const auto list = s.host->env.autoskill_list ? s.host->env.autoskill_list()
                                                 : std::vector<std::pair<game::AutoskillRule, std::string>>{};
    lua_createtable(L, static_cast<int>(list.size()), 0);
    lua_Integer n = 0;
    for (const auto& [r, name] : list) {
        lua_createtable(L, 0, 9);
        set_str(L, "key", r.key);
        set_str(L, "name", name);
        set_int(L, "endurance", r.min_endurance_pct);
        set_int(L, "mana", r.min_mana_pct);
        set_int(L, "target_hp_below", r.target_hp_below);
        set_int(L, "target_hp_above", r.target_hp_above);
        set_int(L, "self_hp_below", r.self_hp_below);
        set_str(L, "when", r.when == game::AutoskillWhen::Always    ? "always"
                           : r.when == game::AutoskillWhen::Resting ? "resting"
                                                                     : "fighting");
        lua_pushboolean(L, r.keep_buff);
        lua_setfield(L, -2, "keep_buff");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

net::GameConnection* acting(lua_State* L, const char* what); // (below, with the other actions)

// hailstone.alt_abilities(): the abilities the AA window lists, one per ability in its order (the tabs' types, then
// the rank ids): the rank it shows (the next one to buy, or the owned last one) as {id (that rank's), group, name,
// tab (1 general, 2 archetype, 3 class, 4 special; the ability's type), category (the window's column), rank (the
// ranks owned), max_rank, cost (the next rank's points; 0 at the maximum), level (the next rank's level), maxed,
// can_train (CanTrainAbility for the next rank: the window's Train), activated, prereqs = {{group, rank, met}}};
// {} before a character is in the world (read).
int l_alt_abilities(lua_State* L) {
    need_cap(L, plugin::kCapRead, "hailstone.alt_abilities");
    const net::GameConnection* c = readable(Script::of(L));
    const auto list = c ? c->aa_list(0, game::AAFilter{}) : std::vector<const game::AltAbility*>{};
    std::vector<const game::AltAbility*> sorted(list.begin(), list.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const game::AltAbility* a, const game::AltAbility* b) { return a->type < b->type; });
    lua_createtable(L, static_cast<int>(sorted.size()), 0);
    lua_Integer n = 0;
    for (const game::AltAbility* a : sorted) {
        const auto& aa = c->alt_adv();
        const bool owned = aa.has(a->id);
        const std::int32_t cur = owned ? a->rank : a->rank - 1;
        const bool maxed = a->max_rank > 0 && cur >= a->max_rank;
        lua_createtable(L, 0, 14);
        set_int(L, "id", a->id);
        set_int(L, "group", a->group);
        set_str(L, "name", c->aa_name(*a));
        set_int(L, "tab", a->type);
        set_str(L, "category", c->aa_category(*a));
        set_int(L, "rank", std::max(cur, 0));
        set_int(L, "max_rank", a->max_rank);
        set_int(L, "cost", maxed ? 0 : a->cost);
        set_int(L, "level", a->level_req);
        set_bool(L, "maxed", maxed);
        set_bool(L, "can_train", !maxed && c->aa_can_train(a->id));
        set_bool(L, "activated", a->activated());
        lua_createtable(L, static_cast<int>(a->prereq_groups.size()), 0);
        lua_Integer k = 0;
        for (std::size_t i = 0; i < a->prereq_groups.size(); ++i) {
            const std::int32_t g = a->prereq_groups[i];
            const std::int32_t need = i < a->prereq_ranks.size() ? a->prereq_ranks[i] : 0;
            if (g <= 0 || need <= 0) continue; // none, or the legacy category points (< 0)
            const game::AltAbility* have = aa.owned_rank(g);
            lua_createtable(L, 0, 3);
            set_int(L, "group", g);
            set_int(L, "rank", need);
            set_bool(L, "met", have && have->rank >= need);
            lua_rawseti(L, -2, ++k);
        }
        lua_setfield(L, -2, "prereqs");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// hailstone.train_aa(group): buys the next rank of the ability, as /alternateadv buy does (OP_AAAction 3, without
// the Train button's question), when CanTrainAbility allows it; false (nothing sent) when it does not, or the
// player has no such ability to buy. One rank a call; the points and ranks change when the server answers
// (OP_RespondAA, OP_AAExpUpdate).
int l_train_aa(lua_State* L) {
    net::GameConnection* c = acting(L, "hailstone.train_aa");
    const lua_Integer group = luaL_checkinteger(L, 1);
    bool sent = false;
    if (c && c->in_zone()) {
        for (const game::AltAbility* a : c->aa_list(0, game::AAFilter{})) {
            if (a->group != group) continue;
            if (!c->alt_adv().has(a->id) && c->aa_can_train(a->id)) sent = c->aa_train(a->id);
            break;
        }
    }
    lua_pushboolean(L, sent);
    return 1;
}

// hailstone.server_rule(section, key): a setting of the server's hailstone.cfg in a section that is not the client's
// own (plugin::Rules::script_sections; [autocombat] hunter = off), as written; nil when the server has no
// hailstone.cfg or does not set it. Names ignore case.
int l_server_rule(lua_State* L) {
    std::string section(luaL_checkstring(L, 1)), key(luaL_checkstring(L, 2));
    for (std::string* t : {&section, &key})
        for (char& ch : *t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const plugin::Policy* policy = Script::of(L).host->env.policy;
    const auto& rules = policy ? policy->current() : std::nullopt;
    if (rules && rules->configured)
        if (auto sec = rules->script_sections.find(section); sec != rules->script_sections.end())
            if (auto v = sec->second.find(key); v != sec->second.end()) {
                lua_pushlstring(L, v->second.data(), v->second.size());
                return 1;
            }
    lua_pushnil(L);
    return 1;
}

// hailstone.character_dir(): the character's files' plugin-config/<script>/; nil before a character is chosen.
int l_character_dir(lua_State* L) {
    Script& s = Script::of(L);
    const net::GameConnection* c = writable(s);
    const CharacterFiles files = c ? c->own_files() : CharacterFiles{};
    return push_dir(L, files.empty() ? fs::path() : files.dir / "plugin-config" / s.name);
}

net::GameConnection* acting(lua_State* L, const char* what) {
    need_cap(L, plugin::kCapActions, what);
    return writable(Script::of(L));
}

// hailstone.right_click_item(slot [, class]) or ({type =, slot =, sub =, aug =} [, class]): the client's own right
// click, as without the hooks' item_right_click; `class` nonzero: its casting check sees that class.
int l_right_click_item(lua_State* L) {
    net::GameConnection* c = acting(L, "hailstone.right_click_item");
    net::ItemSlot slot;
    if (lua_istable(L, 1)) {
        auto field = [&](const char* k, std::int16_t def) {
            lua_getfield(L, 1, k);
            auto v = static_cast<std::int16_t>(luaL_optinteger(L, -1, def));
            lua_pop(L, 1);
            return v;
        };
        slot = net::ItemSlot{field("type", 0), field("slot", -1), field("sub", -1), field("aug", -1), 0, 0};
    } else {
        slot = net::ItemSlot::possessions(static_cast<std::int16_t>(luaL_checkinteger(L, 1)));
    }
    const lua_Integer klass = luaL_optinteger(L, 2, 0);
    if (c) c->right_click_item_original(slot, {}, static_cast<std::uint8_t>(klass >= 0 && klass <= 0xff ? klass : 0));
    return 0;
}

int l_close_spell_book(lua_State* L) {
    if (net::GameConnection* c = acting(L, "hailstone.close_spell_book")) c->close_spell_book();
    return 0;
}

int l_clear_spell_recovery(lua_State* L) {
    if (net::GameConnection* c = acting(L, "hailstone.clear_spell_recovery")) c->clear_spell_recovery();
    return 0;
}

// hailstone.set_window_pet(id): the pet window shows another of the player's pets (the server is not told).
int l_set_window_pet(lua_State* L) {
    if (net::GameConnection* c = acting(L, "hailstone.set_window_pet"))
        c->set_window_pet(static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
    return 0;
}

// hailstone.inspect_item(id [, {augment ids}]): the item's display, as a click on its link (OP_ItemLinkClick).
int l_inspect_item(lua_State* L) {
    net::GameConnection* c = acting(L, "hailstone.inspect_item");
    game::ItemLink link;
    link.item_id = static_cast<std::uint32_t>(luaL_checkinteger(L, 1));
    if (lua_istable(L, 2))
        for (int k = 0; k < 6; ++k) {
            lua_rawgeti(L, 2, k + 1);
            link.augments[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);
        }
    if (c && link.item_id) c->request_item(link);
    return 0;
}

int l_max_camera_distance(lua_State* L) {
    const auto& get = Script::of(L).host->env.max_camera_distance;
    lua_pushnumber(L, get ? get() : 0.0);
    return 1;
}

// hailstone.set_max_camera_distance(d): the camera's zoom limit; the current zoom is cut to it.
int l_set_max_camera_distance(lua_State* L) {
    need_cap(L, plugin::kCapActions, "hailstone.set_max_camera_distance");
    const auto d = static_cast<float>(luaL_checknumber(L, 1));
    const auto& set = Script::of(L).host->env.set_max_camera_distance;
    if (set && d > 0.0f) set(d);
    return 0;
}

} // namespace

void register_hailstone(lua_State* L) {
    lua_newtable(L);
    for (auto [n, fn] : std::initializer_list<std::pair<const char*, lua_CFunction>>{
             {"hooks", l_hooks},
             {"set_active", l_set_active},
             {"set_flag", l_set_flag},
             {"set_player_classes", l_set_player_classes},
             {"set_character_classes", l_set_character_classes},
             {"set_pet_list", l_set_pet_list},
             {"set_vitals", l_set_vitals},
             {"on_packet", l_on_packet},
             {"send_packet", l_send_packet},
             {"ui_function", l_ui_function},
             {"option", l_option},
             {"menu", l_menu},
             {"player", l_player},
             {"spawn", l_spawn},
             {"spawns", l_spawns},
             {"spell", l_spell},
             {"item", l_item},
             {"buff", l_buff},
             {"xtarget", l_xtarget},
             {"eqstr", l_eqstr},
             {"zone_name", l_zone_name},
             {"server", l_server},
             {"chat", l_chat},
             {"server_dir", l_server_dir},
             {"server_rule", l_server_rule},
             {"hold_autoskill", l_hold_autoskill},
             {"autoskill_list", l_autoskill_list},
             {"alt_abilities", l_alt_abilities},
             {"train_aa", l_train_aa},
             {"use_ability", l_use_ability},
             {"character_dir", l_character_dir},
             {"right_click_item", l_right_click_item},
             {"close_spell_book", l_close_spell_book},
             {"clear_spell_recovery", l_clear_spell_recovery},
             {"set_window_pet", l_set_window_pet},
             {"inspect_item", l_inspect_item},
             {"max_camera_distance", l_max_camera_distance},
             {"set_max_camera_distance", l_set_max_camera_distance}}) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, n);
    }
    lua_setglobal(L, "hailstone");
    // A plugin script in its own folder (lua/<name>/init.lua) with a ui/ there: its windows for each UI pack.
    Script& s = Script::of(L);
    std::error_code ec;
    if (s.social < 0 && (s.caps & plugin::kCapUi) && s.host->env.ui && s.entry.filename() == "init.lua" &&
        fs::is_directory(s.root / "ui", ec)) {
        s.host->env.ui->set_folder(s.name, s.root, plugin::UiBridge::Kind::Script);
        s.registered = true;
    }
}

void release_plugin(Script& s) {
    if (s.extension) {
        s.extension->detach();
        if (s.host->env.extensions) s.host->env.extensions->remove(s.chain_id);
        s.extension.reset();
    }
    if (s.social < 0 && s.registered && s.host->env.ui) s.host->env.ui->remove(s.name, plugin::UiBridge::Kind::Script);
}

} // namespace nmsc::mq
