#include "game/eq_names.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace nmsc::game {

namespace {

// Race ids 1-139 in order (index = id - 1).
constexpr std::string_view kRaces[] = {
    "Human", "Barbarian", "Erudite", "Wood Elf", "High Elf", "Dark Elf", "Half Elf", "Dwarf", "Troll", "Ogre",
    "Halfling", "Gnome", "Aviak", "Werewolf", "Brownie", "Centaur", "Golem", "Giant", "Trakanon", "Venril Sathir",
    "Evil Eye", "Beetle", "Kerran", "Fish", "Fairy", "Froglok", "Froglok Ghoul", "Fungusman", "Gargoyle", "Gasbag",
    "Gelatinous Cube", "Ghost", "Ghoul", "Giant Bat", "Giant Eel", "Giant Rat", "Giant Snake", "Giant Spider", "Gnoll",
    "Goblin", "Gorilla", "Wolf", "Bear", "Freeport Guard", "Demi Lich", "Imp", "Griffin", "Kobold", "Lava Dragon",
    "Lion", "Lizard Man", "Mimic", "Minotaur", "Orc", "Human Beggar", "Pixie", "Drachnid", "Solusek Ro",
    "Bloodgill", "Skeleton", "Shark", "Tunare", "Tiger", "Treant", "Vampire", "Statue of Rallos Zek",
    "Highpass Citizen", "Tentacle Terror", "Wisp", "Zombie", "Qeynos Citizen", "Ship", "Launch", "Piranha",
    "Elemental", "Puma", "Neriak Citizen", "Erudite Citizen", "Bixie", "Reanimated Hand", "Rivervale Citizen",
    "Scarecrow", "Skunk", "Snake Elemental", "Spectre", "Sphinx", "Armadillo", "Clockwork Gnome", "Drake",
    "Halas Citizen", "Alligator", "Grobb Citizen", "Oggok Citizen", "Kaladim Citizen", "Cazic Thule", "Cockatrice",
    "Daisy Man", "Elf Vampire", "Denizen", "Dervish", "Efreeti", "Froglok Tadpole", "Phinigel Autropos", "Leech",
    "Swordfish", "Felguard", "Mammoth", "Eye of Zomm", "Wasp", "Mermaid", "Harpy", "Fayguard", "Drixie",
    "Ghost Ship", "Clam", "Sea Horse", "Dwarf Ghost", "Erudite Ghost", "Sabertooth", "Wolf Elemental", "Gorgon",
    "Dragon Skeleton", "Innoruuk", "Unicorn", "Pegasus", "Djinn", "Invisible Man", "Iksar", "Scorpion", "Vah Shir",
    "Sarnak", "Draglock", "Drolvarg", "Mosquito", "Rhinoceros", "Xalgoz", "Kunark Goblin", "Yeti", "Iksar Citizen",
};

// Ids above the dense table that are common in play.
constexpr std::pair<std::uint32_t, std::string_view> kSparseRaces[] = {
    {240, "Teleport Man"},
    {330, "Froglok"},
    {522, "Drakkin"},
};

struct ClassNames {
    std::string_view name;
    std::string_view abbrev;
};

// Player classes 1-16 (index = id - 1).
constexpr ClassNames kClasses[] = {
    {"Warrior", "WAR"},   {"Cleric", "CLR"},      {"Paladin", "PAL"}, {"Ranger", "RNG"},
    {"Shadow Knight", "SHD"}, {"Druid", "DRU"},    {"Monk", "MNK"},    {"Bard", "BRD"},
    {"Rogue", "ROG"},     {"Shaman", "SHM"},      {"Necromancer", "NEC"}, {"Wizard", "WIZ"},
    {"Magician", "MAG"},  {"Enchanter", "ENC"},   {"Beastlord", "BST"}, {"Berserker", "BER"},
};

// NPC-only class ids.
constexpr std::pair<std::uint32_t, std::string_view> kNpcClasses[] = {
    {40, "Banker"},
    {41, "Merchant"},
    {59, "Discord Merchant"},
    {60, "Adventure Recruiter"},
    {61, "Adventure Merchant"},
    {62, "LDoN Treasure"},
    {63, "Tribute Master"},
    {64, "Guild Tribute Master"},
    {66, "Guild Banker"},
    {67, "Norrath's Keepers Merchant"},
    {68, "Dark Reign Merchant"},
    {69, "Fellowship Master"},
    {70, "Alternate Currency Merchant"},
    {71, "Mercenary Liaison"},
};

constexpr std::uint32_t kGmClassFirst = 20; // 20-35: GM trainers for classes 1-16
constexpr std::uint32_t kGmClassLast = 35;

} // namespace

std::string race_name(std::uint32_t race_id) {
    if (race_id >= 1 && race_id <= std::size(kRaces)) return std::string(kRaces[race_id - 1]);
    for (const auto& [id, name] : kSparseRaces)
        if (id == race_id) return std::string(name);
    return "Race " + std::to_string(race_id);
}

bool is_playable_race(std::uint32_t race_id) {
    return (race_id >= 1 && race_id <= 12) || race_id == 128 || race_id == 130 || race_id == 330 || race_id == 522;
}

std::string class_name(std::uint32_t class_id) {
    if (class_id >= 1 && class_id <= std::size(kClasses)) return std::string(kClasses[class_id - 1].name);
    if (class_id >= kGmClassFirst && class_id <= kGmClassLast)
        return std::string(kClasses[class_id - kGmClassFirst].name) + " Guildmaster";
    for (const auto& [id, name] : kNpcClasses)
        if (id == class_id) return std::string(name);
    return "Class " + std::to_string(class_id);
}

std::string class_short_name(std::uint32_t class_id) {
    if (class_id >= 1 && class_id <= std::size(kClasses)) return std::string(kClasses[class_id - 1].abbrev);
    return class_name(class_id);
}

std::string class_list_name(std::span<const std::uint32_t> class_ids, bool abbreviate) {
    std::string out;
    for (std::uint32_t id : class_ids) {
        if (id == 0) continue;
        if (!out.empty()) out += abbreviate ? "/" : " / ";
        out += abbreviate ? class_short_name(id) : class_name(id);
    }
    return out.empty() ? std::string("None") : out;
}

std::string class_mask_name(std::uint32_t mask) {
    std::string out;
    for (std::size_t c = 0; c < 16 && c < std::size(kClasses); ++c) {
        if (!((mask >> c) & 1u)) continue;
        std::string_view a = kClasses[c].abbrev; // "WAR" -> "War"
        if (!out.empty()) out += '/';
        out += a.substr(0, 1);
        for (char ch : a.substr(1)) out += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    }
    return out;
}

std::uint32_t language_eqstr(std::uint32_t lang) {
    static constexpr std::uint32_t kIds[] = {3114, 3200, 3201, 3202, 3203, 3204, 3205, 3206, 3207, 3208, 3217, 3219, 3220, 3209,
                                             3210, 3211, 3212, 3213, 3214, 3215, 3216, 3218, 3221, 3222, 3223, 7658, 7659};
    return lang < std::size(kIds) ? kIds[lang] : 3115;
}

} // namespace nmsc::game
