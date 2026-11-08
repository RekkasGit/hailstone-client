#include "app/lua_eq_plugins.h"

#include <string>
#include <vector>

#include <lua.hpp>

#include "app/lua_eq.h"
#include "core/ascii_case.h"
#include "core/sha256.h"
#include "plugin/lua_value.h"
#include "plugin/policy.h"
#include "plugin/ui_bridge.h"
#include "ui/lua/lua_host.h"

namespace hailstone::app {

namespace {

using plugin::UiValue;
using plugin::push_value;
using plugin::to_value;

EqHost& host_of(lua_State* L) { return *static_cast<EqHost*>(lua_touserdata(L, lua_upvalueindex(1))); }

// Whose code runs: "" the pack's (the player's), else a plugin folder's owner tag (LuaUi::caller_tag).
std::string_view caller() { return ui::lua::LuaUi::caller_tag(); }

// The owner a name means to the caller: a plugin's code names itself by its bare name (a script's windows call
// eq.Plugin("<script>") even beside a compiled plugin of that name); otherwise the bridge's resolve. "" none.
std::string target_owner(const EqHost& h, std::string_view name) {
    if (!h.plugin_ui) return {};
    const std::string_view me = caller();
    if (!me.empty())
        if (auto t = plugin::UiBridge::parse_tag(me); t && iequals(t->second, name) && !h.plugin_ui->resolve(me).empty())
            return std::string(me);
    return h.plugin_ui->resolve(name);
}

// A plugin's code may reach only its own functions, options and menu entries; the pack (the player) reaches all.
// A plugin that drives another's exported functions would act with the other's capabilities (docs/plugins.md
// "Plugin windows").
bool may_reach(std::string_view owner) { return caller().empty() || iequals(caller(), owner); }

// p.fn(...): upvalue 1 the host, 2 the owner's tag, 3 the function's name; the arguments are the call's.
int plugin_fn(lua_State* L) {
    EqHost& h = host_of(L);
    const char* owner = lua_tostring(L, lua_upvalueindex(2));
    const char* name = lua_tostring(L, lua_upvalueindex(3));
    if (!owner || !may_reach(owner)) {
        lua_pushnil(L);
        return 1;
    }
    for (int i = 1; i <= lua_gettop(L); ++i) ui::lua::unview(L, i); // (to_value reads tables raw)
    std::vector<UiValue> args;
    for (int i = 1; i <= lua_gettop(L); ++i) args.push_back(to_value(L, i));
    auto r = h.plugin_ui ? h.plugin_ui->call(owner, name ? name : "", args) : std::nullopt;
    if (!r) {
        lua_pushnil(L);
        return 1;
    }
    push_value(L, *r);
    return 1;
}

// p.call("fn", ...)
int plugin_call(lua_State* L) {
    EqHost& h = host_of(L);
    const char* owner = lua_tostring(L, lua_upvalueindex(2));
    const char* name = luaL_checkstring(L, 1);
    if (!owner || !may_reach(owner)) {
        lua_pushnil(L);
        return 1;
    }
    for (int i = 2; i <= lua_gettop(L); ++i) ui::lua::unview(L, i); // (to_value reads tables raw)
    std::vector<UiValue> args;
    for (int i = 2; i <= lua_gettop(L); ++i) args.push_back(to_value(L, i));
    auto r = h.plugin_ui ? h.plugin_ui->call(owner, name, args) : std::nullopt;
    if (!r) {
        lua_pushnil(L);
        return 1;
    }
    push_value(L, *r);
    return 1;
}

// The plugin object's __index: p.call, p.name, p.owner, and p.<fn> as a function calling it.
int plugin_index(lua_State* L) {
    // upvalue 1 the host, 2 the owner's tag; arguments: the object, the key.
    const char* key = luaL_checkstring(L, 2);
    std::string k = key;
    if (k == "name" || k == "owner") {
        const char* owner = lua_tostring(L, lua_upvalueindex(2));
        auto t = plugin::UiBridge::parse_tag(owner ? owner : "");
        lua_pushstring(L, k == "owner" || !t ? owner : t->second.c_str());
        return 1;
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, lua_upvalueindex(2));
    if (k == "call") {
        lua_pushcclosure(L, plugin_call, 2);
        return 1;
    }
    lua_pushstring(L, key);
    lua_pushcclosure(L, plugin_fn, 3);
    return 1;
}

// p = eq.Plugin(name)   -- name: a bare name or an owner tag; nil for none, and from a plugin's code for another's
int Plugin(lua_State* L) {
    EqHost& h = host_of(L);
    const char* name = luaL_checkstring(L, 1);
    const std::string owner = target_owner(h, name);
    if (owner.empty() || !may_reach(owner)) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);           // the object
    lua_createtable(L, 0, 1);  // its metatable
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushstring(L, owner.c_str());
    lua_pushcclosure(L, plugin_index, 2);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    return 1;
}

// eq.PluginOptions(): {plugin, owner (its tag: plugin::UiBridge::owner_tag), name, label, min, max, value} each
int PluginOptions(lua_State* L) {
    EqHost& h = host_of(L);
    lua_newtable(L);
    if (!h.plugin_ui) return 1;
    // A copy: each get runs plugin code, which may add or replace options (hailstone.option{...}) and so move the
    // list, or the very function running, under a loop over the bridge's own.
    std::vector<plugin::UiBridge::Option> options = h.plugin_ui->options();
    std::erase_if(options, [](const plugin::UiBridge::Option& o) { return !may_reach(o.owner()); }); // a plugin: its own
    lua_Integer i = 0;
    for (const auto& o : options) {
        lua_createtable(L, 0, 7);
        lua_pushstring(L, o.plugin.c_str());
        lua_setfield(L, -2, "plugin");
        lua_pushstring(L, o.owner().c_str());
        lua_setfield(L, -2, "owner");
        lua_pushstring(L, o.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, o.label.c_str());
        lua_setfield(L, -2, "label");
        lua_pushinteger(L, o.min);
        lua_setfield(L, -2, "min");
        lua_pushinteger(L, o.max);
        lua_setfield(L, -2, "max");
        lua_pushinteger(L, o.get ? o.get() : 0);
        lua_setfield(L, -2, "value");
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// eq.SetPluginOption(plugin, name, value)   -- plugin: a name or an owner tag (an option's `owner`)
int SetPluginOption(lua_State* L) {
    EqHost& h = host_of(L);
    const char* plugin = luaL_checkstring(L, 1);
    const char* name = luaL_checkstring(L, 2);
    lua_Integer v = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) ? 1 : 0) : luaL_checkinteger(L, 3);
    if (!h.plugin_ui) return 0;
    const std::string owner = target_owner(h, plugin);
    if (owner.empty() || !may_reach(owner)) return 0;
    if (auto* o = h.plugin_ui->find_option(owner, name); o && o->set) {
        lua_Integer clamped = v < o->min ? o->min : v > o->max ? o->max : v;
        auto set = o->set; // the call may change the list
        set(static_cast<int>(clamped));
    }
    return 0;
}

// eq.PluginMenu(): {plugin, owner, label, command} each
int PluginMenu(lua_State* L) {
    EqHost& h = host_of(L);
    lua_newtable(L);
    if (!h.plugin_ui) return 1;
    lua_Integer i = 0;
    for (const auto& e : h.plugin_ui->menu()) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, e.plugin.c_str());
        lua_setfield(L, -2, "plugin");
        lua_pushstring(L, e.owner().c_str());
        lua_setfield(L, -2, "owner");
        lua_pushstring(L, e.label.c_str());
        lua_setfield(L, -2, "label");
        lua_pushstring(L, e.command.c_str());
        lua_setfield(L, -2, "command");
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// eq.PluginMenuClick(i): runs entry i's command as its owner (run_plugin_menu).
int PluginMenuClick(lua_State* L) {
    EqHost& h = host_of(L);
    lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 1 || !h.plugin_ui || static_cast<std::size_t>(i) > h.plugin_ui->menu().size()) return 0;
    if (!may_reach(h.plugin_ui->menu()[static_cast<std::size_t>(i - 1)].owner())) return 0; // a plugin: its own
    run_plugin_menu(h, static_cast<std::size_t>(i - 1));
    return 0;
}

