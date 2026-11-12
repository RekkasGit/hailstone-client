#pragma once

// Message colours for the expanded chat router. These classify text the client
// already builds from ordinary RoF2 packets; no server extension is required.

#include <cstdint>

#include "game/combat.h"

namespace hailstone::game {

inline constexpr std::uint32_t kChatNpcDeath = 358;
inline constexpr std::uint32_t kChatRandomOthers = 359;
inline constexpr std::uint32_t kChatRandomGroupRaid = 360;
inline constexpr std::uint32_t kChatEnvironmentYou = 361;
inline constexpr std::uint32_t kChatEnvironmentOthers = 362;
inline constexpr std::uint32_t kChatDamageShieldDefending = 363;
inline constexpr std::uint32_t kChatDamageShieldOthers = 364;
inline constexpr std::uint32_t kChatHealsYou = 374;
inline constexpr std::uint32_t kChatHealsOthers = 375;
inline constexpr std::uint32_t kChatBardSongs = 378;
inline constexpr std::uint32_t kChatDirectDamageOthers = 379;
inline constexpr std::uint32_t kChatSpellEmotes = 380;
inline constexpr std::uint32_t kChatHealsReceived = 402;
inline constexpr std::uint32_t kChatSpellFailuresOthers = 406;

constexpr std::uint32_t native_consider_chat_color(Con con) {
    switch (con) {
    case Con::Red: return 390;
    case Con::Yellow: return 391;
    case Con::White: return 392;
    case Con::DarkBlue: return 393;
    case Con::LightBlue: return 394;
    case Con::Green: return 395;
    default: return 396;
    }
}

constexpr std::uint32_t native_death_chat_color(bool npc, bool local) {
    return local ? 277u : npc ? kChatNpcDeath : 278u;
}

constexpr std::uint32_t native_environment_chat_color(std::uint8_t type, bool on_me,
                                                      std::uint32_t source, std::uint32_t local) {
    // The native shield-flavour range includes burning (0xfa). A source-less
    // burning hit is lava; drowning, falls and traps are always environmental.
    if (type >= 0xf3 && type <= 0xfa && source != 0) {
        if (on_me) return 329; // the existing Damage Shields (You Attacking) colour
        return source == local ? kChatDamageShieldDefending : kChatDamageShieldOthers;
    }
    return on_me ? kChatEnvironmentYou : kChatEnvironmentOthers;
}

constexpr std::uint32_t native_random_chat_color(bool yours, bool group_or_raid) {
    return yours ? 287u : group_or_raid ? kChatRandomGroupRaid : kChatRandomOthers;
}

constexpr bool native_song_skill(std::uint8_t skill) {
    return skill == 12 || skill == 41 || skill == 49 || skill == 54 || skill == 70;
}

constexpr std::uint32_t native_spell_hit_chat_color(std::int32_t damage, bool on_me, bool from_me, bool song) {
    if (damage < 0) return from_me ? kChatHealsYou : on_me ? kChatHealsReceived : kChatHealsOthers;
    if (damage > 0) return on_me ? 283u : from_me ? 397u : kChatDirectDamageOthers;
    return song ? kChatBardSongs : kChatSpellEmotes;
}

} // namespace hailstone::game
