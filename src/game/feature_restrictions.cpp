#include "game/feature_restrictions.h"

#include <array>
#include <format>

namespace hailstone::game {

namespace {

// FreeToPlayClient::RestrictionInfo @0xa0aa28: 8 bytes a feature, {char* text, u8 "have", u8 "disabled"}.
struct Restriction {
    const char* text;
    bool have;     // +4: "have" (else "has")
    bool disabled; // +5: "disabled" (else "limited")
};
constexpr std::array<Restriction, 22> kRestrictions{{
    {"AAs", true, false},
    {"maximum level", false, false},
    {"character slots", true, false},
    {"ability to use this spell tier", false, false},
    {"inventory slots", true, false},
    {"total platinum", false, false},
    {"ability to send mail", false, true},
    {"ability to send parcels", false, true},
    {"ability to spend loyalty", false, true},
    {"mercenary ranking cap", false, false},
    {"access to a number of guild hall functions", false, true},
    {"shared bank slot access", false, true},
    {"number of active quests", true, false},
    {"ability to create a guild", false, true},
    {"ability to sell in the bazaar", false, true},
    {"ability to create buy lines in the bazaar", false, true},
    {"ability to use global chat", false, true},
    {"ability to petition", false, true},
    {"advertising mode", false, false},
    {"ability to use this item", false, true},
    {"starting cities", false, false},
    {"appearance slot use", true, false},
}};

constexpr std::array<const char*, 3> kLevels{"free", "silver", "gold"}; // PTR_DAT_00b61314

} // namespace

std::int32_t feature_membership_level(std::int32_t feature, const std::optional<net::MembershipDetails>& details) {
    if (!details) return -1;
    for (std::int32_t level = 0; level <= 2; ++level) {
        for (const auto& s : details->settings) {
            if (static_cast<std::int32_t>(s.level) != level || static_cast<std::int32_t>(s.feature) != feature) continue;
            if (s.value == 1 || s.value > 1 || s.value == -1) return level;
            break; // the level's entry is found: the next level
        }
    }
    return -1;
}

std::string feature_refused_message(std::int32_t feature, const std::optional<net::MembershipDetails>& details) {
    if (feature < 0 || static_cast<std::size_t>(feature) >= kRestrictions.size()) return {};
    const Restriction& r = kRestrictions[static_cast<std::size_t>(feature)];
    const char* verb = r.have ? "have" : "has";
    const char* how = r.disabled ? "disabled" : "limited";
    std::int32_t level = feature_membership_level(feature, details);
    if (level == -1) return std::format("Your {} {} been {} until you purchase this feature.", r.text, verb, how);
    return std::format("Your {} {} been {} because you do not have at least a {} membership.", r.text, verb, how,
                       kLevels[static_cast<std::size_t>(level)]);
}

} // namespace hailstone::game
