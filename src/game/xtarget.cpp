#include "game/xtarget.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "core/msvc_atoi.h"

namespace nmsc::game {

namespace {

bool same_no_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// strnicmp over at most n characters (0 when equal).
bool equal_no_case_n(std::string_view a, std::string_view b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        char x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (std::tolower(static_cast<unsigned char>(x)) != std::tolower(static_cast<unsigned char>(y))) return false;
        if (!x) break;
    }
    return true;
}

// GetDelimitedToken(text, out, size, '^', 1, 0): the text up to the separator (at most size - 1 characters kept)
// and how far it read, the separator included.
std::size_t token(std::string_view text, std::string& out, std::size_t size) {
    std::size_t n = text.find('^');
    std::size_t len = n == std::string_view::npos ? text.size() : n;
    out.assign(text.substr(0, std::min(len, size - 1)));
    return n == std::string_view::npos ? len : len + 1;
}

// ParseDelimitedInt(text, &out, '^'): a token read with atoi.
std::size_t int_token(std::string_view text, int& out) {
    std::string t;
    std::size_t used = token(text, t, 0x800);
    out = core::msvc_atoi(t);
    return used;
}

// XTarget_ParseTargetType's table (re/src/game/ui/xtarget_types.inc): the names and short names.
struct TypeName {
    const char* name;
    std::uint32_t type;
};
constexpr TypeName kTypeNames[] = {
    {"EmptyTarget", 0},       {"AutoHater", 1},          {"CurrentTarget", 2},      {"TargetsTarget", 4},
    {"GroupTank", 5},         {"GroupTankTarget", 6},    {"GroupAssist", 7},        {"GroupAssistTarget", 8},
    {"GroupPuller", 9},       {"GroupPullerTarget", 10}, {"GroupMark1", 11},        {"GroupMark2", 12},
    {"GroupMark3", 13},       {"RaidAssist1", 14},       {"RaidAssist2", 15},       {"RaidAssist3", 16},
    {"RaidAssist1Target", 17}, {"RaidAssist2Target", 18}, {"RaidAssist3Target", 19}, {"RaidMark1", 20},
    {"RaidMark2", 21},        {"RaidMark3", 22},         {"Pet", 23},               {"PetTarget", 24},
    {"Mercenary", 25},        {"MercenaryTarget", 26},   {"ET", 0},                 {"AH", 1},
    {"CT", 2},                {"TT", 4},                 {"GT", 5},                 {"GTT", 6},
    {"GA", 7},                {"GAT", 8},                {"GP", 9},                 {"GPT", 10},
    {"GM1", 11},              {"GM2", 12},               {"GM3", 13},               {"RA1", 14},
    {"RA2", 15},              {"RA3", 16},               {"RA1T", 17},              {"RA2T", 18},
    {"RA3T", 19},             {"RM1", 20},               {"RM2", 21},               {"RM3", 22},
    {"P", 23},                {"PT", 24},                {"M", 25},                 {"MT", 26},
};

} // namespace

const char* xtarget_type_name(std::uint32_t type) {
    static constexpr const char* kNames[kXTargetTypes] = {
        "Empty Target",        "Auto Hater",          "Specific PC",         "Specific NPC",
        "Target's Target",     "Group Tank",          "Group Tank's Target", "Group Assist",
        "Group Assist Target", "Group Puller",        "Group Puller Target", "Group Mark 1",
        "Group Mark 2",        "Group Mark 3",        "Raid Assist 1",       "Raid Assist 2",
        "Raid Assist 3",       "Raid Assist 1 Target", "Raid Assist 2 Target", "Raid Assist 3 Target",
        "Raid Mark 1",         "Raid Mark 2",         "Raid Mark 3",         "Pet",
        "Pet Target",          "Mercenary",           "Mercenary Target",
    };
    return type < kXTargetTypes ? kNames[type] : nullptr;
}

std::optional<std::uint32_t> parse_xtarget_type(std::string_view name) {
    // Every name is compared (no early exit); the last match wins. The names are distinct, so the first is it.
    std::optional<std::uint32_t> out;
    for (const TypeName& t : kTypeNames)
        if (same_no_case(name, t.name)) out = t.type;
    return out;
}

int xtarget_role_icon_cell(std::uint32_t type) {
    // The switch of CExtendedTargetWnd_UpdateAutoHateIcon (A_TargetRoleIcon: 16x16 cells, column by column).
    static constexpr int kCells[kXTargetTypes] = {-1, 0x1e, 0x1b, 0x1c, 0,    2,    3,    4,    5,
                                                  6,  7,    8,    9,    10,   0x14, 0x15, 0x16, 0x18,
                                                  0x19, 0x1a, 0xd, 0xe, 0xf,  0x10, 0x11, 0x12, 0x13};
    return type < kXTargetTypes ? kCells[type] : -1;
}

int xtarget_first_auto_hater_slot(const XTargetState& list, const std::function<bool(int slot)>& skip) {
    for (std::size_t i = 0; i < list.slots.size(); ++i)
        if (list.slots[i].type == kXTargetAutoHater && !(skip && skip(static_cast<int>(i)))) return static_cast<int>(i);
    return -1;
}

int xtarget_last_specific_slot(const XTargetState& list) {
    for (int i = static_cast<int>(list.slots.size()) - 1; i >= 0; --i) {
        std::uint32_t t = list.slots[static_cast<std::size_t>(i)].type;
        if (t == kXTargetSpecificPc || t == kXTargetSpecificNpc) return i;
    }
    return -1;
}

