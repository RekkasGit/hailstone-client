#include "game/group.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace nmsc::game {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool istarts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

// cmd_pet @0x4e2150's whole-word comparisons, in its order (the eqstr ids it
// loads, with their eqstr_us.txt texts).
struct Word {
    std::uint32_t eqstr;
    const char* text;
    std::uint32_t command;
    // The pet window's toggle byte the word writes (cmd_pet's CPetInfoWnd_FlipButtonToggleState /
    // SetButtonToggleState calls; the command indexes 4..13).
    PetToggleAction toggle = PetToggleAction::None;
    int index = 0;
    int index2 = 0;
};
constexpr auto F = PetToggleAction::Flip, N = PetToggleAction::On, O = PetToggleAction::Off,
               B = PetToggleAction::OnOff;
constexpr Word kWords[] = {
    {7512, "report health", 0},   {7515, "health report", 0}, {6144, "report", 0},   {7516, "health", 0},
    {7521, "who leader", 1},      {7520, "leader", 1},        {713, "follow", 4, B, 4, 5}, {6841, "guard", 5, B, 5, 4},
    {7518, "sit", 6, F, 6},       {6844, "sit on", 7, N, 6},  {6845, "sit off", 8, O, 6}, {7505, "as you were", 9, F, 7},
    {697, "stop", 9, F, 7},       {6836, "stop on", 10, N, 7}, {6837, "stop off", 11, O, 7}, {6851, "regroup", 31, F, 8},
    {6849, "regroup on", 32, N, 8}, {6850, "regroup off", 33, O, 8}, {7502, "taunt", 12, F, 9}, {7517, "taunt on", 13, N, 9},
    {7514, "taunt off", 14, O, 9}, {7503, "notaunt", 14, O, 9}, {7509, "no taunt", 14, O, 9}, {7513, "hold", 15, F, 10},
    {690, "hold on", 16, N, 10},  {691, "hold off", 17, O, 10}, {6840, "ghold", 18, F, 11}, {6839, "ghold on", 19, N, 11},
    {6842, "ghold off", 20, O, 11}, {9299, "focus", 24, F, 13}, {692, "focus on", 25, N, 13}, {693, "focus off", 26, O, 13},
    {3272, "feign", 27},          {694, "spellhold", 21, F, 12}, {695, "spellhold on", 22, N, 12}, {9300, "no cast", 22, N, 12},
    {696, "spellhold off", 23, O, 12}, {7500, "back off", 28}, {7501, "calm down", 28}, {7504, "get lost", 29},
    {6848, "leave", 29},          {13103, "target", 30},      {705, "qattack", 3},
};
// The attack words (eqstr 13092..13101), compared as prefixes; a name may follow.
constexpr const char* kAttackWords[] = {"attack", "kill", "kick", "maim", "slay", "murder", "execute", "sack", "destroy", "whack"};

} // namespace

int GroupState::count() const {
    int n = 0;
    for (const auto& m : slots) n += !m.name.empty();
    return n;
}

int GroupState::slot_of(const std::string& name) const {
    if (name.empty()) return -1;
    for (int i = 0; i < kGroupSlots; ++i)
        if (iequals(slots[static_cast<std::size_t>(i)].name, name)) return i;
    return -1;
}

bool GroupState::has_member(const std::string& name) const { return slot_of(name) >= 0; }

bool GroupState::is_leader(const std::string& name) const { return !leader.empty() && iequals(leader, name); }

bool GroupState::add(const std::string& name, std::uint32_t level, bool merc, const std::string& owner) {
    if (name.empty() || has_member(name)) return false;
    for (auto& m : slots) {
        if (!m.name.empty()) continue;
        m = {};
        m.name = name;
        m.level = level;
        m.merc = merc;
        m.owner_name = owner;
        active = true;
        return true;
    }
    return false;
}

bool GroupState::remove(const std::string& name) {
    int i = slot_of(name);
    if (i < 0) return false;
    slots[static_cast<std::size_t>(i)] = {};
    return true;
}

void GroupState::clear() {
    slots = {};
    leader.clear();
    active = false;
    mana_percent.clear();
    endurance_percent.clear();
}

std::optional<ParsedPetCommand> parse_pet_command(const std::string& args) {
    std::string_view a = args;
    while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
    while (!a.empty() && a.back() == ' ') a.remove_suffix(1);
    for (const Word& w : kWords) {
        if (!iequals(a, w.text)) continue;
        ParsedPetCommand p;
        p.command = w.command;
        p.uses_target = w.command == 3;
        p.leader_target = w.command == 1;
        p.toggle = w.toggle;
        p.toggle_index = w.index;
        p.toggle_index2 = w.index2;
        return p;
    }
    for (const char* word : kAttackWords) {
        std::string_view wv = word;
        if (!istarts_with(a, wv)) continue;
        ParsedPetCommand p;
        p.command = 2;
        std::string_view rest = a.substr(wv.size());
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.empty()) {
            p.uses_target = true;
        } else {
            // Spaces become underscores, as spawn names have them.
            p.named_target.assign(rest);
            std::replace(p.named_target.begin(), p.named_target.end(), ' ', '_');
        }
        return p;
    }
    return std::nullopt;
}

} // namespace nmsc::game
