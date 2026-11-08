#include "game/guild_notes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace hailstone::game {

namespace {

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

// sscanf(line, "%X %s %[^\n", ...): true with all three fields.
bool scan(std::string_view line, std::uint32_t& time, std::string& name, std::string& note) {
    std::size_t i = 0;
    while (i < line.size() && is_space(line[i])) ++i;
    // %X: an optional sign and "0x", then hex digits (at least one)
    std::size_t start = i;
    if (i < line.size() && (line[i] == '+' || line[i] == '-')) ++i;
    if (i + 1 < line.size() && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X') && i + 2 < line.size() &&
        std::isxdigit(static_cast<unsigned char>(line[i + 2])))
        i += 2;
    std::size_t digits = i;
    std::uint32_t v = 0;
    while (i < line.size() && std::isxdigit(static_cast<unsigned char>(line[i]))) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));
        v = v * 16 + static_cast<std::uint32_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        ++i;
    }
    if (i == digits) return false;
    time = line[start] == '-' ? static_cast<std::uint32_t>(0u - v) : v;
    // " %s": blanks, then a word
    while (i < line.size() && is_space(line[i])) ++i;
    std::size_t w = i;
    while (i < line.size() && !is_space(line[i])) ++i;
    if (i == w) return false;
    name.assign(line.substr(w, i - w));
    // " %[^\n": blanks (newlines too), then everything up to a newline (at least one character)
    while (i < line.size() && is_space(line[i])) ++i;
    std::size_t n = i;
    while (i < line.size() && line[i] != '\n') ++i;
    if (i == n) return false;
    note.assign(line.substr(n, i - n));
    return true;
}

// Utf8TerminateWithinBuffer(s, n): at most n bytes, not cutting a UTF-8 sequence.
void cut_utf8(std::string& s, std::size_t n) {
    if (s.size() <= n) return;
    std::size_t end = n;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80) --end;
    s.resize(end);
}

} // namespace

void GuildPersonalNotes::parse(std::string_view text, std::uint32_t now_ticks) {
    entries.clear();
    std::size_t at = 0;
    while (at < text.size()) {
        // fgets(buf, 0x153): up to 0x152 characters, ending after a newline
        std::size_t end = at;
        while (end < text.size() && end - at < 0x152) {
            if (text[end++] == '\n') break;
        }
        std::string_view line = text.substr(at, end - at);
        at = end;
        std::uint32_t time = 0;
        std::string name, note;
        if (!scan(line, time, name, note)) continue;
        cut_utf8(name, 0x3f);
        cut_utf8(note, 0xff);
        auto it = std::find_if(entries.begin(), entries.end(), [&](const GuildPersonalNote& e) { return same_name(e.name, name); });
        if (it != entries.end()) {
            it->time = now_ticks;
            continue;
        }
        entries.push_back({time, std::move(name), std::move(note)});
    }
}

std::string GuildPersonalNotes::format() const {
    std::string out;
    for (const auto& e : entries) {
        char head[16];
        std::snprintf(head, sizeof head, "%08X ", e.time);
        out += head;
        out += e.name;
        out += ' ';
        out += e.note;
        out += '\n';
    }
    return out;
}

const std::string* GuildPersonalNotes::find(std::string_view name, std::uint32_t now_ticks) {
    for (auto& e : entries)
        if (same_name(e.name, name)) {
            e.time = now_ticks;
            return &e.note;
        }
    return nullptr;
}

const std::string* GuildPersonalNotes::get(std::string_view name) const {
    for (const auto& e : entries)
        if (same_name(e.name, name)) return &e.note;
    return nullptr;
}

void GuildPersonalNotes::set(std::string_view name, std::string_view note, std::uint32_t now_time) {
    for (auto& e : entries)
        if (same_name(e.name, name)) {
            e.note.assign(note);
            return;
        }
    entries.push_back({now_time, std::string(name), std::string(note)});
}

} // namespace hailstone::game
