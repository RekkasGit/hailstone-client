#include "app/lua_ui.h"

#include <limits>

#include <lua.hpp>

#include "app/lua_ui_record.h"
#include "core/lua_keep.h"
#include "ui/lua/lua_host.h"

namespace hailstone::app {
namespace {

UiHost& host_of(lua_State* L) { return *static_cast<UiHost*>(lua_touserdata(L, lua_upvalueindex(1))); }

bool trusted(lua_State* L, const UiHost& host) { return host.in_game && host.trusted_ui && ui::lua::pack_code(L); }

void push_strings(lua_State* L, const char* key, const std::vector<std::string>& values) {
    lua_createtable(L, static_cast<int>(values.size()), 0);
    lua_Integer index = 0;
    for (const auto& value : values) {
        lua_pushlstring(L, value.data(), value.size());
        lua_rawseti(L, -2, ++index);
    }
    lua_setfield(L, -2, key);
}

int ServerManagementAvailable(lua_State* L) {
    const UiHost& host = host_of(L);
    bool available = false;
    if (trusted(L, host) && host.server_management_available)
        core::cxx_guard(L, [&] { available = host.server_management_available(); });
    lua_pushboolean(L, available);
    return 1;
}

int ServerManagementOpen(lua_State* L) {
    const UiHost& host = host_of(L);
    bool opened = false;
    if (trusted(L, host) && host.open_server_management)
        core::cxx_guard(L, [&] { opened = host.open_server_management(); });
    lua_pushboolean(L, opened);
    return 1;
}

int ServerManagementClose(lua_State* L) {
    const UiHost& host = host_of(L);
    if (trusted(L, host) && host.close_server_management) core::cxx_guard(L, [&] { host.close_server_management(); });
    return 0;
}

// Known serial lets the built-in window retain the snapshot and read its save status without rebuilding the catalog.
int ServerManagementViewFn(lua_State* L) {
    const UiHost& host = host_of(L);
    if (!trusted(L, host) || !host.server_management) return 0;
    const lua_Integer requested = luaL_optinteger(L, 1, 0);
    const auto serial = requested > 0 && requested <= std::numeric_limits<std::uint32_t>::max() ?
        static_cast<std::uint32_t>(requested) : 0u;
    core::LuaScratch<std::optional<ServerManagementView>> scratch(L);
    auto& view = *scratch;
    core::cxx_guard(L, [&] { view = host.server_management(serial); });
    if (!view) {
        scratch.release();
        return 0;
    }
    lua_createtable(L, 0, 8);
    record::text(L, "revision", view->revision);
    record::text(L, "file_revision", view->file_revision);
    record::text(L, "message", view->message);
    lua_pushinteger(L, static_cast<lua_Integer>(view->serial));
    lua_setfield(L, -2, "serial");
    lua_pushboolean(L, view->saving);
    lua_setfield(L, -2, "saving");
    if (serial == view->serial) {
        scratch.release();
        return 1;
    }
    lua_pushinteger(L, view->minimum_status);
    lua_setfield(L, -2, "minimum_status");
    lua_Integer index = 0;
    lua_createtable(L, static_cast<int>(view->fields.size()), 0);
    for (const auto& field : view->fields) {
        lua_createtable(L, 0, 3);
        record::text(L, "section", field.section);
        record::text(L, "key", field.key);
        record::text(L, "value", field.value);
        lua_rawseti(L, -2, ++index);
    }
    lua_setfield(L, -2, "fields");
    index = 0;
    lua_createtable(L, static_cast<int>(view->commands.size()), 0);
    for (const auto& command : view->commands) {
        lua_createtable(L, 0, 5);
        record::text(L, "name", command.name);
        record::text(L, "description", command.description);
        record::text(L, "origin", command.origin);
        push_strings(L, "aliases", command.aliases);
        push_strings(L, "groups", command.groups);
        lua_rawseti(L, -2, ++index);
    }
    lua_setfield(L, -2, "commands");
    scratch.release();
    return 1;
}

std::string_view edit_text(lua_State* L, const char* key, std::size_t max) {
    lua_pushstring(L, key);
    if (lua_rawget(L, -2) != LUA_TSTRING) luaL_error(L, "Management edit requires text for %s", key);
    std::size_t size = 0;
    const char* text = lua_tolstring(L, -1, &size);
    if (size > max) luaL_error(L, "Management edit is too large");
    lua_pop(L, 1);
    return {text, size};
}

int ServerManagementSave(lua_State* L) {
    const UiHost& host = host_of(L);
    if (!trusted(L, host) || !host.save_server_management) {
        lua_pushboolean(L, false);
        return 1;
    }
    std::size_t revision_size = 0;
    const char* revision = luaL_checklstring(L, 1, &revision_size);
    luaL_checktype(L, 2, LUA_TTABLE);
    const auto count = lua_rawlen(L, 2);
    if (revision_size > 10 || count == 0 || count > 64) {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& edits = core::lua_keep<std::vector<ServerManagementEdit>>(L);
    core::cxx_guard(L, [&] { edits.reserve(count); });
    for (std::size_t n = 1; n <= count; ++n) {
        if (lua_rawgeti(L, 2, static_cast<lua_Integer>(n)) != LUA_TTABLE)
            return luaL_error(L, "Management edit requires a table");
        const std::string_view section = edit_text(L, "section", 40);
        const std::string_view key = edit_text(L, "key", 40);
        const std::string_view value = edit_text(L, "value", 8192);
        core::cxx_guard(L, [&] { edits.push_back({std::string(section), std::string(key), std::string(value)}); });
        lua_pop(L, 1);
    }
    bool sent = false;
    core::cxx_guard(L, [&] { sent = host.save_server_management({revision, revision_size}, edits); });
    lua_pushboolean(L, sent);
    return 1;
}

} // namespace

void register_server_management_api(lua_State* L, UiHost& host) {
    static const luaL_Reg functions[] = {{"ServerManagementAvailable", ServerManagementAvailable},
                                         {"ServerManagementClose", ServerManagementClose},
                                         {"ServerManagementOpen", ServerManagementOpen},
                                         {"ServerManagementSave", ServerManagementSave},
                                         {"ServerManagementView", ServerManagementViewFn},
                                         {nullptr, nullptr}};
    lua_pushlightuserdata(L, &host);
    luaL_setfuncs(L, functions, 1);
}

} // namespace hailstone::app
