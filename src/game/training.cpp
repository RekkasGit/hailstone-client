#include "game/training.h"

#include "core/wrap_math.h"

namespace hailstone::game {

namespace {

// checkLang's level table (0xaea244): language * 15 + race column (races 1..12; Iksar 13, Vah Shir 14,
// Froglok 15, which is the next language's first entry, as in the original).
constexpr std::int32_t kLanguageLevels[25 * 15 + 1] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 10, 1, 15, 20, 20, 10, 15, 25, 15, 15, 10, 20, 15, 30,
    30, 5, 20, 1, 35, 35, 23, 25, 35, 20, 20, 20, 35, 20, 10,
    30, 20, 25, 30, 1, 1, 25, 1, 10, 25, 25, 25, 10, 25, 30,
    30, 25, 10, 25, 15, 15, 1, 15, 15, 30, 30, 15, 15, 30, 30,
    30, 20, 25, 30, 10, 10, 25, 10, 1, 20, 20, 25, 10, 20, 30,
    30, 15, 15, 25, 25, 25, 13, 20, 25, 1, 5, 20, 30, 15, 30,
    10, 15, 15, 25, 25, 25, 13, 20, 25, 5, 1, 20, 30, 15, 30,
    20, 20, 25, 15, 10, 10, 25, 10, 15, 25, 25, 30, 1, 20, 30,
    30, 15, 10, 20, 20, 20, 10, 15, 20, 20, 20, 1, 25, 25, 30,
    30, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 25, 30, 30, 15, 15, 30, 15, 30, 30, 30, 15, 20, 30, 30,
    30, 15, 20, 20, 20, 20, 20, 20, 20, 15, 15, 20, 20, 15, 20,
    1, 20, 15, 20, 15, 15, 15, 15, 20, 20, 20, 15, 15, 15, 20,
    20, 10, 15, 10, 10, 10, 10, 10, 10, 10, 10, 10, 15, 15, 15,
    15, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 1,
    20, 12, 30, 30, 15, 15, 12, 15, 30, 12, 12, 15, 20, 12, 30,
    30, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 1, 15,
    15, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    30, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
    40, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 1,
    30,
};

// 0x4dc5b0: the tradeskills a guildmaster teaches only up to 20 (eqstr 117 above that).
bool trade_skill_limit(std::uint32_t skill) {
    switch (skill) {
    case 0x39: case 0x3b: case 0x3c: case 0x3d: case 0x3f: case 0x40: case 0x41: case 0x44: case 0x45:
        return true;
    default:
        return false;
    }
}

} // namespace

bool can_learn_language(const TraineeView& who, std::uint32_t language) {
    if (language > 0x18) return false;
    std::int32_t column;
    if (who.race == 128) column = 13;
    else if (who.race == 130) column = 14;
    else if (who.race == 330) column = 15;
    else if (who.race >= 1 && who.race <= 12) column = static_cast<std::int32_t>(who.race);
    else return false; // the original reads past its table for other races
    bool known = who.level >= kLanguageLevels[language * 15 + static_cast<std::uint32_t>(column)];
    // Class exceptions.
    if (who.class_id == 11) { // necromancer
        if (who.level >= 8 && language == 12) known = true;
        if (who.level >= 20 && language == 17) known = true;
    }
    if (who.class_id == 12 && who.level >= 8 && language == 11) known = true; // wizard
    if (who.class_id == 14 && language == 11) known = true;                   // enchanter
    if (who.class_id == 13 && who.level >= 8 && language == 11) known = true; // magician
    return known;
}

std::vector<TrainingRow> training_rows(const TraineeView& who, const std::array<std::uint32_t, kTrainSkills>& skill_caps,
                                       const std::array<std::uint8_t, kTrainLanguages>& language_caps) {
    std::vector<TrainingRow> rows;
    for (std::uint32_t s = 0; s < kTrainSkills; ++s) {
        if (s == 0x48 || static_cast<std::int32_t>(skill_caps[s]) <= 0) continue;
        if (!who.min_level || who.min_level(s) <= 0) continue;
        if (s == 0x21 || s == 0xf) continue;
        rows.push_back({static_cast<std::int32_t>(s), static_cast<std::int32_t>(skill_caps[s])});
    }
    for (std::uint32_t l = 0; l < kTrainLanguages; ++l)
        if (can_learn_language(who, l) && language_caps[l] > 0) rows.push_back({static_cast<std::int32_t>(100 + l), 100});
    return rows;
}

