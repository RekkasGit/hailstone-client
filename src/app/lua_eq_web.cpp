// The in-game browser for the UI packs (docs/browser.md; ui/<pack>/browser.lua) over plugin::WebAccess. The pack's
// own windows act as the player; a plugin's code (LuaUi::caller_tag) as its owner ("plugin:<name>" for a compiled
// plugin's, "script:<name>" for a Lua plugin's, whose windows share the script's mq.web tabs), with that owner's
// permissions (it needs 'web'), on its own tabs only.
//
//   eq.WebAvailable()            the helper is there (or not tried yet) and has not been given up
//   eq.WebOpen(url [, w, h])     a new tab, or nil and why; "example.org" becomes "https://example.org"
//   eq.WebTabs()                 the caller's tabs, oldest first
//   eq.WebState(tab)             { url, title, loading, canBack, canForward, closed, why } or nil
//   eq.WebNavigate(tab, url)     true, or false and why
//   eq.WebNav(tab, what)         "back", "forward", "reload", "stop"
//   eq.WebClose(tab)
//   eq.WebView(tab, w, h)        draws the page w x h and gives it the mouse, and the keyboard once clicked;
//                                the page takes that size. Returns whether it is hovered
//   eq.WebText(tab, text)        types the text where the page has its cursor (the gamepad pack's on-screen keys)
//   eq.WebEval(tab, script)      runs script in the page
//   eq.WebPost(tab, message)     calls the page's window.hailstone.onmessage(message)
//   eq.WebMessages(tab)          what the page posted (window.hailstone.post) since the last call; a plugin's tabs
//                                only: the player's pages have no one to talk to
//   eq.WebOpenExternal(url)      in the system's browser (http(s) only; the player's windows only)
#include <lua.hpp>

#include <string>

#include "app/lua_eq.h"
#include "core/web_url.h"
#include "platform/open_folder.h"
#include "plugin/policy.h"
#include "plugin/web_access.h"
#include "ui/lua/lua_host.h"

