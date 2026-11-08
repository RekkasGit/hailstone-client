#include "game/lua_social.h"

#include <array>
#include <charconv>
#include <cstdint>

namespace hailstone::game {

namespace {

constexpr std::string_view kHeader = "-- Hailstone Lua social";
constexpr std::string_view kPress = "-- [press]";
constexpr std::string_view kFace = "-- [face]";
constexpr std::string_view kLines = "-- [lines]";
constexpr std::size_t kMaxLines = 5, kMaxLine = 255; // the social editor's

std::string normalise(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string_view trim_end(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return trim_end(s);
}

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(std::string_view in) {
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        std::uint32_t v = static_cast<std::uint8_t>(in[i]) << 16 | static_cast<std::uint8_t>(in[i + 1]) << 8 |
                          static_cast<std::uint8_t>(in[i + 2]);
        for (int s = 18; s >= 0; s -= 6) out += kBase64[(v >> s) & 63];
    }
    if (std::size_t rest = in.size() - i; rest > 0) {
        std::uint32_t v = static_cast<std::uint8_t>(in[i]) << 16;
        if (rest == 2) v |= static_cast<std::uint8_t>(in[i + 1]) << 8;
        out += kBase64[(v >> 18) & 63];
        out += kBase64[(v >> 12) & 63];
        out += rest == 2 ? kBase64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::string> unbase64(std::string_view in) {
    std::string out;
    std::uint32_t v = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        auto at = kBase64.find(c);
        if (at == std::string_view::npos) return std::nullopt;
        v = v << 6 | static_cast<std::uint32_t>(at);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((v >> bits) & 0xff);
        }
    }
    return out;
}

} // namespace

std::string format_lua_social(const LuaSocialFile& f) {
    std::string out;
    out += kHeader;
    out += "\n-- name: " + f.name + "\n-- color: " + std::to_string(f.color) + "\n";
    std::size_t last = 0;
    for (std::size_t i = 0; i < f.lines.size() && i < kMaxLines; ++i)
        if (!f.lines[i].empty()) last = i + 1;
    if (f.lua.empty() && last > 0) {
        out += kLines;
        out += '\n';
        for (std::size_t i = 0; i < last; ++i) {
            std::string line = normalise(f.lines[i].substr(0, kMaxLine));
            for (char& c : line)
                if (c == '\n') c = ' '; // (a line holds no line end)
            out += line + '\n';
        }
        return out;
    }
    out += kPress;
    out += '\n';
    std::string press = std::string(trim_end(normalise(f.lua.press)));
    if (!press.empty()) out += press + '\n';
    std::string face = std::string(trim_end(normalise(f.lua.face)));
    if (!face.empty()) {
        out += kFace;
        out += '\n';
        out += face + '\n';
    }
    return out;
}

std::optional<LuaSocialFile> parse_lua_social(std::string_view given) {
    std::string text = normalise(given);
    LuaSocialFile f;
    std::string* into = nullptr;
    bool lines = false; // in the "-- [lines]" section
    bool any = false;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + at, end - at);
        at = end + 1;
        std::string_view t = trim(line);
        if (t == kPress) {
            into = &f.lua.press;
            lines = false;
            any = true;
        } else if (t == kFace) {
            into = &f.lua.face;
            lines = false;
            any = true;
        } else if (t == kLines) {
            into = nullptr;
            lines = true;
            any = true;
        } else if (lines) {
            if (end == text.size() && line.empty()) break; // (the end after the last line's line end)
            f.lines.emplace_back(line.substr(0, kMaxLine));
        } else if (into) {
            *into += line;
            *into += '\n';
        } else if (t.starts_with("-- name:")) {
            f.name = std::string(trim(t.substr(8)));
        } else if (t.starts_with("-- color:")) {
            std::string_view n = trim(t.substr(9));
            std::from_chars(n.data(), n.data() + n.size(), f.color);
        }
        if (end == text.size()) break;
    }
    if (!any) return std::nullopt;
    while (!f.lines.empty() && f.lines.back().empty()) f.lines.pop_back();
    if (f.lines.size() > kMaxLines) f.lines.resize(kMaxLines);
    if (!f.lua.empty()) f.lines.clear();
    f.lua.press = std::string(trim_end(f.lua.press));
    f.lua.face = std::string(trim_end(f.lua.face));
    if (f.name.size() > 15) f.name.resize(15);
    return f;
}

Sha256Digest lua_social_hash(const LuaSocial& s) {
    Sha256 h;
    h.update(kPress);
    h.update("\n");
    h.update(trim_end(normalise(s.press)));
    h.update("\n");
    h.update(kFace);
    h.update("\n");
    h.update(trim_end(normalise(s.face)));
    return h.finish();
}

std::string export_lua_social(const LuaSocialFile& f) {
    return std::string(kLuaSocialSharePrefix) + base64(format_lua_social(f));
}

std::optional<LuaSocialFile> import_lua_social(std::string_view text) {
    text = trim(text);
    if (!text.starts_with(kLuaSocialSharePrefix)) return std::nullopt;
    auto file = unbase64(text.substr(kLuaSocialSharePrefix.size()));
    if (!file) return std::nullopt;
    return parse_lua_social(*file);
}

std::string lua_social_file_name(int index) { return std::to_string(index + 1) + ".lua"; }

} // namespace hailstone::game
