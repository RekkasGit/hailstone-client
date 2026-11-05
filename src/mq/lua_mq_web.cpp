// mq.web: the in-game browser for scripts and Lua plugins (docs/browser.md, docs/plugins.md), over
// plugin::WebAccess as "script:<name>". Each call needs the server's 'web' permission (a refusal is a Lua error,
// as for mq.imgui); addresses are http(s) on the hosts the server's [web] allow names; a script sees only its own
// tabs, which close when it stops.
//
//   tab = mq.web.open(url [, w, h])     nil, why when refused
//   ok, why = mq.web.navigate(tab, url)
//   mq.web.nav(tab, "back"|"forward"|"reload"|"stop")
//   mq.web.close(tab)
//   mq.web.eval(tab, script)            runs script in the page
//   mq.web.post(tab, message)           the page's window.hailstone.onmessage(message)
//   mq.web.messages(tab)                what the page posted (window.hailstone.post) since the last call
//   mq.web.text(tab, text)              typed where the page has its cursor
//   mq.web.state(tab)                   { url, title, loading, canBack, canForward, closed, why } or nil
//   mq.web.tabs()                       the script's tabs
//   mq.web.view(tab, w, h)              in an mq.imgui window: draws the page and gives it the mouse and keyboard
//   mq.web.available()
#include <lua.hpp>

#include <string>

#include "mq/lua_script.h"
#include "plugin/policy.h"
#include "plugin/web_access.h"

namespace nmsc::mq {

namespace {

struct Caller {
    plugin::WebAccess& web;
    std::string owner;
    std::uint32_t caps;
};

// The script's browser: a Lua error without 'web' or a browser.
Caller caller(lua_State* L, const char* what) {
    Script& s = Script::of(L);
    if ((s.caps & plugin::kCapWeb) == 0)
        luaL_error(L, "%s needs the server's '%s' permission", what, plugin::caps_text(plugin::kCapWeb).c_str());
    if (!s.host->env.web) luaL_error(L, "%s: this client has no browser", what);
    return {*s.host->env.web, "script:" + s.name, s.caps};
}

std::uint32_t tab_arg(lua_State* L, int i) {
    const lua_Integer t = luaL_checkinteger(L, i);
    return t > 0 && t <= 0xffffffff ? static_cast<std::uint32_t>(t) : 0;
}

int l_available(lua_State* L) {
    Script& s = Script::of(L);
    lua_pushboolean(L, (s.caps & plugin::kCapWeb) && s.host->env.web && s.host->env.web->available());
    return 1;
}

int l_open(lua_State* L) {
    Caller c = caller(L, "mq.web.open");
    const std::string url = luaL_checkstring(L, 1);
    const int w = static_cast<int>(luaL_optinteger(L, 2, 800)), h = static_cast<int>(luaL_optinteger(L, 3, 600));
    std::string why;
    const std::uint32_t id = c.web.open(c.owner, c.caps, url, w, h, &why);
    if (!id) {
        lua_pushnil(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

int l_navigate(lua_State* L) {
    Caller c = caller(L, "mq.web.navigate");
    const std::uint32_t tab = tab_arg(L, 1);
    const std::string url = luaL_checkstring(L, 2);
    std::string why;
    const bool ok = c.web.navigate(c.owner, c.caps, tab, url, &why);
    lua_pushboolean(L, ok);
    if (ok) return 1;
    lua_pushstring(L, why.c_str());
    return 2;
}

int l_nav(lua_State* L) {
    Caller c = caller(L, "mq.web.nav");
    const std::uint32_t tab = tab_arg(L, 1);
    const std::string what = luaL_checkstring(L, 2);
    if (what != "back" && what != "forward" && what != "reload" && what != "stop")
        return luaL_argerror(L, 2, "back, forward, reload or stop");
    c.web.nav(c.owner, c.caps, tab, what);
    return 0;
}

int l_close(lua_State* L) {
    Caller c = caller(L, "mq.web.close");
    c.web.close(c.owner, tab_arg(L, 1));
    return 0;
}

int l_eval(lua_State* L) {
    Caller c = caller(L, "mq.web.eval");
    const std::uint32_t tab = tab_arg(L, 1);
    const std::string script = luaL_checkstring(L, 2);
    lua_pushboolean(L, c.web.eval(c.owner, c.caps, tab, script));
    return 1;
}

int l_post(lua_State* L) {
    Caller c = caller(L, "mq.web.post");
    const std::uint32_t tab = tab_arg(L, 1);
    const std::string message = luaL_checkstring(L, 2);
    lua_pushboolean(L, c.web.post(c.owner, c.caps, tab, message));
    return 1;
}

int l_text(lua_State* L) {
    Caller c = caller(L, "mq.web.text");
    const std::uint32_t tab = tab_arg(L, 1);
    const std::string text = luaL_checkstring(L, 2);
    lua_pushboolean(L, c.web.text(c.owner, c.caps, tab, text));
    return 1;
}

int l_messages(lua_State* L) {
    Caller c = caller(L, "mq.web.messages");
    const std::uint32_t tab = tab_arg(L, 1);
    lua_newtable(L);
    lua_Integer n = 0;
    while (auto m = c.web.take_message(c.owner, tab)) {
        lua_pushlstring(L, m->data(), m->size());
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int l_state(lua_State* L) {
    Caller c = caller(L, "mq.web.state");
    auto st = c.web.state(c.owner, tab_arg(L, 1));
    if (!st) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    lua_pushstring(L, st->url.c_str());
    lua_setfield(L, -2, "url");
    lua_pushstring(L, st->title.c_str());
    lua_setfield(L, -2, "title");
    lua_pushboolean(L, st->loading);
    lua_setfield(L, -2, "loading");
    lua_pushboolean(L, st->can_back);
    lua_setfield(L, -2, "canBack");
    lua_pushboolean(L, st->can_forward);
    lua_setfield(L, -2, "canForward");
    lua_pushboolean(L, st->closed);
    lua_setfield(L, -2, "closed");
    lua_pushstring(L, st->why.c_str());
    lua_setfield(L, -2, "why");
    return 1;
}

int l_tabs(lua_State* L) {
    Caller c = caller(L, "mq.web.tabs");
    lua_newtable(L);
    lua_Integer n = 0;
    for (std::uint32_t id : c.web.tabs(c.owner)) {
        lua_pushinteger(L, id);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int l_view(lua_State* L) {
    Caller c = caller(L, "mq.web.view");
    const std::uint32_t tab = tab_arg(L, 1);
    const auto w = static_cast<float>(luaL_checknumber(L, 2)), h = static_cast<float>(luaL_checknumber(L, 3));
    lua_pushboolean(L, c.web.view(c.owner, c.caps, tab, w, h));
    return 1;
}

} // namespace

void push_web_module(lua_State* L) {
    lua_newtable(L);
    for (auto [n, fn] : std::initializer_list<std::pair<const char*, lua_CFunction>>{
             {"available", l_available}, {"open", l_open}, {"navigate", l_navigate}, {"nav", l_nav},
             {"close", l_close}, {"eval", l_eval}, {"post", l_post}, {"text", l_text}, {"messages", l_messages},
             {"state", l_state}, {"tabs", l_tabs}, {"view", l_view}}) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, n);
    }
}

} // namespace nmsc::mq
