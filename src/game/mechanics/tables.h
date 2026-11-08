#pragma once

// The data tables the client's character rules read: the class/level table
// (Resources/BaseData.txt) and the skill caps (Resources/SkillCaps.txt), both
// loaded from the player's game directory as the original does, plus the
// per-skill flags of the original's built-in skill table (skill_info.inc).

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hailstone { class GameDir; }

namespace hailstone::game::mechanics {

inline constexpr int kSkillCount = 100;  // CSkillMgr: skills 0..99
inline constexpr int kCapClasses = 36;   // SkillCaps classes 0..35
inline constexpr int kCapLevels = 101;   // levels 0..100

// One BaseData.txt row (ClassLevelData, pinstClassLevelData): the eight values
// the original reads with ClassLevelData_Get* @0x7ea5e0..0x7ea7a0.
struct ClassLevelRow {
    double base_hp = 0, base_mana = 0, base_endurance = 0;
    double hp_regen = 0, endurance_regen = 0;
    double hp_per_stamina = 0, mana_per_stat = 0, endurance_per_stat = 0;
};

// 0x7ea4b0 (BaseData loader): each line level^class^8 values (atoi, atof);
// lines with a level above 100 (unsigned) are skipped; a later line for the
// same level and class wins (it is pushed in front of the hash bucket).
// Lookups (ClassLevelData_Get*) answer 0.0 for levels outside 1..100 and for
// pairs without a line.
class ClassLevelTable {
public:
    bool load(const GameDir& game);
    std::size_t parse(std::string_view text);
    const ClassLevelRow* row(int level, std::uint32_t cls) const;
    void set(int level, std::uint32_t cls, const ClassLevelRow& r); // one row (tests)
    bool empty() const { return rows_.empty(); }

private:
    std::unordered_map<std::uint64_t, ClassLevelRow> rows_;
};

// CSkillMgr_LoadSkillCaps @0x5ba2c0: class^skill^level^cap^rate per line
// ("0x" numbers in hex); rows with level <= 100 are stored. Also the derived
// minimum levels (CSkillMgr_InitSkillMinLevels @0x5ba9b0).
class SkillCapTable {
public:
    bool load(const GameDir& game);
    std::size_t parse(std::string_view text);
    // The raw table value; 0 outside the table.
    std::int32_t cap(std::uint32_t cls, std::uint32_t skill, std::uint32_t level) const;
    // EQ_Skill::MinLevel[cls] for skills 0..77, classes 1..35: the first level
    // 1..100 whose cap is non-zero, else 0 (0x5baa20 / 0x4a8f00).
    std::int32_t min_level(std::uint32_t skill, std::uint32_t cls) const;
    bool empty() const { return caps_.empty(); }

private:
    std::vector<std::int32_t> caps_; // [class][skill][level]
};

// The skill table flags (CSkillMgr_InitSkillTable, skill_info.inc).
bool skill_level_capped(std::uint32_t skill); // EQ_Skill::LevelCappedSkill (+0x21); false above 99
bool skill_activated(std::uint32_t skill);    // EQ_Skill::Activated (+0x20)
std::uint32_t skill_name_eqstr(std::uint32_t skill); // EQ_Skill::nName (+8): the name's eqstr id
// 0x5ba230: whether a skill above 252 is held at 252 (GetSkillWithItemMods):
// skills above 77, and those of the original's byte table at 0x5ba25c.
bool skill_held_at_252(std::uint32_t skill);

struct GameTables {
    ClassLevelTable class_levels;
    SkillCapTable skill_caps;
    // Loads both from the game directory; false when either is missing.
    bool load(const GameDir& game);
};

} // namespace hailstone::game::mechanics
