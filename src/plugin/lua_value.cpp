#include "plugin/lua_value.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <lua.hpp>

#include "core/log.h"

namespace hailstone::plugin {

namespace {

// A table key written as a whole number ("12", "-3"; not "012" or "+1"): to_value makes a Lua table's number keys
// such strings, and push_value makes them numbers again, so a table keyed by numbers survives both ways.
std::optional<lua_Integer> integer_key(const std::string& k) {
    std::size_t digits = k.size() - (k.empty() || k[0] != '-' ? 0 : 1);
    if (digits == 0 || (digits > 1 && k[k.size() - digits] == '0')) return std::nullopt;
    lua_Integer v = 0;
    auto [end, ec] = std::from_chars(k.data(), k.data() + k.size(), v);
    if (ec != std::errc() || end != k.data() + k.size()) return std::nullopt;
    return v;
}

// A number key's text as Lua's tostring gives it ("12", "1.5", "2.0"), made here rather than with lua_tolstring on
// a copy: that makes a Lua string, and an allocation that fails raises (Lua is built as C) past this file's
// vectors and strings. LUA_INTEGER_FMT and LUA_NUMBER_FMT, with lobject.c's ".0" for a float that looks whole.
std::string number_key(lua_State* L, int idx) {
    char buf[64];
    if (lua_isinteger(L, idx)) {
        std::snprintf(buf, sizeof buf, LUA_INTEGER_FMT, static_cast<LUAI_UACINT>(lua_tointeger(L, idx)));
        return buf;
    }
    std::string out(buf, static_cast<std::size_t>(std::max(0, std::snprintf(buf, sizeof buf, LUA_NUMBER_FMT,
                                                                            static_cast<LUAI_UACNUMBER>(lua_tonumber(L, idx))))));
    if (out.find_first_not_of("-0123456789") == std::string::npos) out += std::string(1, lua_getlocaledecpoint()) + "0";
    return out;
}

} // namespace

namespace {

// What a conversion may still take, and the tables it is inside (a table met again on its own path holds itself).
struct Budget {
    std::size_t nodes = kMaxValueNodes, bytes = kMaxValueBytes;
    std::vector<const void*> path;
    std::string why; // set: refused
    bool take(std::size_t n_bytes) {
        if (!why.empty()) return false;
        if (nodes == 0) why = "it has more than " + std::to_string(kMaxValueNodes) + " values";
        else if (n_bytes > bytes) why = "its text is longer than " + std::to_string(kMaxValueBytes) + " bytes";
        if (!why.empty()) return false;
        --nodes;
        bytes -= n_bytes;
        return true;
    }
};

UiValue convert(lua_State* L, int idx, int depth, Budget& b) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        if (!b.take(0)) return {};
        return UiValue::boolean(lua_toboolean(L, idx) != 0);
    case LUA_TNUMBER:
        if (!b.take(0)) return {};
        if (lua_isinteger(L, idx)) return UiValue::integer(lua_tointeger(L, idx));
        return UiValue::number(lua_tonumber(L, idx));
    case LUA_TSTRING: {
        std::size_t n = 0;
        const char* text = lua_tolstring(L, idx, &n);
        if (!b.take(n)) return {};
        return UiValue::string(std::string(text, n));
    }
    case LUA_TTABLE: {
        if (!b.take(0)) return {};
        // Each level takes up to three slots (the key, its value, a copy of the key). lua_checkstack, not
        // luaL_checkstack: an error would longjmp past the destructors of the levels above.
        if (depth >= kMaxValueDepth || !lua_checkstack(L, 4)) return {};
        const void* self = lua_topointer(L, idx);
        if (std::find(b.path.begin(), b.path.end(), self) != b.path.end()) {
            b.why = "a table holds itself";
            return {};
        }
        b.path.push_back(self);
        const lua_Unsigned n = lua_rawlen(L, idx);
        bool array = true;
        std::size_t keys = 0;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            ++keys;
            if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 ||
                static_cast<lua_Unsigned>(lua_tointeger(L, -2)) > n)
                array = false;
            lua_pop(L, 1);
        }
        UiValue v;
        if (array && keys == n) {
            v.kind = UiValue::Kind::Array;
            for (lua_Unsigned i = 1; i <= n && b.why.empty(); ++i) {
                lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
                v.items.push_back(convert(L, -1, depth + 1, b));
                lua_pop(L, 1);
            }
            b.path.pop_back();
            return v;
        }
        v.kind = UiValue::Kind::Table;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            if (b.why.empty() && (lua_type(L, -2) == LUA_TSTRING || lua_type(L, -2) == LUA_TNUMBER)) {
                // A number key's text made here: lua_next needs the key as it was, and a string made from a copy
                // could raise. Nothing here raises (Lua is built as C: this frame's objects would leak).
                std::string key;
                if (lua_type(L, -2) == LUA_TSTRING) {
                    std::size_t len = 0;
                    const char* kt = lua_tolstring(L, -2, &len);
                    key.assign(kt, len);
                } else {
                    key = number_key(L, -2);
                }
                const std::size_t kn = key.size();
                if (kn <= b.bytes) {
                    b.bytes -= kn;
                    v.fields.emplace_back(std::move(key), convert(L, -1, depth + 1, b));
                } else {
                    b.take(kn);
                }
            }
            lua_pop(L, 1);
        }
        b.path.pop_back();
        return v;
    }
    default:
        b.take(0);
        return {};
    }
}

