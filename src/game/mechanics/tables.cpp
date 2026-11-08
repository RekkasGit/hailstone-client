#include "game/mechanics/tables.h"

#include <cstdlib>
#include <string>

#include "core/game_dir.h"

namespace hailstone::game::mechanics {

namespace {

std::string_view as_text(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The lines of a text file (TextFileReader: '\n' ends a line, a '\r' before it is dropped).
template <typename F> void for_each_line(std::string_view text, F&& f) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) f(line);
        pos = end + 1;
    }
}

// The '^'-separated fields of a line.
std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    for (;;) {
        std::size_t caret = line.find('^', pos);
        out.emplace_back(line.substr(pos, caret == std::string_view::npos ? std::string_view::npos : caret - pos));
        if (caret == std::string_view::npos) break;
        pos = caret + 1;
    }
    return out;
}

// atoi: leading spaces, sign, digits; 0 for anything else.
std::int32_t c_atoi(const std::string& s) { return static_cast<std::int32_t>(std::strtol(s.c_str(), nullptr, 10)); }
// atof (correctly rounded, as the original's CRT is for these short decimals).
double c_atof(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

std::uint64_t key(int level, std::uint32_t cls) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(level)) << 32) | cls;
}

struct SkillFlags {
    bool level_capped;
    bool activated;
    std::uint32_t name;
};
constexpr SkillFlags kSkillFlags[kSkillCount] = {
#define HAILSTONE_SKILL(id, capped, activated, name) {capped, activated, name},
#include "game/mechanics/skill_info.inc"
#undef HAILSTONE_SKILL
};

// The byte table at 0x5ba25c (skills 0..77): 1 selects "held at 252".
constexpr std::uint8_t kHeldAt252[78] = {
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0,
};

} // namespace

// ---- ClassLevelTable ----

bool ClassLevelTable::load(const GameDir& game) {
    auto bytes = game.read("Resources/BaseData.txt");
    if (!bytes) return false;
    return parse(as_text(*bytes)) > 0;
}

std::size_t ClassLevelTable::parse(std::string_view text) {
    rows_.clear();
    std::size_t n = 0;
    for_each_line(text, [&](std::string_view line) {
        auto f = fields(line);
        f.resize(10);
        auto level = static_cast<std::uint32_t>(c_atoi(f[0]));
        if (level > 100) return; // above 100 @0x7ea530
        auto cls = static_cast<std::uint32_t>(c_atoi(f[1]));
        ClassLevelRow r;
        r.base_hp = c_atof(f[2]);
        r.base_mana = c_atof(f[3]);
        r.base_endurance = c_atof(f[4]);
        r.hp_regen = c_atof(f[5]);
        r.endurance_regen = c_atof(f[6]);
        r.hp_per_stamina = c_atof(f[7]);
        r.mana_per_stat = c_atof(f[8]);
        r.endurance_per_stat = c_atof(f[9]);
        rows_[key(static_cast<int>(level), cls)] = r; // the newest line is found first
        ++n;
    });
    return n;
}

void ClassLevelTable::set(int level, std::uint32_t cls, const ClassLevelRow& r) { rows_[key(level, cls)] = r; }

const ClassLevelRow* ClassLevelTable::row(int level, std::uint32_t cls) const {
    if (static_cast<std::uint32_t>(level) - 1u > 99) return nullptr;
    auto it = rows_.find(key(level, cls));
    return it == rows_.end() ? nullptr : &it->second;
}

// ---- SkillCapTable ----

bool SkillCapTable::load(const GameDir& game) {
    auto bytes = game.read("Resources/SkillCaps.txt");
    if (!bytes) return false;
    return parse(as_text(*bytes)) > 0;
}

std::size_t SkillCapTable::parse(std::string_view text) {
    caps_.assign(static_cast<std::size_t>(kCapClasses) * kSkillCount * kCapLevels, 0);
    std::size_t n = 0;
    auto number = [](const std::string& s) -> std::int32_t {
        if (s.size() > 1 && s[0] == '0' && s[1] == 'x')
            return static_cast<std::int32_t>(std::strtol(s.c_str(), nullptr, 16));
        return c_atoi(s);
    };
    for_each_line(text, [&](std::string_view line) {
        auto f = fields(line);
        f.resize(5);
        std::int32_t cls = number(f[0]), skill = number(f[1]), level = number(f[2]), cap = number(f[3]);
        ++n;
        if (level > 100) return;
        // The original stores without checking class and skill (writing past
        // the table for bad lines); such lines are dropped here.
        if (cls < 0 || cls >= kCapClasses || skill < 0 || skill >= kSkillCount || level < 0) return;
        caps_[(static_cast<std::size_t>(cls) * kSkillCount + static_cast<std::size_t>(skill)) * kCapLevels +
              static_cast<std::size_t>(level)] = cap;
    });
    return n;
}

std::int32_t SkillCapTable::cap(std::uint32_t cls, std::uint32_t skill, std::uint32_t level) const {
    if (caps_.empty() || cls >= kCapClasses || skill >= kSkillCount || level >= kCapLevels) return 0;
    return caps_[(static_cast<std::size_t>(cls) * kSkillCount + skill) * kCapLevels + level];
}

std::int32_t SkillCapTable::min_level(std::uint32_t skill, std::uint32_t cls) const {
    if (skill > 77 || cls < 1 || cls > 35) return 0; // InitSkillMinLevels covers skills 0..0x4d
    for (std::uint32_t level = 1; level < 101; ++level)
        if (cap(cls, skill, level) != 0) return static_cast<std::int32_t>(level);
    return 0;
}

bool skill_level_capped(std::uint32_t skill) { return skill < kSkillCount && kSkillFlags[skill].level_capped; }
bool skill_activated(std::uint32_t skill) { return skill < kSkillCount && kSkillFlags[skill].activated; }
std::uint32_t skill_name_eqstr(std::uint32_t skill) { return skill < kSkillCount ? kSkillFlags[skill].name : 0; }
bool skill_held_at_252(std::uint32_t skill) { return skill > 77 || kHeldAt252[skill] != 0; }

bool GameTables::load(const GameDir& game) {
    bool a = class_levels.load(game);
    bool b = skill_caps.load(game);
    return a && b;
}

} // namespace hailstone::game::mechanics
