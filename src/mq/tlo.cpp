#include "mq/tlo.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>

#include "core/float_chars.h"

namespace nmsc::mq {

namespace {

char low(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = low(c);
    return out;
}

} // namespace

bool CaseLess::operator()(std::string_view a, std::string_view b) const {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) { return low(x) < low(y); });
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return low(x) == low(y); });
}

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char x, char y) { return low(x) == low(y); });
    return it != haystack.end();
}

std::vector<std::string> split_index(std::string_view index) {
    std::vector<std::string> out;
    if (trim(index).empty()) return out;
    while (true) {
        std::size_t comma = index.find(',');
        out.emplace_back(trim(index.substr(0, comma)));
        if (comma == std::string_view::npos) break;
        index.remove_prefix(comma + 1);
    }
    return out;
}

bool parse_int(std::string_view text, std::int64_t& out) {
    text = trim(text);
    if (text.empty()) return false;
    auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && p == text.data() + text.size();
}

bool parse_float(std::string_view text, double& out) {
    text = trim(text);
    if (text.empty()) return false;
    auto [p, ec] = float_from_chars(text.data(), text.data() + text.size(), out); // (core/float_chars.h: Xcode 15)
    return ec == std::errc() && p == text.data() + text.size();
}

std::optional<std::int64_t> whole_number(double d) {
    if (!(d >= -0x1p63 && d < 0x1p63)) return std::nullopt; // NaN fails both
    return static_cast<std::int64_t>(d);
}

