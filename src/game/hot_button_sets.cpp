#include "game/hot_button_sets.h"

#include <algorithm>
#include <cctype>
#include <format>

#include "core/ini_file.h"

namespace hailstone::game {

namespace {

constexpr std::string_view kSection = "HBLoadouts";
constexpr std::string_view kFileSection = "Hotbuttons";

int compare_no_case(std::string_view a, std::string_view b) { // __stricmp
    std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

std::string slot_key(int page, int button) { return std::format("Page{}.Slot{}", page + 1, button + 1); }

} // namespace

void HotButtonSets::load(const IniFile& ini) {
    for (int n = 0; n < kHotButtonSets; ++n) {
        HotButtonSet& s = sets_[static_cast<std::size_t>(n)];
        s.changed = false;
        s.in_use = ini.get_int(kSection, std::format("HBSet{}.inuse", n + 1), 0) != 0;
        if (!s.in_use) continue; // (the name and file number of an unused entry are left as they were)
        s.name = ini.get_string(kSection, std::format("HBSet{}.name", n + 1), "Unnamed");
        if (s.name.size() > kHotButtonSetNameMax) s.name.resize(kHotButtonSetNameMax);
        s.file = ini.get_int(kSection, std::format("HBSet{}.fileNum", n + 1), 0);
    }
    dirty_ = false;
}

void HotButtonSets::save(IniFile& ini) {
    for (int n = 0; n < kHotButtonSets; ++n) {
        HotButtonSet& s = sets_[static_cast<std::size_t>(n)];
        if (!s.changed) continue;
        // (the original leaves the changed flag set; only what is written matters)
        s.changed = false;
        ini.set_int(kSection, std::format("HBSet{}.inuse", n + 1), s.in_use ? 1 : 0);
        if (!s.in_use) continue;
        ini.set(kSection, std::format("HBSet{}.name", n + 1), s.name);
        ini.set_int(kSection, std::format("HBSet{}.fileNum", n + 1), s.file);
    }
}

void HotButtonSets::sort() {
    // 0x519890: both in use by __stricmp, in use before unused (qsort is not stable; only unused entries and
    // equal names compare equal, and those do not matter)
    std::stable_sort(sets_.begin(), sets_.end(), [](const HotButtonSet& a, const HotButtonSet& b) {
        if (a.in_use && b.in_use) return compare_no_case(a.name, b.name) < 0;
        return a.in_use && !b.in_use;
    });
    for (auto& s : sets_) s.changed = true;
    dirty_ = true;
}

int HotButtonSets::begin_save(std::string_view name) {
    std::string cut(name.substr(0, kHotButtonSetNameMax));
    int at = -1;
    for (int n = 0; n < kHotButtonSets; ++n) {
        const HotButtonSet& s = sets_[static_cast<std::size_t>(n)];
        if (s.in_use && compare_no_case(cut, s.name) == 0) {
            at = n;
            break;
        }
        if (at == -1 && !s.in_use) at = n;
    }
    if (at == -1) return -1;
    HotButtonSet& s = sets_[static_cast<std::size_t>(at)];
    // (the original takes file = at, the file of whichever set the sort left that index's number to)
    auto taken = [&](std::int32_t file) {
        for (int n = 0; n < kHotButtonSets; ++n)
            if (n != at && sets_[static_cast<std::size_t>(n)].in_use && sets_[static_cast<std::size_t>(n)].file == file)
                return true;
        return false;
    };
    if (!s.in_use || taken(s.file)) {
        std::int32_t file = 0;
        while (taken(file)) ++file;
        s.file = file;
    }
    s.in_use = s.changed = true;
    s.name = cut;
    dirty_ = true;
    return at;
}

bool HotButtonSets::remove(int n) {
    if (n < 0 || n >= kHotButtonSets || !sets_[static_cast<std::size_t>(n)].in_use) return false;
    sets_[static_cast<std::size_t>(n)].in_use = false;
    sets_[static_cast<std::size_t>(n)].changed = true;
    sort();
    return true;
}

int HotButtonSets::in_use_count() const {
    return static_cast<int>(std::count_if(sets_.begin(), sets_.end(), [](const HotButtonSet& s) { return s.in_use; }));
}

void write_hot_button_set(IniFile& file, const HotButtonStore& store, int bar) {
    for (int page = 0; page < kHotPages; ++page)
        for (int button = 0; button < kHotButtons; ++button) {
            const HotButton& b = store.at(bar, page, button);
            // IniCache_WriteString(.., NULL) for an empty button (type 0): the key goes
            if (b.empty()) file.erase(kFileSection, slot_key(page, button));
            else file.set(kFileSection, slot_key(page, button), format_hot_button(b));
        }
}

void read_hot_button_set(const IniFile& file, HotButtonStore& store, int bar) {
    for (int page = 0; page < kHotPages; ++page)
        for (int button = 0; button < kHotButtons; ++button) {
            HotButton& b = store.at(bar, page, button);
            b = HotButton{}; // ClearHotButtonSlot
            // IniCache_GetString(.., "", buf, 0x40): a value of up to 63 characters
            std::string v = file.get_string(kFileSection, slot_key(page, button));
            if (v.size() > 63) v.resize(63);
            if (!v.empty()) b = parse_hot_button(v);
        }
}

void clear_hot_bar(HotButtonStore& store, int bar) {
    for (int page = 0; page < kHotPages; ++page)
        for (int button = 0; button < kHotButtons; ++button) store.at(bar, page, button) = HotButton{};
}

std::string hot_button_set_file(std::int32_t file) { return std::format("HB_{}.ini", file); }

} // namespace hailstone::game