// The question about the server's compiled plugins is the player's alone: a plugin's code in the UI
// (LuaUi::caller_tag: a server's own plugin windows among them) neither sees it nor answers it.
bool plugin_code() { return !caller().empty(); }

// q = eq.PluginConsent()
int PluginConsent(lua_State* L) {
    EqHost& h = host_of(L);
    auto q = h.plugin_consent && !plugin_code() ? h.plugin_consent() : std::nullopt;
    if (!q) {
        lua_pushnil(L);
        return 1;
    }
    if (ui::lua::pack_code(L)) ui::lua::guard_drawing_window(); // the window asking shows it: guarded (lua_host.h)
    lua_createtable(L, 0, 4);
    lua_pushstring(L, q->server.c_str());
    lua_setfield(L, -2, "server");
    lua_pushinteger(L, static_cast<lua_Integer>(q->serial));
    lua_setfield(L, -2, "serial");
    lua_pushlstring(L, plugin::kConsentWarning.data(), plugin::kConsentWarning.size());
    lua_setfield(L, -2, "warning");
    lua_createtable(L, static_cast<int>(q->files.size()), 0);
    lua_Integer i = 0;
    for (const auto& f : q->files) {
        const std::string hex = to_hex(f.sha256);
        const auto u8 = f.file.filename().u8string();
        lua_createtable(L, 0, 5);
        lua_pushstring(L, f.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushlstring(L, reinterpret_cast<const char*>(u8.data()), u8.size());
        lua_setfield(L, -2, "file");
        lua_pushinteger(L, static_cast<lua_Integer>(f.size));
        lua_setfield(L, -2, "size");
        lua_pushstring(L, hex.c_str());
        lua_setfield(L, -2, "sha256");
        lua_pushstring(L, hex.substr(0, 16).c_str());
        lua_setfield(L, -2, "short");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "plugins");
    return 1;
}

// ok = eq.AnswerPluginConsent(allow): true, false, or nil (put away until the next zone-in)
int AnswerPluginConsent(lua_State* L) {
    EqHost& h = host_of(L);
    std::optional<bool> allow;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        allow = lua_toboolean(L, 1) != 0;
    }
    if (plugin_code() || !h.answer_plugin_consent || !h.plugin_consent || !h.plugin_consent()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    h.answer_plugin_consent(allow);
    lua_pushboolean(L, 1);
    return 1;
}

} // namespace

bool run_plugin_menu(EqHost& h, std::size_t i) {
    if (!h.plugin_ui || i >= h.plugin_ui->menu().size() || !h.submit_chat) return false;
    const plugin::UiBridge::MenuEntry& e = h.plugin_ui->menu()[i];
    const std::string command = e.command, owner = e.owner();
    plugin::CallerScope scope(h.plugin_caps ? h.plugin_caps(owner) : 0u, owner);
    h.submit_chat(command);
    return true;
}

void register_plugins_api(lua_State* L, EqHost& host) {
    for (auto [name, fn] : std::initializer_list<std::pair<const char*, lua_CFunction>>{
             {"Plugin", Plugin},
             {"PluginOptions", PluginOptions},
             {"SetPluginOption", SetPluginOption},
             {"PluginMenu", PluginMenu},
             {"PluginMenuClick", PluginMenuClick},
             {"PluginConsent", PluginConsent},
             {"AnswerPluginConsent", AnswerPluginConsent}}) {
        lua_pushlightuserdata(L, &host);
        lua_pushcclosure(L, fn, 1);
        lua_setfield(L, -2, name);
    }
}

} // namespace hailstone::app
