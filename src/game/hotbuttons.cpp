#include "game/hotbuttons.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>

#include "core/ini_file.h"

namespace hailstone::game {

namespace {

// ParseDelimitedInt: an int up to the delimiter; returns the characters consumed (with the delimiter).
std::size_t parse_int(const std::string& s, std::size_t pos, std::int32_t& out) {
    std::size_t end = s.find(',', pos);
    std::string_view field(s.data() + pos, (end == std::string::npos ? s.size() : end) - pos);
    out = 0;
    std::from_chars(field.data(), field.data() + field.size(), out);
    return (end == std::string::npos ? s.size() : end + 1) - pos;
}

std::string key(int page, int button) {
    return "Page" + std::to_string(page + 1) + "Button" + std::to_string(button + 1);
}

} // namespace

std::string hot_button_section(int bar) { return bar == 0 ? "HotButtons" : "HotButtons" + std::to_string(bar + 1); }

std::string format_hot_button(const HotButton& b) {
    std::string s;
    s += static_cast<char>(static_cast<int>(b.type) + 'A');
    s += std::to_string(b.slot);
    s += ',';
    s += static_cast<char>(b.icon_type + 'A');
    s += std::to_string(b.icon_slot);
    s += ',';
    s += b.guid;
    s += ',';
    s += std::to_string(b.item_id);
    s += ',';
    s += b.label;
    return s;
}

HotButton parse_hot_button(const std::string& text) {
    HotButton b;
    if (text.empty()) return b;
    std::size_t pos = 0;
    b.type = static_cast<HotButtonType>(static_cast<std::uint8_t>(text[pos++] - 'A'));
    pos += parse_int(text, pos, b.slot);
    if (pos < text.size()) b.icon_type = text[pos++] - 'A';
    pos += parse_int(text, pos, b.icon_slot);
    // GetDelimitedToken(.., 0x12, ','): the guid, at most 17 characters.
    std::size_t comma = text.find(',', pos);
    std::string guid = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if (guid.size() > 17) guid.resize(17);
    b.guid = guid;
    pos = comma == std::string::npos ? text.size() : comma + 1;
    pos += parse_int(text, pos, b.item_id);
    b.label = pos < text.size() ? text.substr(pos, 63) : std::string();
    return b;
}

HotButton& HotButtonStore::at(int bar, int page, int button) {
    return buttons_[static_cast<std::size_t>((bar * kHotPages + page) * kHotButtons + button)];
}

const HotButton& HotButtonStore::at(int bar, int page, int button) const {
    return buttons_[static_cast<std::size_t>((bar * kHotPages + page) * kHotButtons + button)];
}

void HotButtonStore::set_page(int bar, int page) {
    if (bar < 0 || bar >= kHotBars) return;
    pages_[static_cast<std::size_t>(bar)] = (page % kHotPages + kHotPages) % kHotPages;
}

void HotButtonStore::load(const IniFile& ini) {
    for (int bar = 0; bar < kHotBars; ++bar)
        for (int page = 0; page < kHotPages; ++page)
            for (int button = 0; button < kHotButtons; ++button) {
                // A missing key reads as "*" (the default at 0x9c572c) and leaves the button empty.
                std::string v = ini.get_string(hot_button_section(bar), key(page, button), "*");
                at(bar, page, button) = v.empty() || v[0] == '*' ? HotButton{} : parse_hot_button(v);
            }
    for (int page = 0; page < kSocialPages; ++page)
        for (int button = 0; button < kSocialButtons; ++button) {
            Social& s = social(page * kSocialButtons + button);
            std::string base = key(page, button);
            std::string name = ini.get_string("Socials", base + "Name", "*");
            s = Social{};
            if (name.empty() || name[0] == '*') continue;
            s.name = name.substr(0, 15);
            s.color = ini.get_int("Socials", base + "Color", 0);
            for (int l = 0; l < kSocialLines; ++l)
                s.lines[static_cast<std::size_t>(l)] = ini.get_string("Socials", base + "Line" + std::to_string(l + 1));
        }
    for (int i = 0; i < 6; ++i) abilities[static_cast<std::size_t>(i)] = ini.get_int("Abilities", "Ability" + std::to_string(i + 1), -1);
    for (int i = 0; i < 4; ++i)
        combat_skills[static_cast<std::size_t>(i)] = ini.get_int("CombatSkills", "CombatSkill" + std::to_string(i + 1), -1);
    for (int i = 0; i < kCombatAbilityButtons; ++i)
        combat_abilities[static_cast<std::size_t>(i)] =
            ini.get_int("CombatAbilities", "CombatAbility" + std::to_string(i + 1), -1);
}

void HotButtonStore::save(IniFile& ini) const {
    for (int bar = 0; bar < kHotBars; ++bar)
        for (int page = 0; page < kHotPages; ++page)
            for (int button = 0; button < kHotButtons; ++button) {
                const HotButton& b = at(bar, page, button);
                // SaveHotButtonsToIni writes empty buttons as an empty string; dropping the key reads back the same.
                if (b.empty()) ini.erase(hot_button_section(bar), key(page, button));
                else ini.set(hot_button_section(bar), key(page, button), format_hot_button(b));
            }
    for (int page = 0; page < kSocialPages; ++page)
        for (int button = 0; button < kSocialButtons; ++button) {
            const Social& s = social(page * kSocialButtons + button);
            std::string base = key(page, button);
            if (s.empty()) {
                ini.erase("Socials", base + "Name");
                ini.erase("Socials", base + "Color");
                for (int l = 0; l < kSocialLines; ++l) ini.erase("Socials", base + "Line" + std::to_string(l + 1));
                continue;
            }
            ini.set("Socials", base + "Name", s.name);
            ini.set_int("Socials", base + "Color", s.color);
            for (int l = 0; l < kSocialLines; ++l) {
                const std::string& line = s.lines[static_cast<std::size_t>(l)];
                if (line.empty()) ini.erase("Socials", base + "Line" + std::to_string(l + 1));
                else ini.set("Socials", base + "Line" + std::to_string(l + 1), line);
            }
        }
    for (int i = 0; i < 6; ++i) ini.set_int("Abilities", "Ability" + std::to_string(i + 1), abilities[static_cast<std::size_t>(i)]);
    for (int i = 0; i < 4; ++i)
        ini.set_int("CombatSkills", "CombatSkill" + std::to_string(i + 1), combat_skills[static_cast<std::size_t>(i)]);
    // SaveCombatAbilitiesToIni @0x546930.
    for (int i = 0; i < kCombatAbilityButtons; ++i)
        ini.set_int("CombatAbilities", "CombatAbility" + std::to_string(i + 1),
                    combat_abilities[static_cast<std::size_t>(i)]);
}

void HotButtonStore::load_lua(const std::filesystem::path& dir) {
    for (int i = 0; i < kSocialPages * kSocialButtons; ++i) {
        Social& s = social(i);
        s.lua = {};
        std::ifstream f(dir / lua_social_file_name(i), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto parsed = parse_lua_social(text);
        if (!parsed) continue;
        s.lua = parsed->lua;
        if (s.name.empty()) {
            s.name = parsed->name.empty() ? "Lua" : parsed->name;
            s.color = parsed->color;
        }
    }
}

bool HotButtonStore::save_lua(const std::filesystem::path& dir) const {
    bool ok = true;
    std::error_code ec;
    for (int i = 0; i < kSocialPages * kSocialButtons; ++i) {
        const Social& s = social(i);
        const auto path = dir / lua_social_file_name(i);
        if (s.empty() || !s.is_lua()) {
            std::filesystem::remove(path, ec);
            continue;
        }
        std::string text = format_lua_social({s.name, s.color, s.lua, {}});
        std::ifstream in(path, std::ios::binary);
        if (in && std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) == text) continue;
        in.close();
        std::filesystem::create_directories(dir, ec);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << text;
        ok = ok && static_cast<bool>(f);
    }
    return ok;
}

int store_social(HotButtonStore& store, std::string_view name, std::string_view line, std::int32_t color,
                 std::int32_t start, bool same_name) {
    if (start < 0 || start >= kSocialPages * kSocialButtons) start = 0;
    auto same = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    for (int index = start; index < kSocialPages * kSocialButtons; ++index) {
        Social& s = store.social(index);
        if (!s.empty() && !(same_name && same(s.name, name))) continue;
        s.name = std::string(name.substr(0, 15));
        s.color = static_cast<std::int32_t>(static_cast<std::uint8_t>(color)); // a byte in the record
        s.lines[0] = std::string(line.substr(0, 255));
        s.cooldown_start = s.cooldown_length = 0;
        s.lua = {}; // Hailstone: a Lua social of that name becomes the line
        return index;
    }
    return -1;
}

} // namespace hailstone::game