int xtarget_specific_slot_by_spawn(const XTargetState& list, std::uint32_t spawn_id) {
    for (std::size_t i = 0; i < list.slots.size(); ++i) {
        const XTargetSlot& s = list.slots[i];
        if (s.spawn_id == spawn_id && (s.type == kXTargetSpecificPc || s.type == kXTargetSpecificNpc))
            return static_cast<int>(i);
    }
    return -1;
}

bool xtarget_set_slot(XTargetState& list, int slot, std::uint32_t type, std::string_view name) {
    if (slot < 0 || slot >= static_cast<int>(list.slots.size())) return false;
    XTargetSlot& s = list.slots[static_cast<std::size_t>(slot)];
    if (s.type == type && equal_no_case_n(name, s.name, 0x40)) return false;
    s.status = 0;
    s.spawn_id = 0;
    s.type = type;
    s.name.assign(name.substr(0, 63)); // steve_strncpy(+0xc, name, 0x40)
    return true;
}

std::string xtarget_role_string(const XTargetState& list, std::string_view name, int flag) {
    int count = 0;
    for (const XTargetSlot& s : list.slots)
        if (s.type != kXTargetAutoHater) ++count;
    std::string out = std::to_string(flag) + '^' + std::string(name) + '^' + std::to_string(count);
    for (std::size_t i = 0; i < list.slots.size(); ++i) {
        const XTargetSlot& s = list.slots[i];
        if (s.type == kXTargetAutoHater) continue;
        out += '^' + std::to_string(i) + '^' + std::to_string(s.type);
        if (s.type == kXTargetSpecificPc) out += '^' + s.name.substr(0, 63);
    }
    return out;
}

bool xtarget_apply_role_string(std::string_view text, std::size_t slot_count, std::string* name,
                               const XTargetSend& send) {
    text = text.substr(0, 0x5dc); // strncpy into 1500 bytes
    int flag = 0;
    std::size_t at = int_token(text, flag);
    if (flag != 1) return false;
    std::array<bool, kXTargetMaxSlots> listed{};
    std::string set_name;
    at += token(text.substr(at), set_name, 0x18);
    if (name) *name = set_name;
    int count = 0;
    at += int_token(text.substr(at), count);
    // Deliberate difference: the original sends a request for each of `count` entries, however many it claims. The
    // window has kXTargetMaxSlots rows, so a hand-edited count past that (two billion requests) is capped there.
    count = std::min(count, kXTargetMaxSlots);
    for (; count > 0; --count) {
        int slot = 0, type = -1;
        at += int_token(text.substr(std::min(at, text.size())), slot);
        at += int_token(text.substr(std::min(at, text.size())), type);
        if (static_cast<unsigned>(slot) < static_cast<unsigned>(kXTargetMaxSlots))
            listed[static_cast<std::size_t>(slot)] = true;
        if (type == static_cast<int>(kXTargetSpecificNpc)) continue;
        std::string who;
        if (type == static_cast<int>(kXTargetSpecificPc)) at += token(text.substr(std::min(at, text.size())), who, 0x40);
        send(slot, static_cast<std::uint32_t>(type), who);
    }
    for (std::size_t i = 0; i < slot_count; ++i)
        if (i >= listed.size() || !listed[i]) send(static_cast<int>(i), kXTargetAutoHater, std::string());
    return true;
}

std::string xtarget_role_string_name(std::string_view text) {
    text = text.substr(0, 0x5dc);
    int flag = 0;
    std::size_t at = int_token(text, flag);
    std::string name;
    if (flag == 1) token(text.substr(at), name, 0x18);
    return name;
}

void XTargetRoleSets::load(const std::function<std::string(const std::string& key)>& get) {
    for (int i = 0; i < kCount; ++i) {
        XTargetRoleSet& s = sets[static_cast<std::size_t>(i)];
        std::string v = get("TargetSet" + std::to_string(i + 1));
        if (v.empty()) {
            s = {};
            continue;
        }
        s.used = true;
        s.roles = v.substr(0, 0x5db);
        s.dirty = false;
        s.name = xtarget_role_string_name(v);
    }
}

void XTargetRoleSets::save(const std::function<void(const std::string& key, const std::string& value)>& put) const {
    for (int i = 0; i < kCount; ++i) {
        const XTargetRoleSet& s = sets[static_cast<std::size_t>(i)];
        if (s.dirty) put("TargetSet" + std::to_string(i + 1), s.used ? s.roles : std::string());
    }
}

void XTargetRoleSets::sort_and_save(const std::function<void(const std::string& key, const std::string& value)>& put) {
    // qsort with FUN_00519920: used sets first, by name without case.
    std::stable_sort(sets.begin(), sets.end(), [](const XTargetRoleSet& a, const XTargetRoleSet& b) {
        if (a.used != b.used) return a.used;
        if (!a.used) return false;
        std::size_t n = std::min(a.name.size(), b.name.size());
        for (std::size_t i = 0; i < n; ++i) {
            int x = std::tolower(static_cast<unsigned char>(a.name[i])), y = std::tolower(static_cast<unsigned char>(b.name[i]));
            if (x != y) return x < y;
        }
        return a.name.size() < b.name.size();
    });
    for (XTargetRoleSet& s : sets) s.dirty = true;
    save(put);
}

int XTargetRoleSets::slot_for_name(std::string_view name) const {
    int found = -1;
    for (int i = 0; i < kCount; ++i) {
        const XTargetRoleSet& s = sets[static_cast<std::size_t>(i)];
        if (s.used && same_no_case(name, s.name)) return i;
        if (found == -1 && !s.used) found = i;
    }
    return found;
}

} // namespace nmsc::game