std::int32_t training_cost(std::int32_t value) {
    // A 32-bit difference cubed, divided signed: a skill past 1301 wraps the cube (the server's value is uncapped).
    auto v = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) - 10u);
    if (v < 0) v = 0;
    const auto u = static_cast<std::uint32_t>(v);
    return core::wrapped_div(u * u * u, 100);
}

std::int32_t pc_skill_limit(const SkillLimitView& who, std::uint32_t skill) {
    auto value = [&](std::uint32_t s) { return who.skill ? who.skill(s) : 0; };
    auto effect = [&](std::int32_t spa) { return who.total_effect ? who.total_effect(spa) : 0; };
    std::int32_t cap = who.skill_cap ? who.skill_cap(who.level, who.class_id, skill) : 0;
    bool capped = who.level_capped && who.level_capped(skill);
    // The original's jump table at 0x584e44: 60..69 except 62, 66 and 67.
    if (!capped && skill - 0x3c <= 9 && skill != 0x3e && skill != 0x42 && skill != 0x43) {
        std::int32_t allowed = effect(0x107) + 1;
        std::int32_t over = 0;
        for (std::uint32_t other : {0x3fu, 0x3du, 0x45u, 0x44u, 0x40u, 0x3cu, 0x41u})
            if (other != skill && value(other) > 200) ++over;
        if (over >= allowed) cap = 200;
    }
    if (skill < 0x2b || skill > 0x2f) return cap; // 0x5ba150: the specializations
    std::int32_t level = who.level;
    std::int32_t highest = who.highest_level ? static_cast<std::uint8_t>(who.highest_level()) : 0;
    std::int32_t first = 50, second = 100;
    std::int32_t needed = effect(0xf8) != 0 ? 2 : 1;
    if (level > 0x46 || (level > 0x40 && highest > 0x46)) {
        first = highest * 5 - 300;
        second = highest * 5 - 250;
    }
    std::int32_t over_first = 0, over_second = 0;
    for (std::uint32_t other = 0x2b; other <= 0x2f; ++other) {
        if (other == skill) continue;
        if (value(other) > first) ++over_first;
        if (value(other) > second) ++over_second;
    }
    if (over_first >= needed) return first;
    return over_second >= 1 ? second : cap;
}

bool at_skill_limit(const SkillLimitView& who, std::uint32_t skill) {
    return (who.skill ? who.skill(skill) : 0) >= pc_skill_limit(who, skill);
}

std::optional<TrainRefusal> check_train(const TraineeView& who, const TrainingRow& row) {
    std::int32_t skill = row.id();
    if (row.kind == -1) return TrainRefusal{12923};
    if (!row.language() && skill == 0x39 && who.race != 12) return TrainRefusal{0}; // tinkering: gnomes only
    if (row.language()) {
        auto lang = static_cast<std::uint32_t>(skill);
        if (!can_learn_language(who, lang)) return TrainRefusal{12924};
        if (who.practice_points <= 0) return TrainRefusal{12925};
        std::int32_t known = who.language ? who.language(lang) : 0;
        if (lang < 0x20 && known == 100) return TrainRefusal{12926, true, true};
        if (known < row.cap) return std::nullopt;
        return TrainRefusal{12927};
    }
    auto s = static_cast<std::uint32_t>(skill);
    std::int32_t value = who.skill ? who.skill(s) : 0;
    if (trade_skill_limit(s) && value > 0x14) return TrainRefusal{117, false, false};
    if (who.practice_points <= 0) return TrainRefusal{12925};
    if (!who.class_cap || who.class_cap(s) == 0) return TrainRefusal{12928};
    if (who.min_level && who.min_level(s) > who.level) return TrainRefusal{12929};
    if (who.at_skill_limit && who.at_skill_limit(s)) return TrainRefusal{12930, true, true};
    if (value < row.cap) return std::nullopt;
    return TrainRefusal{12931};
}

} // namespace hailstone::game
