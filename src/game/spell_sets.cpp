#include "game/spell_sets.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

#include "core/ini_file.h"
#include "core/msvc_atoi.h"

namespace nmsc::game {

namespace {

constexpr std::string_view kSection = "SpellLoadouts"; // 0x9d32f8

int compare_no_case(std::string_view a, std::string_view b) { // __stricmp
    std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

} // namespace

void SpellLoadouts::load(const IniFile& ini) {
    for (int n = 0; n < kSpellLoadouts; ++n) {
        SpellLoadout& s = sets_[static_cast<std::size_t>(n)];
        s = {};
        s.in_use = ini.get_int(kSection, std::format("SpellLoadout{}.inuse", n + 1), 0) != 0;
        if (!s.in_use) continue;
        s.name = ini.get_string(kSection, std::format("SpellLoadout{}.name", n + 1));
        if (s.name.size() > kLoadoutNameMax) s.name.resize(kLoadoutNameMax);
        for (int k = 0; k < kLoadoutGems; ++k)
            s.spells[static_cast<std::size_t>(k)] = ini.get_int(kSection, std::format("SpellLoadout{}.slot{}", n + 1, k + 1), -1);
    }
    dirty_ = false;
}

void SpellLoadouts::save(IniFile& ini) {
    for (int n = 0; n < kSpellLoadouts; ++n) {
        SpellLoadout& s = sets_[static_cast<std::size_t>(n)];
        if (!s.changed) continue;
        s.changed = false;
        ini.set_int(kSection, std::format("SpellLoadout{}.inuse", n + 1), s.in_use ? 1 : 0);
        if (!s.in_use) continue;
        ini.set(kSection, std::format("SpellLoadout{}.name", n + 1), s.name);
        for (int k = 0; k < kLoadoutGems; ++k)
            ini.set_int(kSection, std::format("SpellLoadout{}.slot{}", n + 1, k + 1), s.spells[static_cast<std::size_t>(k)]);
    }
}

void SpellLoadouts::sort() {
    // (qsort is not stable; sets that compare equal are only unused ones and duplicates of a name)
    std::stable_sort(sets_.begin(), sets_.end(), [](const SpellLoadout& a, const SpellLoadout& b) {
        if (a.in_use && b.in_use) return compare_no_case(a.name, b.name) < 0;
        return a.in_use && !b.in_use;
    });
    for (auto& s : sets_) s.changed = true;
    dirty_ = true;
}

int SpellLoadouts::index_of(std::string_view name) const {
    for (int n = 0; n < kSpellLoadouts; ++n)
        if (sets_[static_cast<std::size_t>(n)].in_use && sets_[static_cast<std::size_t>(n)].name == name) return n;
    return -1;
}

SpellLoadouts::SaveResult SpellLoadouts::save_set(std::string_view name, const std::array<std::int32_t, kLoadoutGems>& gems,
                                                  int* index) {
    std::string cut(name.substr(0, kLoadoutNameMax));
    int at = -1;
    for (int n = 0; n < kSpellLoadouts; ++n) {
        const SpellLoadout& s = sets_[static_cast<std::size_t>(n)];
        if (s.in_use && compare_no_case(cut, s.name) == 0) {
            at = n;
            break;
        }
        if (at == -1 && !s.in_use) at = n;
    }
    if (at == -1) return SaveResult::Full;
    SpellLoadout& s = sets_[static_cast<std::size_t>(at)];
    s.in_use = true;
    s.changed = true;
    s.name = cut;
    s.spells = gems;
    sort();
    if (index) *index = index_of(cut);
    return SaveResult::Saved;
}

bool SpellLoadouts::edit(int n, std::string_view name, const std::array<std::int32_t, kLoadoutGems>& gems, int* index) {
    if (n < 0 || n >= kSpellLoadouts || !sets_[static_cast<std::size_t>(n)].in_use || name.empty()) return false;
    std::string cut(name.substr(0, kLoadoutNameMax));
    for (int k = 0; k < kSpellLoadouts; ++k) {
        SpellLoadout& other = sets_[static_cast<std::size_t>(k)];
        if (k != n && other.in_use && compare_no_case(cut, other.name) == 0) other.in_use = false;
    }
    SpellLoadout& s = sets_[static_cast<std::size_t>(n)];
    s.name = cut;
    s.spells = gems;
    sort();
    if (index) *index = index_of(cut);
    return true;
}

bool SpellLoadouts::remove(int n) {
    if (n < 0 || n >= kSpellLoadouts || !sets_[static_cast<std::size_t>(n)].in_use) return false;
    sets_[static_cast<std::size_t>(n)].in_use = false;
    sets_[static_cast<std::size_t>(n)].changed = true;
    sort();
    return true;
}

int SpellLoadouts::find(std::string_view arg) const {
    if (arg.empty()) return -1;
    if (std::all_of(arg.begin(), arg.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        unsigned number = static_cast<unsigned>(core::msvc_atoi(arg)) - 1u;
        if (number <= 9u && sets_[number].in_use) return static_cast<int>(number);
        return -1;
    }
    for (int n = 0; n < kSpellLoadouts; ++n) {
        const SpellLoadout& s = sets_[static_cast<std::size_t>(n)];
        if (s.in_use && s.name.size() >= arg.size() && compare_no_case(s.name.substr(0, arg.size()), arg) == 0) return n;
    }
    return -1;
}

} // namespace nmsc::game
