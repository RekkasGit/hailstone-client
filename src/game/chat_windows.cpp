#include "game/chat_windows.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace hailstone::game {

namespace {

// GetChannelFromColor's table for the colours 0x100..0x15e (rof2decomp re/src/game/ui/chat_channel_colors.inc).
constexpr std::int32_t kFirstColor = 0x100, kLastColor = 0x15e;
constexpr std::array<std::uint8_t, kLastColor - kFirstColor + 1> kChannelFromColor{
    0,  1,  2,  4,  5,  6,  7,  8,  10, 9,  24, 23, 25, 22, 11, 31, 22, 22, 22, 22, 22, 28, 29, 26,
    27, 50, 22, 40, 39, 22, 22, 42, 36, 37, 22, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 30, 38, 32,
    33, 34, 35, 0,  1,  2,  4,  5,  6,  7,  8,  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 22, 3,
    43, 46, 22, 44, 45, 41, 47, 49, 51, 52, 53, 22, 22, 22, 54, 22, 22, 4,  55, 22, 8,  4,  56,
};

using M = ChatFilterMenu;
constexpr ChatFilterItem kFilterItems[] = {
    // the Filters menu (+0x180), in InitContextMenu's order
    {M::Filters, 0, 6630},   // Say
    {M::Filters, 1, 6631},   // Tell
    {M::Filters, 2, 6632},   // Group
    {M::Filters, 3, 5113},   // Raid
    {M::Filters, 4, 6633},   // Guild
    {M::Filters, 5, 6634},   // OOC
    {M::Filters, 6, 6635},   // Auction
    {M::Filters, 7, 6636},   // Shout
    {M::Filters, 8, 6637},   // Emotes
    {M::Filters, 49, 5870},  // System Messages
    // (then the Chat Channel, Melee and Spells submenus)
    {M::Filters, 11, 6641},  // Skills
    {M::Filters, 42, 3343},  // Randoms
    {M::Filters, 22, 6642},  // Other
    {M::Filters, 47, 5871},  // Experience Messages
    {M::Filters, 50, 13950}, // /who
    {M::Filters, 52, 6133},  // Pet Responses
    {M::Filters, 53, 6152},  // Item Speech
    {M::Filters, 54, 4588},  // Fellowship Messages
    {M::Filters, 55, 6361},  // Mercenary Messages
    {M::Filters, 56, 6447},  // PvP Messages
    // Chat Channel (+0x1a4): one item per joined channel (ids 0x70 + k)
    {M::ChatChannel, 12, 0}, {M::ChatChannel, 13, 0}, {M::ChatChannel, 14, 0}, {M::ChatChannel, 15, 0},
    {M::ChatChannel, 16, 0}, {M::ChatChannel, 17, 0}, {M::ChatChannel, 18, 0}, {M::ChatChannel, 19, 0},
    {M::ChatChannel, 20, 0}, {M::ChatChannel, 21, 0},
    // Melee (+0x194)
    {M::Melee, 9, 15014},   // Your Hits
    {M::Melee, 23, 15015},  // Your Misses
    {M::Melee, 24, 15016},  // You Being Hit
    {M::Melee, 25, 15017},  // You Being Missed
    {M::Melee, 26, 15018},  // Others Hits
    {M::Melee, 27, 15019},  // Others Misses
    {M::Melee, 43, 3344},   // Pet messages
    {M::Melee, 44, 5868},   // Pet Rampage/Flurry
    {M::Melee, 45, 5869},   // Pet Criticals
    {M::Melee, 30, 15020},  // Critical Hits
    {M::Melee, 31, 15021},  // Disciplines
    {M::Melee, 32, 15022},  // Melee Warnings
    {M::Melee, 33, 15023},  // NPC Rampage
    {M::Melee, 34, 15024},  // NPC Flurry
    {M::Melee, 35, 15025},  // NPC Enrage
    {M::Melee, 28, 15026},  // My Death
    {M::Melee, 29, 15027},  // Others Death
    {M::Melee, 46, 3376},   // Damage Shield Reports
    // Spells (+0x19c)
    {M::Spells, 10, 15028}, // Mine
    {M::Spells, 36, 15029}, // Others
    {M::Spells, 37, 15030}, // Failures
    {M::Spells, 38, 15031}, // Criticals
    {M::Spells, 39, 15032}, // Worn Off
    {M::Spells, 40, 15033}, // Non Melee Hits
    {M::Spells, 41, 5867},  // Focus Effects
    {M::Spells, 51, 6132},  // Pet Spells
};

constexpr ChatDefaultItem kDefaultItems[] = {
    {0xc8, 8, 6630, true},     // Say
    {0xc9, 7, 6631, false},    // Tell (EnableMenuItem(item, 0) right after it is added)
    {0xca, 2, 6632, true},     // Group
    {0xd9, 0xf, 5113, true},   // Raid
    {0xcb, 0, 6633, true},     // Guild
    {0xcc, 5, 6634, true},     // OOC
    {0xcd, 4, 6635, true},     // Auction
};

} // namespace