Registry::Registry() {
    // MQ's pure types and their common members.
    Type& t_int = type("int");
    Type& t_float = type("float");
    Type& t_string = type("string");
    Type& t_bool = type("bool");
    int_ = &t_int;
    float_ = &t_float;
    string_ = &t_string;
    bool_ = &t_bool;
    t_int.members["Float"] = [](const Context&, const Value& v, std::string_view) { return Value::number(double(v.i)); };
    t_int.members["Hex"] = [](const Context&, const Value& v, std::string_view) {
        return Value::string(std::format("0x{:x}", v.i));
    };
    t_int.members["Reverse"] = [](const Context&, const Value& v, std::string_view) {
        auto u = static_cast<std::uint32_t>(v.i);
        return Value::integer((u >> 24) | ((u >> 8) & 0xff00) | ((u << 8) & 0xff0000) | (u << 24));
    };
    t_float.members["Int"] = [](const Context&, const Value& v, std::string_view) {
        const auto n = whole_number(v.f);
        return n ? Value::integer(*n) : Value::nil();
    };
    t_float.members["Deci"] = [](const Context&, const Value& v, std::string_view) {
        return Value::string(std::format("{:.1f}", v.f));
    };
    t_float.members["Centi"] = [](const Context&, const Value& v, std::string_view) {
        return Value::string(std::format("{:.2f}", v.f));
    };
    t_float.members["Milli"] = [](const Context&, const Value& v, std::string_view) {
        return Value::string(std::format("{:.3f}", v.f));
    };
    t_float.members["Precision"] = [](const Context&, const Value& v, std::string_view index) {
        std::int64_t n = 2;
        parse_int(index, n);
        return Value::string(std::format("{:.{}f}", v.f, static_cast<int>(std::clamp<std::int64_t>(n, 0, 12))));
    };
    t_float.members["Raw"] = [](const Context&, const Value& v, std::string_view) {
        float f = static_cast<float>(v.f);
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return Value::integer(bits);
    };
    t_string.members["Length"] = [](const Context&, const Value& v, std::string_view) {
        return Value::integer(static_cast<std::int64_t>(v.s.size()));
    };
    t_string.members["Upper"] = [](const Context&, const Value& v, std::string_view) {
        std::string s = v.s;
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value::string(s);
    };
    t_string.members["Lower"] = [](const Context&, const Value& v, std::string_view) { return Value::string(lower(v.s)); };
    t_string.members["Left"] = [](const Context&, const Value& v, std::string_view index) {
        std::int64_t n = 0;
        if (!parse_int(index, n)) return Value::nil();
        auto len = static_cast<std::int64_t>(v.s.size());
        n = n < 0 ? std::max<std::int64_t>(0, len + n) : std::min(n, len);
        return Value::string(v.s.substr(0, static_cast<std::size_t>(n)));
    };
    t_string.members["Right"] = [](const Context&, const Value& v, std::string_view index) {
        std::int64_t n = 0;
        if (!parse_int(index, n)) return Value::nil();
        auto len = static_cast<std::int64_t>(v.s.size());
        n = n < 0 ? std::max<std::int64_t>(0, len + n) : std::min(n, len);
        return Value::string(v.s.substr(static_cast<std::size_t>(len - n)));
    };
    t_string.members["Mid"] = [](const Context&, const Value& v, std::string_view index) {
        auto a = split_index(index);
        std::int64_t start = 0, n = 0;
        if (a.size() != 2 || !parse_int(a[0], start) || !parse_int(a[1], n) || start < 1) return Value::nil();
        if (static_cast<std::size_t>(start - 1) >= v.s.size()) return Value::string("");
        return Value::string(v.s.substr(static_cast<std::size_t>(start - 1), static_cast<std::size_t>(std::max<std::int64_t>(0, n))));
    };
    t_string.members["Find"] = [](const Context&, const Value& v, std::string_view index) {
        std::string hay = lower(v.s), needle = lower(index);
        auto at = hay.find(needle);
        return at == std::string::npos ? Value::nil() : Value::integer(static_cast<std::int64_t>(at) + 1);
    };
    t_string.members["Equal"] = [](const Context&, const Value& v, std::string_view index) {
        return Value::boolean(iequals(v.s, index));
    };
    t_string.members["NotEqual"] = [](const Context&, const Value& v, std::string_view index) {
        return Value::boolean(!iequals(v.s, index));
    };
    t_string.members["EqualCS"] = [](const Context&, const Value& v, std::string_view index) {
        return Value::boolean(v.s == index);
    };
    t_string.members["Arg"] = [](const Context&, const Value& v, std::string_view index) {
        auto a = split_index(index);
        std::int64_t n = 0;
        if (a.empty() || !parse_int(a[0], n) || n < 1) return Value::nil();
        char sep = a.size() > 1 && !a[1].empty() ? a[1][0] : ' ';
        std::vector<std::string> words;
        std::string cur;
        for (char c : v.s) {
            if (c == sep) {
                if (!cur.empty() || sep != ' ') words.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) words.push_back(cur);
        return n <= static_cast<std::int64_t>(words.size()) ? Value::string(words[static_cast<std::size_t>(n - 1)])
                                                           : Value::nil();
    };
    t_string.members["Count"] = [](const Context&, const Value& v, std::string_view index) {
        if (index.empty()) return Value::integer(0);
        return Value::integer(std::count(v.s.begin(), v.s.end(), index[0]));
    };
    t_string.members["Token"] = [](const Context&, const Value& v, std::string_view index) {
        auto a = split_index(index);
        std::int64_t n = 0;
        if (a.size() < 2 || !parse_int(a[0], n) || n < 1 || a[1].empty()) return Value::nil();
        std::size_t start = 0;
        for (std::int64_t k = 1; k < n; ++k) {
            start = v.s.find(a[1][0], start);
            if (start == std::string::npos) return Value::nil();
            ++start;
        }
        return Value::string(v.s.substr(start, v.s.find(a[1][0], start) - start));
    };
}

Registry::~Registry() = default;

Type& Registry::type(const std::string& name) {
    auto it = types_.find(name);
    if (it != types_.end()) return *it->second;
    auto t = std::make_unique<Type>();
    t->name = name;
    Type& ref = *t;
    types_.emplace(name, std::move(t));
    return ref;
}

const Type* Registry::find_type(std::string_view name) const {
    auto it = types_.find(name);
    return it == types_.end() ? nullptr : it->second.get();
}

void Registry::add_tlo(std::string name, TloFn fn) { tlos_[std::move(name)] = std::move(fn); }

void Registry::remove_tlo(std::string_view name) {
    auto it = tlos_.find(name);
    if (it != tlos_.end()) tlos_.erase(it);
}

bool Registry::has_tlo(std::string_view name) const { return tlos_.find(name) != tlos_.end(); }

std::vector<std::string> Registry::tlos() const {
    std::vector<std::string> out;
    for (const auto& [name, fn] : tlos_) out.push_back(name);
    return out;
}

std::vector<std::string> Registry::complete(const Context& ctx, std::string_view path) const {
    // The parts: name[index] separated by dots outside brackets.
    struct Part {
        std::string name, index;
    };
    std::vector<Part> parts(1);
    int depth = 0;
    for (char c : path) {
        if (c == '[' && depth++ == 0) continue;
        if (c == ']' && depth > 0 && --depth == 0) continue;
        if (depth > 0) parts.back().index += c;
        else if (c == '.') parts.emplace_back();
        else parts.back().name += c;
    }
    const std::string prefix = parts.back().name;
    auto starts = [&](std::string_view name) {
        if (name.size() < prefix.size()) return false;
        for (std::size_t i = 0; i < prefix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        return true;
    };
    std::vector<std::string> out;
    if (parts.size() == 1) {
        for (const auto& [name, fn] : tlos_)
            if (starts(name)) out.push_back(name);
        return out;
    }
    Value v = tlo(ctx, parts[0].name, parts[0].index);
    const Type* t = nullptr;
    if (v.is_nil() && parts.size() == 2) {
        // The type a NULL TLO has when it is there.
        static constexpr std::pair<std::string_view, std::string_view> kTypes[] = {
            {"Me", "character"}, {"Target", "spawn"}, {"Spawn", "spawn"}, {"NearestSpawn", "spawn"}, {"Pet", "spawn"},
            {"Group", "group"}, {"Zone", "zone"}, {"Spell", "spell"}, {"Cursor", "item"}, {"FindItem", "item"}};
        for (auto [tlo_name, type_name] : kTypes)
            if (CaseLess{}(tlo_name, parts[0].name) == CaseLess{}(parts[0].name, tlo_name)) t = find_type(type_name);
    }
    for (std::size_t i = 1; i + 1 < parts.size() && !v.is_nil(); ++i) v = member(ctx, v, parts[i].name, parts[i].index);
    if (!t) {
        switch (v.kind) {
        case Value::Kind::Nil: return out;
        case Value::Kind::Bool: t = bool_; break;
        case Value::Kind::Int: t = int_; break;
        case Value::Kind::Float: t = float_; break;
        case Value::Kind::String: t = string_; break;
        case Value::Kind::Object: t = v.type; break;
        }
    }
    for (int d = 0; t && d < 8; ++d, t = t->parent)
        for (const auto& [name, fn] : t->members)
            if (starts(name)) out.push_back(name);
    std::sort(out.begin(), out.end(), CaseLess{});
    out.erase(std::unique(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
                  return !CaseLess{}(a, b) && !CaseLess{}(b, a);
              }),
              out.end());
    return out;
}

Value Registry::tlo(const Context& ctx, std::string_view name, std::string_view index) const {
    auto it = tlos_.find(name);
    if (it == tlos_.end()) return Value::nil();
    return it->second(ctx, index);
}

Value Registry::member(const Context& ctx, const Value& self, std::string_view name, std::string_view index) const {
    const Type* t = nullptr;
    switch (self.kind) {
    case Value::Kind::Nil: return Value::nil();
    case Value::Kind::Bool: t = bool_; break;
    case Value::Kind::Int: t = int_; break;
    case Value::Kind::Float: t = float_; break;
    case Value::Kind::String: t = string_; break;
    case Value::Kind::Object: t = self.type; break;
    }
    Value v = self;
    for (int depth = 0; t && depth < 8; ++depth) {
        auto it = t->members.find(name);
        if (it != t->members.end()) return it->second(ctx, v, index);
        if (t->any_member) {
            Value found = t->any_member(ctx, v, name, index);
            if (!found.is_nil()) return found;
        }
        if (t->convert) v = t->convert(ctx, v);
        if (v.is_nil()) return v;
        t = t->parent;
    }
    // An object without the member: its plain value may have it (Target.Name.Length).
    if (self.kind == Value::Kind::Object) {
        Value p = plain(ctx, self);
        if (p.kind != Value::Kind::Object && !p.is_nil()) return member(ctx, p, name, index);
    }
    return Value::nil();
}

Value Registry::plain(const Context& ctx, const Value& v) const {
    Value x = v;
    for (int depth = 0; x.kind == Value::Kind::Object && depth < 8; ++depth) {
        const Type* t = x.type;
        while (t && !t->to_value) t = t->parent;
        if (!t) return Value::string(x.type->name);
        x = t->to_value(ctx, x);
    }
    return x.kind == Value::Kind::Object ? Value::string(x.type->name) : x;
}

std::string Registry::text(const Context& ctx, const Value& v) const {
    Value p = plain(ctx, v);
    switch (p.kind) {
    case Value::Kind::Nil: return "NULL";
    case Value::Kind::Bool: return p.b ? "TRUE" : "FALSE";
    case Value::Kind::Int: return std::to_string(p.i);
    case Value::Kind::Float: return std::format("{:.2f}", p.f);
    case Value::Kind::String: return p.s;
    case Value::Kind::Object: break;
    }
    return "NULL";
}

} // namespace nmsc::mq