// Whether v is within the budget (what push_value would make of it: below kMaxValueDepth only).
bool fits(const UiValue& v, int depth, Budget& b) {
    switch (v.kind) {
    case UiValue::Kind::String: return b.take(v.s.size());
    case UiValue::Kind::Array:
    case UiValue::Kind::Table:
        if (!b.take(0)) return false;
        if (depth >= kMaxValueDepth) return true;
        for (const auto& i : v.items)
            if (!fits(i, depth + 1, b)) return false;
        for (const auto& [k, f] : v.fields)
            if (!b.take(k.size()) || !fits(f, depth + 1, b)) return false;
        return true;
    default: return b.take(0);
    }
}

void push(lua_State* L, const UiValue& v, int depth);

} // namespace

std::optional<UiValue> to_value_checked(lua_State* L, int idx, std::string* why) {
    Budget b;
    const int top = lua_gettop(L);
    UiValue v = convert(L, idx, 0, b);
    lua_settop(L, top);
    if (!b.why.empty()) {
        if (why) *why = b.why;
        return std::nullopt;
    }
    return v;
}

UiValue to_value(lua_State* L, int idx, int depth) {
    Budget b;
    const int top = lua_gettop(L);
    UiValue v = convert(L, idx, depth, b);
    lua_settop(L, top);
    if (b.why.empty()) return v;
    log::warn("a Lua value passed to or from a plugin was left out: {}", b.why);
    return {};
}

namespace {
void push(lua_State* L, const UiValue& v, int depth) {
    // A table or array below the depth limit, or with no room left on the stack, is nil. Each level takes two
    // slots (its table and the value being set); the caller's slot holds this one.
    if ((v.kind == UiValue::Kind::Array || v.kind == UiValue::Kind::Table) &&
        (depth >= kMaxValueDepth || !lua_checkstack(L, 2))) {
        lua_pushnil(L);
        return;
    }
    switch (v.kind) {
    case UiValue::Kind::Nil: lua_pushnil(L); break;
    case UiValue::Kind::Bool: lua_pushboolean(L, v.b); break;
    case UiValue::Kind::Int: lua_pushinteger(L, v.i); break;
    case UiValue::Kind::Number: lua_pushnumber(L, v.n); break;
    case UiValue::Kind::String: lua_pushlstring(L, v.s.data(), v.s.size()); break;
    case UiValue::Kind::Array:
        lua_createtable(L, static_cast<int>(v.items.size()), 0);
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            push(L, v.items[i], depth + 1);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        break;
    case UiValue::Kind::Table:
        lua_createtable(L, 0, static_cast<int>(v.fields.size()));
        for (const auto& [k, f] : v.fields) {
            if (auto i = integer_key(k)) {
                push(L, f, depth + 1);
                lua_rawseti(L, -2, *i);
                continue;
            }
            push(L, f, depth + 1);
            lua_setfield(L, -2, k.c_str());
        }
        break;
    }
}

} // namespace

void push_value(lua_State* L, const UiValue& v, int depth) {
    bool ok = true;
    { // the budget is gone before anything is pushed: a push that fails raises past this frame
        Budget b;
        if (!fits(v, depth, b)) {
            log::warn("a value passed to Lua was left out: {}", b.why);
            ok = false;
        }
    }
    if (ok) push(L, v, depth);
    else lua_pushnil(L);
}

} // namespace hailstone::plugin