int chat_channel_of_color(std::int32_t color) {
    if (color < kFirstColor || color > kLastColor) return kChatChannelOther;
    return kChannelFromColor[static_cast<std::size_t>(color - kFirstColor)];
}

std::span<const ChatFilterItem> chat_filter_items() { return kFilterItems; }

std::uint32_t chat_filter_all_eqstr(ChatFilterMenu menu) {
    switch (menu) {
    case M::Filters: return 6040;     // All normal channels will now filter to this window.
    case M::Melee: return 6041;       // All melee channels ...
    case M::Spells: return 6042;      // All spell channels ...
    case M::ChatChannel: return 6043; // All chat server channels ...
    }
    return 6040;
}

std::span<const ChatDefaultItem> chat_default_items() { return kDefaultItems; }

std::uint32_t hit_category_eqstr(int category) { return 15014 + static_cast<std::uint32_t>(category); }

std::uint32_t hit_mode_eqstr(int category, int mode) {
    // the miss categories (1, 3, 5) say Miss Only for mode 2; ALL's submenu (6) Number Only
    if (mode == 2) return category >= 0 && category < 6 && (category & 1) ? 15037 : 15036;
    return mode == 1 ? 15035 : 15034;
}

std::uint32_t hit_mode_all_eqstr(int mode) { return 6044 + static_cast<std::uint32_t>(mode); }

std::string tell_menu_text(std::string name, const std::string& tell_word) {
    char c = '/';
    if (name.find('.') != std::string::npos) {
        if (!name.empty() && name[0] == '.') name.erase(0, 1);
        c = ';';
    }
    if (!name.empty() && (name[0] == '[' || name[0] == ';')) {
        c = name[0];
        name.erase(0, 1);
    }
    return std::format("{}{} {} ", c, tell_word, name);
}

bool cycle_last_tell(const std::vector<std::string>& names, int& index, bool newer) {
    constexpr int kLast = 49; // 50 entries
    const int n = static_cast<int>(std::min<std::size_t>(names.size(), kLast + 1));
    auto set = [&](int i) { return i >= 0 && i < n && !names[static_cast<std::size_t>(i)].empty(); };
    if (!set(0)) return false;
    if (newer) {
        if (index > 0) {
            --index;
            return true;
        }
        // round to the last name: entry 49, else the first one set below it
        for (index = kLast; index >= 0; --index)
            if (set(index)) return true;
    } else if (index < kLast && set(index + 1)) {
        ++index;
        return true;
    }
    index = 0;
    return true;
}

std::string reply_target_text(const std::string& name, const std::string& rtarget_word) {
    std::string_view n = name;
    if (!n.empty() && (n[0] == ';' || n[0] == '[')) n.remove_prefix(1);
    return std::format("{} {}", rtarget_word, n);
}

CycleReply cycle_reply(std::string_view input, const std::vector<std::string>& names, int& index, bool newer,
                       const std::string& tell_word, const std::string& rtarget_word) {
    CycleReply out;
    // CXStr::Mid(1) of the input, then CompareN against each word past its first character (its slash)
    std::string_view rest = input.empty() ? input : input.substr(1);
    auto starts = [&](const std::string& word) {
        std::string_view w = word.empty() ? std::string_view() : std::string_view(word).substr(1);
        return rest.size() >= w.size() && rest.substr(0, w.size()) == w;
    };
    const bool rtarget = starts(rtarget_word);
    if (!rtarget && !starts(tell_word)) return out;
    if (!cycle_last_tell(names, index, newer)) out.messages.push_back(kStrNoTellsToReply);
    const std::string* name =
        index >= 0 && static_cast<std::size_t>(index) < names.size() && !names[static_cast<std::size_t>(index)].empty()
            ? &names[static_cast<std::size_t>(index)]
            : nullptr;
    if (!name) {
        out.messages.push_back(kStrNoTellsToTarget);
        return out;
    }
    out.text = rtarget ? reply_target_text(*name, rtarget_word)
                       : tell_menu_text(*name, tell_word.empty() ? tell_word : tell_word.substr(1));
    return out;
}

} // namespace hailstone::game