namespace hailstone::app {

namespace {

struct Caller {
    plugin::WebAccess* web = nullptr;
    std::string owner;
    std::uint32_t caps = 0;
};

// Each function below checks its arguments before it makes its Caller or any other C++ object: Lua is built as C,
// so a failed check longjmps out without running destructors.
Caller caller(lua_State* L) {
    auto& h = *static_cast<EqHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    Caller c;
    c.web = h.web;
    const std::string_view tag = ui::lua::LuaUi::caller_tag();
    if (tag.empty()) {
        c.caps = plugin::kCapAll;
    } else {
        c.owner = std::string(tag); // (plugin::UiBridge::owner_tag)
        c.caps = h.plugin_caps ? h.plugin_caps(tag) : 0;
    }
    return c;
}

std::uint32_t tab_arg(lua_State* L, int i) {
    const lua_Integer t = luaL_checkinteger(L, i);
    return t > 0 && t <= 0xffffffff ? static_cast<std::uint32_t>(t) : 0;
}

int WebAvailable(lua_State* L) {
    Caller c = caller(L);
    lua_pushboolean(L, c.web && c.web->available());
    return 1;
}

int WebOpen(lua_State* L) {
    const char* url_arg = luaL_checkstring(L, 1);
    const int w = static_cast<int>(luaL_optinteger(L, 2, 800)), h = static_cast<int>(luaL_optinteger(L, 3, 600));
    Caller c = caller(L);
    const std::string url = url_arg;
    std::string why = "the in-game browser is not installed";
    const std::uint32_t id = c.web ? c.web->open(c.owner, c.caps, url, w, h, &why) : 0;
    if (!id) {
        lua_pushnil(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

int WebTabs(lua_State* L) {
    Caller c = caller(L);
    lua_newtable(L);
    if (!c.web) return 1;
    lua_Integer n = 0;
    for (std::uint32_t id : c.web->tabs(c.owner)) {
        lua_pushinteger(L, id);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int WebState(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    Caller c = caller(L);
    auto s = c.web ? c.web->state(c.owner, tab) : std::nullopt;
    if (!s) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    lua_pushstring(L, s->url.c_str());
    lua_setfield(L, -2, "url");
    lua_pushstring(L, s->title.c_str());
    lua_setfield(L, -2, "title");
    lua_pushboolean(L, s->loading);
    lua_setfield(L, -2, "loading");
    lua_pushboolean(L, s->can_back);
    lua_setfield(L, -2, "canBack");
    lua_pushboolean(L, s->can_forward);
    lua_setfield(L, -2, "canForward");
    lua_pushboolean(L, s->closed);
    lua_setfield(L, -2, "closed");
    lua_pushstring(L, s->why.c_str());
    lua_setfield(L, -2, "why");
    return 1;
}

int WebNavigate(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    const char* url_arg = luaL_checkstring(L, 2);
    Caller c = caller(L);
    const std::string url = url_arg;
    std::string why = "the in-game browser is not installed";
    const bool ok = c.web && c.web->navigate(c.owner, c.caps, tab, url, &why);
    lua_pushboolean(L, ok);
    if (ok) return 1;
    lua_pushstring(L, why.c_str());
    return 2;
}

int WebNav(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    const std::string_view what = luaL_checkstring(L, 2);
    if (what != "back" && what != "forward" && what != "reload" && what != "stop")
        return luaL_argerror(L, 2, "back, forward, reload or stop");
    Caller c = caller(L);
    if (c.web) c.web->nav(c.owner, c.caps, tab, std::string(what));
    return 0;
}

int WebClose(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    Caller c = caller(L);
    if (c.web) c.web->close(c.owner, tab);
    return 0;
}

int WebText(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    const char* text_arg = luaL_checkstring(L, 2);
    Caller c = caller(L);
    const std::string text = text_arg;
    if (c.web) c.web->text(c.owner, c.caps, tab, text);
    return 0;
}

int WebEval(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    const char* script_arg = luaL_checkstring(L, 2);
    Caller c = caller(L);
    const std::string script = script_arg;
    lua_pushboolean(L, c.web && c.web->eval(c.owner, c.caps, tab, script));
    return 1;
}

int WebPost(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    const char* message_arg = luaL_checkstring(L, 2);
    Caller c = caller(L);
    const std::string message = message_arg;
    lua_pushboolean(L, c.web && c.web->post(c.owner, c.caps, tab, message));
    return 1;
}

int WebMessages(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    Caller c = caller(L);
    lua_newtable(L);
    if (!c.web) return 1;
    lua_Integer n = 0;
    while (auto m = c.web->take_message(c.owner, tab)) {
        lua_pushlstring(L, m->data(), m->size());
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int WebView(lua_State* L) {
    const std::uint32_t tab = tab_arg(L, 1);
    const auto w = static_cast<float>(luaL_checknumber(L, 2)), h = static_cast<float>(luaL_checknumber(L, 3));
    Caller c = caller(L);
    lua_pushboolean(L, c.web && c.web->view(c.owner, c.caps, tab, w, h));
    return 1;
}

int WebOpenExternal(lua_State* L) {
    const char* url_arg = luaL_checkstring(L, 1);
    Caller c = caller(L);
    const std::string url = url_arg;
    // Only from the player's windows: a plugin's page in the player's own browser is out of the rules' reach.
    const bool ok = c.owner.empty() && web::url_allowed(url, {}) && platform::open_url(url);
    lua_pushboolean(L, ok);
    return 1;
}

} // namespace

void register_web_api(lua_State* L, EqHost& h) {
    static const luaL_Reg fns[] = {
        {"WebAvailable", WebAvailable}, {"WebOpen", WebOpen},       {"WebTabs", WebTabs},
        {"WebState", WebState},         {"WebNavigate", WebNavigate}, {"WebNav", WebNav},
        {"WebClose", WebClose},         {"WebView", WebView},       {"WebText", WebText},
        {"WebEval", WebEval},           {"WebPost", WebPost},       {"WebMessages", WebMessages},
        {"WebOpenExternal", WebOpenExternal}, {nullptr, nullptr}};
    lua_pushlightuserdata(L, &h);
    luaL_setfuncs(L, fns, 1);
}

} // namespace hailstone::app
