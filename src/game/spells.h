#pragma once

// Spell data and the spell rules the client decides itself.
//
// Spell records come from the game's spells_us.txt: one line per spell, 237
// '^'-separated columns, loaded as the original's Spellmanager::LoadTextSpells
// @0x5c1c30 / EQ_Spell_InitFromLine @0x4ae710 do (lines that do not
// start with a digit are skipped; an id above 45000 stops the load). Column
// numbers follow the original's field map, which matches EQEmu's spells_new
// columns.
//
// The rules (cast checks, cast time, memorize speed, chat lines) follow the
// original's casting functions and CEverQuest::ReportSuccessfulHit @0x52ee40
// (the spell branch, type 231). The server decides fizzles, interrupts,
// resists, mana use and landing; the client only gates, times and reports.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "game/eq_strings.h"
#include "game/world_state.h"

namespace hailstone { class GameDir; }

namespace hailstone::game {

inline constexpr std::uint32_t kNoSpell = 0xffffffff;
inline constexpr std::uint32_t kMaxSpellId = 45000;  // IsValidSpellID: 1..45000
inline constexpr int kSpellGems = 12;                 // gems 0..11 (CastingSlot Gem1..Gem12)
inline constexpr int kItemGem = 12;                   // CastingSlot::Item
inline constexpr int kBaseSpellGems = 8;              // CanUseMemorizedSpellSlot: gems 0..7 always
inline constexpr int kSpellBookSize = 720;
inline constexpr int kLongBuffSlots = 42;             // RoF2 long buffs 0..41
inline constexpr int kShortBuffSlots = 20;            // songs 42..61
inline constexpr int kBuffSlots = 63;                 // PC slots incl. the discipline slot 62
inline constexpr int kMemorizeTicks = 92;             // StartSpellMemorization @0x75c106
inline constexpr std::uint32_t kGemLockMs = 10000;    // CastSpell @0x43c76b: gem/global lock until the server answers
inline constexpr std::uint32_t kBuffTickMs = 6000;    // ProcessAffects every 6 s


enum eEQSPA
{
	SPA_HP = 0,
	SPA_AC = 1,
	SPA_ATTACK_POWER = 2,
	SPA_MOVEMENT_RATE = 3,
	SPA_STR = 4,
	SPA_DEX = 5,
	SPA_AGI = 6,
	SPA_STA = 7,
	SPA_INT = 8,
	SPA_WIS = 9,
	SPA_CHA = 10,
	SPA_HASTE = 11, // "Melee Speed"
	SPA_INVISIBILITY = 12,
	SPA_SEE_INVIS = 13,
	SPA_ENDURING_BREATH = 14,
	SPA_MANA = 15,
	SPA_NPC_FRENZY = 16,
	SPA_NPC_AWARENESS = 17,
	SPA_NPC_AGGRO = 18,
	SPA_NPC_FACTION = 19,
	SPA_BLINDNESS = 20,
	SPA_STUN = 21,
	SPA_CHARM = 22,
	SPA_FEAR = 23,
	SPA_FATIGUE = 24,
	SPA_BIND_AFFINITY = 25,
	SPA_GATE = 26,
	SPA_DISPEL_MAGIC = 27,
	SPA_INVIS_VS_UNDEAD = 28,
	SPA_INVIS_VS_ANIMALS = 29,
	SPA_NPC_AGGRO_RADIUS = 30,
	SPA_ENTHRALL = 31,
	SPA_CREATE_ITEM = 32,
	SPA_SUMMON_PET = 33,
	SPA_CONFUSE = 34,
	SPA_DISEASE = 35,
	SPA_POISON = 36,
	SPA_DETECT_HOSTILE = 37,
	SPA_DETECT_MAGIC = 38,
	SPA_NO_TWINCAST = 39,
	SPA_INVULNERABILITY = 40,
	SPA_BANISH = 41,
	SPA_SHADOW_STEP = 42,
	SPA_BERSERK = 43,
	SPA_LYCANTHROPY = 44,
	SPA_VAMPIRISM = 45,
	SPA_RESIST_FIRE = 46,
	SPA_RESIST_COLD = 47,
	SPA_RESIST_POISON = 48,
	SPA_RESIST_DISEASE = 49,
	SPA_RESIST_MAGIC = 50,
	SPA_DETECT_TRAPS = 51,
	SPA_DETECT_UNDEAD = 52,
	SPA_DETECT_SUMMONED = 53,
	SPA_DETECT_ANIMALS = 54,
	SPA_STONESKIN = 55,
	SPA_TRUE_NORTH = 56,
	SPA_LEVITATION = 57,
	SPA_CHANGE_FORM = 58,
	SPA_DAMAGE_SHIELD = 59,
	SPA_TRANSFER_ITEM = 60,
	SPA_ITEM_LORE = 61,
	SPA_ITEM_IDENTIFY = 62,
	SPA_NPC_WIPE_HATE_LIST = 63,
	SPA_SPIN_STUN = 64,
	SPA_INFRAVISION = 65,
	SPA_ULTRAVISION = 66,
	SPA_EYE_OF_ZOMM = 67,
	SPA_RECLAIM_ENERGY = 68,
	SPA_MAX_HP = 69,
	SPA_CORPSE_BOMB = 70,
	SPA_CREATE_UNDEAD = 71,
	SPA_PRESERVE_CORPSE = 72,
	SPA_BIND_SIGHT = 73,
	SPA_FEIGN_DEATH = 74,
	SPA_VENTRILOQUISM = 75,
	SPA_SENTINEL = 76,
	SPA_LOCATE_CORPSE = 77,
	SPA_SPELL_SHIELD = 78,
	SPA_INSTANT_HP = 79,
	SPA_ENCHANT_LIGHT = 80,
	SPA_RESURRECT = 81,
	SPA_SUMMON_TARGET = 82,
	SPA_PORTAL = 83,
	SPA_HP_NPC_ONLY = 84,
	SPA_MELEE_PROC = 85,
	SPA_NPC_HELP_RADIUS = 86,
	SPA_MAGNIFICATION = 87,
	SPA_EVACUATE = 88,
	SPA_HEIGHT = 89,
	SPA_IGNORE_PET = 90,
	SPA_SUMMON_CORPSE = 91,
	SPA_HATE = 92,
	SPA_WEATHER_CONTROL = 93,
	SPA_FRAGILE = 94,
	SPA_SACRIFICE = 95,
	SPA_SILENCE = 96,
	SPA_MAX_MANA = 97,
	SPA_BARD_HASTE = 98,
	SPA_ROOT = 99,
	SPA_HEALDOT = 100,
	SPA_COMPLETEHEAL = 101,
	SPA_PET_FEARLESS = 102,
	SPA_CALL_PET = 103,
	SPA_TRANSLOCATE = 104,
	SPA_NPC_ANTI_GATE = 105,
	SPA_BEASTLORD_PET = 106,
	SPA_ALTER_PET_LEVEL = 107,
	SPA_FAMILIAR = 108,
	SPA_CREATE_ITEM_IN_BAG = 109,
	SPA_ARCHERY = 110,
	SPA_RESIST_ALL = 111,
	SPA_FIZZLE_SKILL = 112,
	SPA_SUMMON_MOUNT = 113,
	SPA_MODIFY_HATE = 114,
	SPA_CORNUCOPIA = 115,
	SPA_CURSE = 116,
	SPA_HIT_MAGIC = 117,
	SPA_AMPLIFICATION = 118,
	SPA_ATTACK_SPEED_MAX = 119,
	SPA_HEALMOD = 120,
	SPA_IRONMAIDEN = 121,
	SPA_REDUCESKILL = 122,
	SPA_IMMUNITY = 123,
	SPA_FOCUS_DAMAGE_MOD = 124,
	SPA_FOCUS_HEAL_MOD = 125,
	SPA_FOCUS_RESIST_MOD = 126,
	SPA_FOCUS_CAST_TIME_MOD = 127,
	SPA_FOCUS_DURATION_MOD = 128,
	SPA_FOCUS_RANGE_MOD = 129,
	SPA_FOCUS_HATE_MOD = 130,
	SPA_FOCUS_REAGENT_MOD = 131,
	SPA_FOCUS_MANACOST_MOD = 132,
	SPA_FOCUS_STUNTIME_MOD = 133,
	SPA_FOCUS_LEVEL_MAX = 134,
	SPA_FOCUS_RESIST_TYPE = 135,
	SPA_FOCUS_TARGET_TYPE = 136,
	SPA_FOCUS_WHICH_SPA = 137,
	SPA_FOCUS_BENEFICIAL = 138,
	SPA_FOCUS_WHICH_SPELL = 139,
	SPA_FOCUS_DURATION_MIN = 140,
	SPA_FOCUS_INSTANT_ONLY = 141,
	SPA_FOCUS_LEVEL_MIN = 142,
	SPA_FOCUS_CASTTIME_MIN = 143,
	SPA_FOCUS_CASTTIME_MAX = 144,
	SPA_NPC_PORTAL_WARDER_BANISH = 145,
	SPA_PORTAL_LOCATIONS = 146,
	SPA_PERCENT_HEAL = 147,
	SPA_STACKING_BLOCK = 148,
	SPA_STRIP_VIRTUAL_SLOT = 149,
	SPA_DIVINE_INTERVENTION = 150,
	SPA_POCKET_PET = 151,
	SPA_PET_SWARM = 152,
	SPA_HEALTH_BALANCE = 153,
	SPA_CANCEL_NEGATIVE_MAGIC = 154,
	SPA_POP_RESURRECT = 155,
	SPA_MIRROR = 156,
	SPA_FEEDBACK = 157,
	SPA_REFLECT = 158,
	SPA_MODIFY_ALL_STATS = 159,
	SPA_CHANGE_SOBRIETY = 160,
	SPA_SPELL_GUARD = 161,
	SPA_MELEE_GUARD = 162,
	SPA_ABSORB_HIT = 163,
	SPA_OBJECT_SENSE_TRAP = 164,
	SPA_OBJECT_DISARM_TRAP = 165,
	SPA_OBJECT_PICKLOCK = 166,
	SPA_FOCUS_PET = 167,
	SPA_DEFENSIVE = 168,
	SPA_CRITICAL_MELEE = 169,
	SPA_CRITICAL_SPELL = 170,
	SPA_CRIPPLING_BLOW = 171,
	SPA_EVASION = 172,
	SPA_RIPOSTE = 173,
	SPA_DODGE = 174,
	SPA_PARRY = 175,
	SPA_DUAL_WIELD = 176,
	SPA_DOUBLE_ATTACK = 177,
	SPA_MELEE_LIFETAP = 178,
	SPA_PURETONE = 179,
	SPA_SANCTIFICATION = 180,
	SPA_FEARLESS = 181,
	SPA_HUNDRED_HANDS = 182,
	SPA_SKILL_INCREASE_CHANCE = 183, // Unused
	SPA_ACCURACY = 184,
	SPA_SKILL_DAMAGE_MOD = 185,
	SPA_MIN_DAMAGE_DONE_MOD = 186,
	SPA_MANA_BALANCE = 187,
	SPA_BLOCK = 188,
	SPA_ENDURANCE = 189,
	SPA_INCREASE_MAX_ENDURANCE = 190,
	SPA_AMNESIA = 191,
	SPA_HATE_OVER_TIME = 192,
	SPA_SKILL_ATTACK = 193,
	SPA_FADE = 194,
	SPA_STUN_RESIST = 195,
	SPA_STRIKETHROUGH1 = 196, // Deprecated
	SPA_SKILL_DAMAGE_TAKEN = 197,
	SPA_INSTANT_ENDURANCE = 198,
	SPA_TAUNT = 199,
	SPA_PROC_CHANCE = 200,
	SPA_RANGE_ABILITY = 201,
	SPA_ILLUSION_OTHERS = 202,
	SPA_MASS_GROUP_BUFF = 203,
	SPA_GROUP_FEAR_IMMUNITY = 204,
	SPA_RAMPAGE = 205,
	SPA_AE_TAUNT = 206,
	SPA_FLESH_TO_BONE = 207,
	SPA_PURGE_POISON = 208,
	SPA_CANCEL_BENEFICIAL = 209,
	SPA_SHIELD_CASTER = 210,
	SPA_DESTRUCTIVE_FORCE = 211,
	SPA_FOCUS_FRENZIED_DEVASTATION = 212,
	SPA_PET_PCT_MAX_HP = 213,
	SPA_HP_MAX_HP = 214,
	SPA_PET_PCT_AVOIDANCE = 215,
	SPA_MELEE_ACCURACY = 216,
	SPA_HEADSHOT = 217,
	SPA_PET_CRIT_MELEE = 218,
	SPA_SLAY_UNDEAD = 219,
	SPA_INCREASE_SKILL_DAMAGE = 220,
	SPA_REDUCE_WEIGHT = 221,
	SPA_BLOCK_BEHIND = 222,
	SPA_DOUBLE_RIPOSTE = 223,
	SPA_ADD_RIPOSTE = 224,
	SPA_GIVE_DOUBLE_ATTACK = 225,
	SPA_2H_BASH = 226,
	SPA_REDUCE_SKILL_TIMER = 227,
	SPA_ACROBATICS = 228,
	SPA_CAST_THROUGH_STUN = 229,
	SPA_EXTENDED_SHIELDING = 230,
	SPA_BASH_CHANCE = 231,
	SPA_DIVINE_SAVE = 232,
	SPA_METABOLISM = 233,
	SPA_POISON_MASTERY = 234,
	SPA_FOCUS_CHANNELING = 235,
	SPA_FREE_PET = 236,
	SPA_PET_AFFINITY = 237,
	SPA_PERM_ILLUSION = 238,
	SPA_STONEWALL = 239,
	SPA_STRING_UNBREAKABLE = 240,
	SPA_IMPROVE_RECLAIM_ENERGY = 241,
	SPA_INCREASE_CHANGE_MEMWIPE = 242,
	SPA_ENHANCED_CHARM = 243,
	SPA_ENHANCED_ROOT = 244,
	SPA_TRAP_CIRCUMVENTION = 245,
	SPA_INCREASE_AIR_SUPPLY = 246,
	SPA_INCREASE_MAX_SKILL = 247,
	SPA_EXTRA_SPECIALIZATION = 248,
	SPA_OFFHAND_MIN_WEAPON_DAMAGE = 249,
	SPA_INCREASE_PROC_CHANCE = 250,
	SPA_ENDLESS_QUIVER = 251,
	SPA_BACKSTAB_FRONT = 252,
	SPA_CHAOTIC_STAB = 253,
	SPA_NOSPELL = 254,
	SPA_SHIELDING_DURATION_MOD = 255,
	SPA_SHROUD_OF_STEALTH = 256,
	SPA_GIVE_PET_HOLD = 257, // Deprecated
	SPA_TRIPLE_BACKSTAB = 258,
	SPA_AC_LIMIT_MOD = 259,
	SPA_ADD_INSTRUMENT_MOD = 260,
	SPA_SONG_MOD_CAP = 261,
	SPA_INCREASE_STAT_CAP = 262,
	SPA_TRADESKILL_MASTERY = 263,
	SPA_REDUCE_AA_TIMER = 264,
	SPA_NO_FIZZLE = 265,
	SPA_ADD_2H_ATTACK_CHANCE = 266,
	SPA_ADD_PET_COMMANDS = 267,
	SPA_ALCHEMY_FAIL_RATE = 268,
	SPA_FIRST_AID = 269,
	SPA_EXTEND_SONG_RANGE = 270,
	SPA_BASE_RUN_MOD = 271,
	SPA_INCREASE_CASTING_LEVEL = 272,
	SPA_DOTCRIT = 273,
	SPA_HEALCRIT = 274,
	SPA_MENDCRIT = 275,
	SPA_DUAL_WIELD_AMT = 276,
	SPA_EXTRA_DI_CHANCE = 277,
	SPA_FINISHING_BLOW = 278,
	SPA_FLURRY = 279,
	SPA_PET_FLURRY = 280,
	SPA_PET_FEIGN = 281,
	SPA_INCREASE_BANDAGE_AMT = 282,
	SPA_WU_ATTACK = 283,
	SPA_IMPROVE_LOH = 284,
	SPA_NIMBLE_EVASION = 285,
	SPA_FOCUS_DAMAGE_AMT = 286,
	SPA_FOCUS_DURATION_AMT = 287,
	SPA_ADD_PROC_HIT = 288,
	SPA_DOOM_EFFECT = 289,
	SPA_INCREASE_RUN_SPEED_CAP = 290,
	SPA_PURIFY = 291,
	SPA_STRIKETHROUGH = 292,
	SPA_STUN_RESIST2 = 293,
	SPA_SPELL_CRIT_CHANCE = 294,
	SPA_REDUCE_SPECIAL_TIMER = 295,
	SPA_FOCUS_DAMAGE_MOD_DETRIMENTAL = 296,
	SPA_FOCUS_DAMAGE_AMT_DETRIMENTAL = 297,
	SPA_TINY_COMPANION = 298,
	SPA_WAKE_DEAD = 299,
	SPA_DOPPELGANGER = 300,
	SPA_INCREASE_RANGE_DMG = 301,
	SPA_FOCUS_DAMAGE_MOD_CRIT = 302,
	SPA_FOCUS_DAMAGE_AMT_CRIT = 303,
	SPA_SECONDARY_RIPOSTE_MOD = 304,
	SPA_DAMAGE_SHIELD_MOD = 305,
	SPA_WEAK_DEAD_2 = 306,
	SPA_APPRAISAL = 307,
	SPA_ZONE_SUSPEND_MINION = 308,
	SPA_TELEPORT_CASTERS_BINDPOINT = 309,
	SPA_FOCUS_REUSE_TIMER = 310,
	SPA_FOCUS_COMBAT_SKILL = 311,
	SPA_OBSERVER = 312,
	SPA_FORAGE_MASTER = 313,
	SPA_IMPROVED_INVIS = 314,
	SPA_IMPROVED_INVIS_UNDEAD = 315,
	SPA_IMPROVED_INVIS_ANIMALS = 316,
	SPA_INCREASE_WORN_HP_REGEN_CAP = 317,
	SPA_INCREASE_WORN_MANA_REGEN_CAP = 318,
	SPA_CRITICAL_HP_REGEN = 319,
	SPA_SHIELD_BLOCK_CHANCE = 320,
	SPA_REDUCE_TARGET_HATE = 321,
	SPA_GATE_STARTING_CITY = 322,
	SPA_DEFENSIVE_PROC = 323,
	SPA_HP_FOR_MANA = 324,
	SPA_NO_BREAK_AE_SNEAK = 325,
	SPA_ADD_SPELL_SLOTS = 326,
	SPA_ADD_BUFF_SLOTS = 327,
	SPA_INCREASE_NEGATIVE_HP_LIMIT = 328,
	SPA_MANA_ABSORB_PCT_DMG = 329,
	SPA_CRIT_ATTACK_MODIFIER = 330,
	SPA_FAIL_ALCHEMY_ITEM_RECOVERY = 331,
	SPA_SUMMON_TO_CORPSE = 332,
	SPA_DOOM_RUNE_EFFECT = 333,
	SPA_NO_MOVE_HP = 334,
	SPA_FOCUSED_IMMUNITY = 335,
	SPA_ILLUSIONARY_TARGET = 336,
	SPA_INCREASE_EXP_MOD = 337,
	SPA_EXPEDIENT_RECOVERY = 338,
	SPA_FOCUS_CASTING_PROC = 339,
	SPA_CHANCE_SPELL = 340,
	SPA_WORN_ATTACK_CAP = 341,
	SPA_NO_PANIC = 342,
	SPA_SPELL_INTERRUPT = 343,
	SPA_ITEM_CHANNELING = 344,
	SPA_ASSASSINATE_MAX_LEVEL = 345,
	SPA_HEADSHOT_MAX_LEVEL = 346,
	SPA_DOUBLE_RANGED_ATTACK = 347,
	SPA_FOCUS_MANA_MIN = 348,
	SPA_INCREASE_SHIELD_DMG = 349,
	SPA_MANABURN = 350,
	SPA_SPAWN_INTERACTIVE_OBJECT = 351,
	SPA_INCREASE_TRAP_COUNT = 352,
	SPA_INCREASE_SOI_COUNT = 353,
	SPA_DEACTIVATE_ALL_TRAPS = 354,
	SPA_LEARN_TRAP = 355,
	SPA_CHANGE_TRIGGER_TYPE = 356,
	SPA_FOCUS_MUTE = 357,
	SPA_INSTANT_MANA = 358,
	SPA_PASSIVE_SENSE_TRAP = 359,
	SPA_PROC_ON_KILL_SHOT = 360,
	SPA_PROC_ON_DEATH = 361,
	SPA_POTION_BELT = 362,
	SPA_BANDOLIER = 363,
	SPA_ADD_TRIPLE_ATTACK_CHANCE = 364,
	SPA_PROC_ON_SPELL_KILL_SHOT = 365,
	SPA_GROUP_SHIELDING = 366,
	SPA_MODIFY_BODY_TYPE = 367,
	SPA_MODIFY_FACTION = 368,
	SPA_CORRUPTION = 369,
	SPA_RESIST_CORRUPTION = 370,
	SPA_SLOW = 371,
	SPA_GRANT_FORAGING = 372,
	SPA_DOOM_ALWAYS = 373,
	SPA_TRIGGER_SPELL = 374,
	SPA_CRIT_DOT_DMG_MOD = 375,
	SPA_FLING = 376,
	SPA_DOOM_ENTITY = 377,
	SPA_RESIST_OTHER_SPA = 378,
	SPA_DIRECTIONAL_TELEPORT = 379,
	SPA_EXPLOSIVE_KNOCKBACK = 380,
	SPA_FLING_TOWARD = 381,
	SPA_SUPPRESSION = 382,
	SPA_FOCUS_CASTING_PROC_NORMALIZED = 383,
	SPA_FLING_AT = 384,
	SPA_FOCUS_WHICH_GROUP = 385,
	SPA_DOOM_DISPELLER = 386,
	SPA_DOOM_DISPELLEE = 387,
	SPA_SUMMON_ALL_CORPSES = 388,
	SPA_REFRESH_SPELL_TIMER = 389,
	SPA_LOCKOUT_SPELL_TIMER = 390,
	SPA_FOCUS_MANA_MAX = 391,
	SPA_FOCUS_HEAL_AMT = 392,
	SPA_FOCUS_HEAL_MOD_BENEFICIAL = 393,
	SPA_FOCUS_HEAL_AMT_BENEFICIAL = 394,
	SPA_FOCUS_HEAL_MOD_CRIT = 395,
	SPA_FOCUS_HEAL_AMT_CRIT = 396,
	SPA_ADD_PET_AC = 397,
	SPA_FOCUS_SWARM_PET_DURATION = 398,
	SPA_FOCUS_TWINCAST_CHANCE = 399,
	SPA_HEALBURN = 400,
	SPA_MANA_IGNITE = 401,
	SPA_ENDURANCE_IGNITE = 402,
	SPA_FOCUS_SPELL_CLASS = 403,
	SPA_FOCUS_SPELL_SUBCLASS = 404,
	SPA_STAFF_BLOCK_CHANCE = 405,
	SPA_DOOM_LIMIT_USE = 406,
	SPA_DOOM_FOCUS_USED = 407,
	SPA_LIMIT_HP = 408,
	SPA_LIMIT_MANA = 409,
	SPA_LIMIT_ENDURANCE = 410,
	SPA_FOCUS_LIMIT_CLASS = 411,
	SPA_FOCUS_LIMIT_RACE = 412,
	SPA_FOCUS_BASE_EFFECTS = 413,
	SPA_FOCUS_LIMIT_SKILL = 414,
	SPA_FOCUS_LIMIT_ITEM_CLASS = 415,
	SPA_AC2 = 416,
	SPA_MANA2 = 417,
	SPA_FOCUS_INCREASE_SKILL_DMG_2 = 418,
	SPA_PROC_EFFECT_2 = 419,
	SPA_FOCUS_LIMIT_USE = 420,
	SPA_FOCUS_LIMIT_USE_AMT = 421,
	SPA_FOCUS_LIMIT_USE_MIN = 422,
	SPA_FOCUS_LIMIT_USE_TYPE = 423,
	SPA_GRAVITATE = 424,
	SPA_FLY = 425,
	SPA_ADD_EXTENDED_TARGET_SLOTS = 426,
	SPA_SKILL_PROC = 427,
	SPA_PROC_SKILL_MODIFIER = 428,
	SPA_SKILL_PROC_SUCCESS = 429,
	SPA_POST_EFFECT = 430,
	SPA_POST_EFFECT_DATA = 431,
	SPA_EXPAND_MAX_ACTIVE_TROPHY_BENEFITS = 432,
	SPA_ADD_NORMALIZED_SKILL_MIN_DMG_AMT = 433,
	SPA_ADD_NORMALIZED_SKILL_MIN_DMG_AMT_2 = 434,
	SPA_FRAGILE_DEFENSE = 435,
	SPA_FREEZE_BUFF_TIMER = 436,
	SPA_TELEPORT_TO_ANCHOR = 437,
	SPA_TRANSLOCATE_TO_ANCHOR = 438,
	SPA_ASSASSINATE = 439,
	SPA_FINISHING_BLOW_MAX = 440,
	SPA_DISTANCE_REMOVAL = 441,
	SPA_REQUIRE_TARGET_DOOM = 442,
	SPA_REQUIRE_CASTER_DOOM = 443,
	SPA_IMPROVED_TAUNT = 444,
	SPA_ADD_MERC_SLOT = 445,
	SPA_STACKER_A = 446,
	SPA_STACKER_B = 447,
	SPA_STACKER_C = 448,
	SPA_STACKER_D = 449,
	SPA_DOT_GUARD = 450,
	SPA_MELEE_THRESHOLD_GUARD = 451,
	SPA_SPELL_THRESHOLD_GUARD = 452,
	SPA_MELEE_THRESHOLD_DOOM = 453,
	SPA_SPELL_THRESHOLD_DOOM = 454,
	SPA_ADD_HATE_PCT = 455,
	SPA_ADD_HATE_OVER_TIME_PCT = 456,
	SPA_RESOURCE_TAP = 457,
	SPA_FACTION_MOD = 458,
	SPA_SKILL_DAMAGE_MOD_2 = 459,
	SPA_OVERRIDE_NOT_FOCUSABLE = 460,
	SPA_FOCUS_DAMAGE_MOD_2 = 461,
	SPA_FOCUS_DAMAGE_AMT_2 = 462,
	SPA_SHIELD = 463,
	SPA_PC_PET_RAMPAGE = 464,
	SPA_PC_PET_AE_RAMPAGE = 465,
	SPA_PC_PET_FLURRY = 466,
	SPA_DAMAGE_SHIELD_MITIGATION_AMT = 467,
	SPA_DAMAGE_SHIELD_MITIGATION_PCT = 468,
	SPA_CHANCE_BEST_IN_SPELL_GROUP = 469,
	SPA_TRIGGER_BEST_IN_SPELL_GROUP = 470,
	SPA_DOUBLE_MELEE_ATTACKS = 471,
	SPA_AA_BUY_NEXT_RANK = 472,
	SPA_DOUBLE_BACKSTAB_FRONT = 473,
	SPA_PET_MELEE_CRIT_DMG_MOD = 474,
	SPA_TRIGGER_SPELL_NON_ITEM = 475,
	SPA_WEAPON_STANCE = 476,
	SPA_HATELIST_TO_TOP = 477,
	SPA_HATELIST_TO_TAIL = 478,
	SPA_FOCUS_LIMIT_MIN_VALUE = 479,
	SPA_FOCUS_LIMIT_MAX_VALUE = 480,
	SPA_FOCUS_CAST_SPELL_ON_LAND = 481,
	SPA_SKILL_BASE_DAMAGE_MOD = 482,
	SPA_FOCUS_INCOMING_DMG_MOD = 483,
	SPA_FOCUS_INCOMING_DMG_AMT = 484,
	SPA_FOCUS_LIMIT_CASTER_CLASS = 485,
	SPA_FOCUS_LIMIT_SAME_CASTER = 486,
	SPA_EXTEND_TRADESKILL_CAP = 487,
	SPA_DEFENDER_MELEE_FORCE_PCT = 488,
	SPA_WORN_ENDURANCE_REGEN_CAP = 489,
	SPA_FOCUS_MIN_REUSE_TIME = 490,
	SPA_FOCUS_MAX_REUSE_TIME = 491,
	SPA_FOCUS_ENDURANCE_MIN = 492,
	SPA_FOCUS_ENDURANCE_MAX = 493,
	SPA_PET_ADD_ATK = 494,
	SPA_FOCUS_DURATION_MAX = 495,
	SPA_CRIT_MELEE_DMG_MOD_MAX = 496,
	SPA_FOCUS_CAST_PROC_NO_BYPASS = 497,
	SPA_ADD_EXTRA_PRIMARY_ATTACK_PCT = 498,
	SPA_ADD_EXTRA_SECONDARY_ATTACK_PCT = 499,
	SPA_FOCUS_CAST_TIME_MOD2 = 500,
	SPA_FOCUS_CAST_TIME_AMT = 501,
	SPA_FEARSTUN = 502,
	SPA_MELEE_DMG_POSITION_MOD = 503,
	SPA_MELEE_DMG_POSITION_AMT = 504,
	SPA_DMG_TAKEN_POSITION_MOD = 505,
	SPA_DMG_TAKEN_POSITION_AMT = 506,
	SPA_AMPLIFY_MOD = 507,
	SPA_AMPLIFY_AMT = 508,
	SPA_HEALTH_TRANSFER = 509,
	SPA_FOCUS_RESIST_INCOMING = 510,
	SPA_FOCUS_TIMER_MIN = 511,
	SPA_PROC_TIMER_MOD = 512,
	SPA_MANA_MAX = 513,
	SPA_ENDURANCE_MAX = 514,
	SPA_AC_AVOIDANCE_MAX = 515,
	SPA_AC_MITIGATION_MAX = 516,
	SPA_ATTACK_OFFENSE_MAX = 517,
	SPA_ATTACK_ACCURACY_MAX = 518,
	SPA_LUCK_AMT = 519,
	SPA_LUCK_PCT = 520,
	SPA_ENDURANCE_ABSORB_PCT_DMG = 521,
	SPA_INSTANT_MANA_PCT = 522,
	SPA_INSTANT_ENDURANCE_PCT = 523,
	SPA_DURATION_HP_PCT = 524,
	SPA_DURATION_MANA_PCT = 525,
	SPA_DURATION_ENDURANCE_PCT = 526,
};

enum eEQSPELLCAT
{
	SPELLCAT_AEGOLISM = 1,
	SPELLCAT_AGILITY = 2,
	SPELLCAT_ALLIANCE = 3,
	SPELLCAT_ANIMAL = 4,
	SPELLCAT_ANTONICA = 5,
	SPELLCAT_ARMOR_CLASS = 6,
	SPELLCAT_ATTACK = 7,
	SPELLCAT_BANE = 8,
	SPELLCAT_BLIND = 9,
	SPELLCAT_BLOCK = 10,
	SPELLCAT_CALM = 11,
	SPELLCAT_CHARISMA = 12,
	SPELLCAT_CHARM = 13,
	SPELLCAT_COLD = 14,
	SPELLCAT_COMBAT_ABILITIES = 15,
	SPELLCAT_COMBAT_INNATES = 16,
	SPELLCAT_CONVERSIONS = 17,
	SPELLCAT_CREATE_ITEM = 18,
	SPELLCAT_CURE = 19,
	SPELLCAT_DAMAGE_OVER_TIME = 20,
	SPELLCAT_DAMAGE_SHIELD = 21,
	SPELLCAT_DEFENSIVE = 22,
	SPELLCAT_DESTROY = 23,
	SPELLCAT_DEXTERITY = 24,
	SPELLCAT_DIRECT_DAMAGE = 25,
	SPELLCAT_DISARM_TRAPS = 26,
	SPELLCAT_DISCIPLINES = 27,
	SPELLCAT_DISCORD = 28,
	SPELLCAT_DISEASE = 29,
	SPELLCAT_DISEMPOWERING = 30,
	SPELLCAT_DISPEL = 31,
	SPELLCAT_DURATION_HEALS = 32,
	SPELLCAT_DURATION_TAP = 33,
	SPELLCAT_ENCHANT_METAL = 34,
	SPELLCAT_ENTHRALL = 35,
	SPELLCAT_FAYDWER = 36,
	SPELLCAT_FEAR = 37,
	SPELLCAT_FIRE = 38,
	SPELLCAT_FIZZLE_RATE = 39,
	SPELLCAT_FUMBLE = 40,
	SPELLCAT_HASTE = 41,
	SPELLCAT_HEALS = 42,
	SPELLCAT_HEALTH = 43,
	SPELLCAT_HEALTH_MANA = 44,
	SPELLCAT_HP_BUFFS = 45,
	SPELLCAT_HP_TYPE_ONE = 46,
	SPELLCAT_HP_TYPE_TWO = 47,
	SPELLCAT_ILLUSION_OTHER = 48,
	SPELLCAT_ILLUSION_ADVENTURER = 49,
	SPELLCAT_IMBUE_GEM = 50,
	SPELLCAT_INVISIBILITY = 51,
	SPELLCAT_INVULNERABILITY = 52,
	SPELLCAT_JOLT = 53,
	SPELLCAT_KUNARK = 54,
	SPELLCAT_LEVITATE = 55,
	SPELLCAT_LIFE_FLOW = 56,
	SPELLCAT_LUCLIN = 57,
	SPELLCAT_MAGIC = 58,
	SPELLCAT_MANA = 59,
	SPELLCAT_MANA_DRAIN = 60,
	SPELLCAT_MANA_FLOW = 61,
	SPELLCAT_MELEE_GUARD = 62,
	SPELLCAT_MEMORY_BLUR = 63,
	SPELLCAT_MISC = 64,
	SPELLCAT_MOVEMENT = 65,
	SPELLCAT_OBJECTS = 66,
	SPELLCAT_ODUS = 67,
	SPELLCAT_OFFENSIVE = 68,
	SPELLCAT_PET = 69,
	SPELLCAT_PET_HASTE = 70,
	SPELLCAT_PET_MISC_BUFFS = 71,
	SPELLCAT_PHYSICAL = 72,
	SPELLCAT_PICKLOCK = 73,
	SPELLCAT_PLANT = 74,
	SPELLCAT_POISON = 75,
	SPELLCAT_POWER_TAP = 76,
	SPELLCAT_QUICK_HEAL = 77,
	SPELLCAT_REFLECTION = 78,
	SPELLCAT_REGEN = 79,
	SPELLCAT_RESIST_BUFF = 80,
	SPELLCAT_RESIST_DEBUFFS = 81,
	SPELLCAT_RESURRECTION = 82,
	SPELLCAT_ROOT = 83,
	SPELLCAT_RUNE = 84,
	SPELLCAT_SENSE_TRAP = 85,
	SPELLCAT_SHADOWSTEP = 86,
	SPELLCAT_SHIELDING = 87,
	SPELLCAT_SLOW = 88,
	SPELLCAT_SNARE = 89,
	SPELLCAT_SPECIAL = 90,
	SPELLCAT_SPELL_FOCUS = 91,
	SPELLCAT_SPELL_GUARD = 92,
	SPELLCAT_SPELLSHIELD = 93,
	SPELLCAT_STAMINA = 94,
	SPELLCAT_STATISTIC_BUFFS = 95,
	SPELLCAT_STRENGTH = 96,
	SPELLCAT_STUN = 97,
	SPELLCAT_SUM_AIR = 98,
	SPELLCAT_SUM_ANIMATION = 99,
	SPELLCAT_SUM_EARTH = 100,
	SPELLCAT_SUM_FAMILIAR = 101,
	SPELLCAT_SUM_FIRE = 102,
	SPELLCAT_SUM_UNDEAD = 103,
	SPELLCAT_SUM_WARDER = 104,
	SPELLCAT_SUM_WATER = 105,
	SPELLCAT_SUMMON_ARMOR = 106,
	SPELLCAT_SUMMON_FOCUS = 107,
	SPELLCAT_SUMMON_FOOD_WATER = 108,
	SPELLCAT_SUMMON_UTILITY = 109,
	SPELLCAT_SUMMON_WEAPON = 110,
	SPELLCAT_SUMMONED = 111,
	SPELLCAT_SYMBOL = 112,
	SPELLCAT_TAELOSIA = 113,
	SPELLCAT_TAPS = 114,
	SPELLCAT_TECHNIQUES = 115,
	SPELLCAT_THE_PLANES = 116,
	SPELLCAT_TIMER_1 = 117,
	SPELLCAT_TIMER_2 = 118,
	SPELLCAT_TIMER_3 = 119,
	SPELLCAT_TIMER_4 = 120,
	SPELLCAT_TIMER_5 = 121,
	SPELLCAT_TIMER_6 = 122,
	SPELLCAT_TRANSPORT = 123,
	SPELLCAT_UNDEAD = 124,
	SPELLCAT_UTILITY_BENEFICIAL = 125,
	SPELLCAT_UTILITY_DETRIMENTAL = 126,
	SPELLCAT_VELIOUS = 127,
	SPELLCAT_VISAGES = 128,
	SPELLCAT_VISION = 129,
	SPELLCAT_WISDOM_INTELLIGENCE = 130,
	SPELLCAT_TRAPS = 131,
	SPELLCAT_AURAS = 132,
	SPELLCAT_ENDURANCE = 133,
	SPELLCAT_SERPENTS_SPINE = 134,
	SPELLCAT_CORRUPTION = 135,
	SPELLCAT_LEARNING = 136,
	SPELLCAT_CHROMATIC = 137,
	SPELLCAT_PRISMATIC = 138,
	SPELLCAT_SUM_SWARM = 139,
	SPELLCAT_DELAYED = 140,
	SPELLCAT_TEMPORARY = 141,
	SPELLCAT_TWINCAST = 142,
	SPELLCAT_SUM_BODYGUARD = 143,
	SPELLCAT_HUMANOID = 144,
	SPELLCAT_HASTE_SPELL_FOCUS = 145,
	SPELLCAT_TIMER_7 = 146,
	SPELLCAT_TIMER_8 = 147,
	SPELLCAT_TIMER_9 = 148,
	SPELLCAT_TIMER_10 = 149,
	SPELLCAT_TIMER_11 = 150,
	SPELLCAT_TIMER_12 = 151,
	SPELLCAT_HATRED = 152,
	SPELLCAT_FAST = 153,
	SPELLCAT_ILLUSION_SPECIAL = 154,
	SPELLCAT_TIMER_13 = 155,
	SPELLCAT_TIMER_14 = 156,
	SPELLCAT_TIMER_15 = 157,
	SPELLCAT_TIMER_16 = 158,
	SPELLCAT_TIMER_17 = 159,
	SPELLCAT_TIMER_18 = 160,
	SPELLCAT_TIMER_19 = 161,
	SPELLCAT_TIMER_20 = 162,
	SPELLCAT_ALARIS = 163,
	SPELLCAT_COMBINATION = 164,
	SPELLCAT_INDEPENDENT = 165,
	SPELLCAT_SKILL_ATTACKS = 166,
	SPELLCAT_INCOMING = 167,
	SPELLCAT_CURSE = 168,
	SPELLCAT_TIMER_21 = 169,
	SPELLCAT_TIMER_22 = 170,
	SPELLCAT_TIMER_23 = 171,
	SPELLCAT_TIMER_24 = 172,
	SPELLCAT_TIMER_25 = 173,
	SPELLCAT_DRUNKENNESS = 174,
	SPELLCAT_THROWING = 175,
	SPELLCAT_MELEE_DAMAGE = 176
};

// One spells_us.txt record (the fields the client uses).
struct SpellData {
    std::uint32_t id = 0;                      // col 0
    std::string name;                          // 1
    std::string player_1;                      // 2  (the projectile/target model text)
    std::string teleport_zone;                 // 3
    std::string cast_by_me, cast_by_other;     // 4, 5 (bard begin-cast texts)
    std::string cast_on_you, cast_on_other;    // 6, 7 (landing texts)
    std::string wear_off;                      // 8
    float range = 0, ae_range = 0, push_back = 0, push_up = 0;  // 9..12
    std::uint32_t cast_time = 0;               // 13 ms
    std::uint32_t recovery_time = 0;           // 14 ms (the global recovery after the cast)
    std::uint32_t recast_time = 0;             // 15 ms (the gem's own refresh)
    std::uint32_t duration_formula = 0;        // 16
    std::uint32_t duration_cap = 0;            // 17 tics
    std::uint32_t ae_duration = 0;             // 18
    std::int32_t mana = 0;                     // 19
    std::array<std::int32_t, 12> base{};       // 20..31
    std::array<std::int32_t, 12> base2{};      // 32..43 (limit)
    std::array<std::int32_t, 12> max{};        // 44..55
    std::int32_t book_icon = 0;                // 56
    std::int32_t gem_icon = 0;                 // 57 (memicon)
    std::array<std::int32_t, 4> reagent_id{};  // 58..61
    std::array<std::int32_t, 4> reagent_count{};      // 62..65
    std::array<std::int32_t, 4> no_expend_reagent{};  // 66..69
    std::array<std::int32_t, 12> formula{};    // 70..81
    std::uint8_t light_type = 0;               // 82
    std::uint8_t spell_type = 0;               // 83: 0 detrimental, 1 beneficial, 2 beneficial group-only
    std::uint8_t activated = 0;                // 84
    std::uint8_t resist = 0;                   // 85
    std::array<std::int32_t, 12> effect{};     // 86..97 SPA ids, 254 = unused
    std::uint8_t target_type = 0;              // 98
    std::uint8_t cast_difficulty = 0;          // 99
    std::uint8_t skill = 0;                    // 100
    std::uint8_t zone_type = 0;                // 101
    std::uint8_t environment = 0;              // 102
    std::uint8_t time_of_day = 0;              // 103
    std::array<std::uint8_t, 16> class_level{};// 104..119 (class 1..16; 255 = cannot use)
    std::uint8_t casting_anim = 0;             // 120
    std::uint8_t target_anim = 0;              // 121
    std::uint8_t travel_type = 0;              // 122
    std::int32_t spa_index = 0;                // 123
    std::uint32_t deity_mask = 0;              // 125..141 (+0x15c: bit n when column 125 + n is set; 0 any deity)
    bool cancel_on_sit = false;                // 124
    std::int32_t spell_icon = 0;               // 144 (A_SpellIcons cell)
    std::int32_t spell_anim = 0;               // 145
    bool uninterruptable = false;              // 146
    std::int32_t resist_adj = 0;               // 147
    bool not_stackable_dot = false;            // 148 (+0x239 NotStackableDot)
    bool deletable = false;                    // 149 (+0x23a Deletable: the book says it can be deleted, 4027)
    bool duration_window = false;              // 154 (short buff box)
    std::int32_t recourse_link = 0;            // 150 (+0x178 Autocast: CastSpell refuses it on yourself, 210)
    std::int32_t description_id = 0;           // 155 (dbstr type 6)
    std::int32_t category = 0;                 // 156 (dbstr type 5)
    std::int32_t subcategory = 0;              // 157
    std::int32_t subcategory2 = 0;             // 158 (+0x184; the combat ability window files an ability under both)
    std::int32_t endurance_cost = 0;           // 166
    std::int32_t reuse_timer_index = 0;        // 167 (linked timer 1..19)
    bool is_skill = false;                     // 168
    std::int32_t endurance_upkeep = 0;         // 174 (+0x19c)
    std::int32_t hit_count_type = 0;           // 175 (+0x1a4)
    std::int32_t hit_count = 0;                // 176 (+0x1a8)
    std::int32_t global_group = 0;             // 180 (+0x1c8; 0x2e: GetHPRegen's sitting check)
    std::int32_t pcnpc_only = 0;               // 183
    bool cast_not_standing = false;            // 184
    bool no_dispell = false;                   // 186 (+0x242 NoDispell)
    bool sneak = false;                        // 196 (+0x4dc SneakAttack: needs hiding, keeps invisibility)
    bool not_focusable = false;                // 197 (+0x4dd)
    bool stacks_with_self = false;             // 203 (+0x4df StacksWithSelf)
    bool suspendable = false;                  // 200 (+0x279 CountdownHeld: held while the buff timers are on hold)
    std::int32_t spell_group = 0;              // 207
    bool bypass_regen_check = false;           // 212 (+0x23b: does not stop fast regen)
    std::int32_t spell_rank = 0;               // 208 (+0x1dc)
    std::int32_t req_association = 0;         // 211 (+0x1e8 SpellReqAssociationID: CastSpell asks the spell manager)
    bool can_cast_in_combat = true;            // 213
    bool can_cast_out_of_combat = true;        // 214
    std::int32_t spell_class = 0;              // 221 (+0x1e0)
    std::int32_t spell_subclass = 0;           // 222 (+0x1e4)
    float distance_mod_start_x = 0;            // 227 (+0x21c)
    float distance_mod_start_mod = 0;          // 228 (+0x220)
    float distance_mod_end_x = 0;              // 229 (+0x224)
    float distance_mod_end_mod = 0;            // 230 (+0x228)
    // +0x4e9: (end_mod - start_mod) / (end_x - start_x), the divisor 1 when it lies strictly within FLT_EPSILON of 0
    // (the parser's range ratio, game/spell_record.cpp).
    float distance_mod_ratio = 0;
    float min_range = 0;                       // 231
    bool no_remove = false;                    // 232 (+0x4e3 NoRemove)
    bool only_during_fast_regen = false;       // 234
    std::int32_t spell_subgroup = 0;           // 236 (+0x1d8 SpellSubGroup: the rank swap's lookup)

    bool beneficial() const { return spell_type != 0; }
    // EQ_Spell::SpellAffects @0x4af770: any of the 12 slots.
    bool has_effect(int spa) const;
    // GetPCSpellAffect @0x433fd0's scan of a buff's spell: the slots before the first 254 (blank) only.
    bool has_effect_before_blank(int spa) const;
    // EQ_Spell::GetSpellLevelNeeded for one class (1..16); 255 when it cannot use the spell.
    std::uint8_t level_needed(std::uint8_t class_id) const;
};

// The loaded spell file.
// Whether spells_us.txt text is the live client's layout (165 carets a line, its texts moved to
// spells_us_str.txt) rather than RoF2's (236).
bool spells_text_is_live_layout(std::string_view text);
// A live spells_us.txt line as a RoF2 one (237 fields): its columns moved to RoF2's places, the five texts
// (you cast, other casts, cast on you, cast on other, fades) from spells_us_str.txt, the effects from the last field
// ("slot|spa|base|base2|formula|max" joined by '$'). Columns live has no copy of are 0 (deletable: 1).
std::string spell_line_from_live(std::string_view line, const std::array<std::string_view, 5>& texts);
// A whole live spells_us.txt with its spells_us_str.txt, as RoF2's text.
std::string spells_text_from_live(std::string_view spells, std::string_view strings);

class SpellTable {
public:
    SpellTable();
    // Reads spells_us.txt from the game directory (the server's assets first); false when it is missing. The live
    // client's layout (spells_text_is_live_layout) is read through spells_text_from_live.
    bool load(const GameDir& game);
    // Parses the file's text. Returns the number of records loaded.
    std::size_t parse(std::string_view text);
    // One line (without the line end); nullopt when it is not a spell record.
    static std::optional<SpellData> parse_line(std::string_view line);

    const SpellData* get(std::uint32_t id) const;
    // The spell named `name` (case-insensitive; the lowest id when several share it), or null.
    const SpellData* find_by_name(std::string_view name) const;
    // SpellManager::GetSpellByGroupAndRank @0x5c1ae0 over the
    // spells of SpellGroup `group` (> 0; the load hashes them by it): among those of SpellSubGroup `subgroup`, the
    // one of exactly `rank`; else, when `rank` is -1 or `below` for lower ranks, the highest rank seen. Null when
    // none. Of equal ranks the original takes the first in its hash chain; here the highest id.
    const SpellData* by_group_and_rank(std::int32_t group, std::int32_t subgroup, std::int32_t rank, bool below) const;
    std::size_t size() const { return spells_.size(); }
    bool empty() const { return spells_.empty(); }

    // SpellManager_GetSpellCRC @0x432560: EQ_Spell_CalcCRC of the spell's
    // record as the file gave it (game/spell_record.h), for ids 0..45000
    // (unsigned) the Unknown Spell's CRC when the file has no such line, and
    // above 45000 the Unknown Spell's too. OP_CastSpell carries it.
    std::uint32_t spell_crc(std::uint32_t id) const;
    // ClientSpellManager::LoadSpells' ComputeFileChecksum @0x808d90 of the
    // two files (__MemChecker1: CRC-32 of the whole file; 0 when it cannot be
    // read): OP_ZoneEntry +68 and +72.
    std::uint32_t file_crc() const { return file_crc_; }
    std::uint32_t assoc_file_crc() const { return assoc_file_crc_; }

private:
    std::unordered_map<std::uint32_t, SpellData> spells_;
    std::unordered_map<std::uint32_t, std::uint32_t> crcs_;
    std::uint32_t default_crc_ = 0;
    std::uint32_t file_crc_ = 0, assoc_file_crc_ = 0;
};

// EQEmu SpellTargetType values used by the client's checks.
enum TargetType : std::uint8_t {
    kTargetOptional = 1, kTargetAEClientV1 = 2, kTargetGroupTeleport = 3, kTargetAECaster = 4, kTargetSingle = 5,
    kTargetSelf = 6, kTargetAETarget = 8, kTargetAnimal = 9, kTargetUndead = 10, kTargetSummoned = 11,
    kTargetTap = 13, kTargetPet = 14, kTargetCorpse = 15, kTargetGroup = 41,
};

// EQ_Spell_TargetTypeNeedsTarget @0x432500: the single-target types.
bool target_type_needs_target(std::uint8_t target_type);
// The target types for which CastSpell's switch (@0x43b747) refuses to cast
// without a target (eqstr 214): the single-target types it checks and the
// targeted AE types 8, 21, 22, 23, 28.
bool target_type_requires_target(std::uint8_t target_type);

// CharacterZoneClient::CastSpell's checks are game/mechanics/cast_spell.h.

// The lowest level at which any class in `class_mask` (bit c-1 = class c) can
// use the spell (NMS multiclass: the server accepts any unlocked class,
// zone/client_process.cpp OPMemorizeSpell); 255 when none can. A mask of 0
// uses `class_id` alone.
std::uint8_t level_needed(const SpellData& spell, std::uint32_t class_mask, std::uint8_t class_id);

// The cast time itself is mechanics::cast_time_ms (game/mechanics/character.h).
// CastSpell treats casts of 100 ms or less as instant (@0x43c471).
inline bool is_instant(std::uint32_t cast_ms) { return cast_ms <= 100; }

// Memorising (CSpellBookWnd::GetSpellMemTicksLeft) is game/mechanics/spell_gems.h.

// The chat line for a spell hit (OP_Damage type 231), as ReportSuccessfulHit
// builds it: on the player CastOnYou (with "You have been healed for N
// points." / "You have taken N points of damage." for non-zero damage), on
// others "<target name><CastOnAnother>". nullopt when the spell has no such
// text or the target is unknown.
std::optional<std::string> spell_hit_message(const EqStrings& strings, const WorldState& world,
                                             const SpellData& spell, std::uint32_t target, std::int32_t damage);

// Buff time left: `tics` as of `received_ms` (the packet, or the client's
// last buff tick, game/passage_of_time.h).
// Durations of 0xfffffffe and above (and the permanent formula 50) never run out.
bool buff_is_permanent(std::uint32_t tics, const SpellData* spell);
std::uint64_t buff_ms_left(std::uint32_t tics, std::int64_t received_ms, std::int64_t now_ms);
// "333d", "1:05:00", "4:59", "12s".
std::string format_duration(std::uint64_t ms);

// ---- client-side state (owned by net::GameConnection) ---------------------

// A gem: the profile's memorised spell (+0x3060) and PlayerClient +0x2e8's ready time, absolute __EQGetTime ms
// (GameConnection::eq_time()), compared unsigned (game/mechanics/spell_gems.h).
struct SpellGem {
    std::uint32_t spell = kNoSpell;
    std::uint32_t ready = 0;
};

struct BuffSlot {
    std::uint32_t spell = kNoSpell;
    std::uint32_t tics = 0;          // as received
    std::int64_t received_ms = 0;
    std::uint8_t level = 0;
    std::uint32_t caster_id = 0;
    std::string caster_name;
    std::uint32_t num_hits = 0;
    float modifier = 1.0f;           // bard instrument modifier (EQ_Affect +4)
    std::uint8_t effect_type = 2;    // EQ_Affect +0: 2 buff, 4 inverted
    // EQ_Affect +0x28: the per-slot data (the counters left of a counter effect), OP_Buff's slot_data. EQEmu sends
    // zeros there (rof2.cpp ENCODE(OP_Buff): "TODO: implement slot_data stuff"); the player profile's buffs carry
    // none this client decodes.
    std::array<std::int32_t, 12> slot_data{};
    bool empty() const { return spell == kNoSpell || spell == 0; }
};

// The cast in progress (LaunchSpellData, PlayerZoneClient+0x444).
struct CastingData {
    std::int64_t eta_ms = 0;             // 0 when the bar is not running
    std::uint32_t spell = kNoSpell;      // -1 when not casting
    std::uint8_t slot = 0xff;            // gem
    std::uint32_t cast_time = 0;         // bar length
    std::uint32_t target = 0;
    // The cast item (+0x450 ItemGlobalIndex as CastSpell stores it: location, slot | sub << 16, aug; all -1 for a
    // gem) and its id (+0x460).
    std::array<std::uint32_t, 3> item_location{0xffffffffu, 0xffffffffu, 0x0000ffffu};
    std::uint32_t item_id = 0;
    std::uint32_t item_cast_type = 0; // +0x45c
    bool casting() const { return spell != kNoSpell; }
    // LaunchSpellData_IsItemCast @0x441e30: the location is 0..0x17 and the slot word is not -1.
    bool is_item_cast() const {
        return static_cast<std::int32_t>(item_location[0]) >= 0 && static_cast<std::int32_t>(item_location[0]) < 0x18 &&
               (item_location[1] & 0xffffu) != 0xffffu;
    }
};

// A memorization in progress (CSpellBookWnd +0x234..+0x23c); it ends when OP_MemorizeSpell goes out.
struct Memorizing {
    int gem = -1;
    int book_slot = -1;
    std::uint32_t spell = kNoSpell;
    int ticks_left = kMemorizeTicks;
};

// A scribe in progress (CSpellBookWnd ScribeBookSlot/SpellId/ScribeTicksLeft):
// 92 ticks, 3 per frame at most every 2 ms (GetSpellScribeTicksLeft @0x75e620, scribe_countdown).
struct Scribing {
    int book_slot = -1;
    std::uint32_t spell = kNoSpell;
    int ticks_left = kMemorizeTicks;
    bool sent = false;
};

// Somebody else's cast (OP_BeginCast), for the target window.
struct OtherCast {
    std::uint32_t spell = kNoSpell;
    std::int64_t started_ms = 0;
    std::uint32_t cast_time = 0;
};

struct SpellState {
    std::array<std::uint32_t, kSpellBookSize> book{};
    std::array<SpellGem, kSpellGems + 1> gems{}; // 12 = the item/AA slot
    std::uint32_t recovery_ready = 0;            // PlayerClient +0x3cc: the global recovery lock (__EQGetTime ms)
    CastingData casting;
    int pending = 0;                             // IsItemPending (requests the server has not answered)
    std::optional<Memorizing> memorizing;
    std::uint32_t memorize_last_tick = 0;        // 0xf749a8: the last memorise tick (0 after a memorise ends)
    int memorize_sent_gem = -1;                  // the gem of the OP_MemorizeSpell the server has not answered
    std::optional<Scribing> scribing;
    std::uint32_t scribe_last_tick = 0;          // 0xf749ac: the last scribe tick (0 after a scribe is sent)
    std::array<BuffSlot, kBuffSlots> buffs{};
    std::int64_t last_buff_tick_ms = 0;          // the character's 6 s tick timer (spawn +0x24c)
    std::int64_t last_upkeep_tick_ms = 0;        // its 1 s timer (spawn +0x2d8; 0: set at the next frame)
    std::uint32_t target_buffs_entity = 0;       // whose buffs target_buffs are (OP_TargetBuffs)
    std::vector<std::pair<std::uint32_t, BuffSlot>> target_buffs; // slot, buff
    std::unordered_map<std::uint32_t, OtherCast> others;           // spawn id → cast
    std::uint32_t hunger = 0, thirst = 0;        // the profile's, then OP_Stamina
    std::array<std::uint32_t, 20> linked_ready{}; // profile +0x6a0c: shared timers 1..19, Unix seconds (gem tint only)
    // profile +0x69bc: the combat ability (discipline) reuse timers 0..19 (PcBase_SetCombatAbilityTimer, read by
    // GetDisciplineReuseTimer), Unix seconds when each is ready; ids from 20 up are a server plugin's (NMS).
    std::array<std::uint32_t, 20> discipline_ready{};
    // The player spawn's +0x22d (CharacterZoneClient_AreBuffTimersOnHold): set by OP_SpawnAppearance 38 (value != 0),
    // by a SPA 0x1b4 buff landing or ticking; cleared with the spawn (PlayerZoneClient::ResetVariables), here at zone-in.
    bool buff_timers_on_hold = false;

    SpellState() { book.fill(kNoSpell); }
    // CCastSpellWnd::HandleSpellLeftClick's check: now (__EQGetTime) past the gem's timer and the recovery lock.
    bool gem_castable(int gem, std::uint32_t now) const;
    // RefreshSpellGemButtons' readiness (the tint), without the casting and linked-timer parts: now at or past both.
    bool gem_timers_done(int gem, std::uint32_t now) const;
};

// Spell_IsBuffCountdownHeld @0x4dcf20: a suspendable spell (+0x279) while the buff timers are on hold, unless it has
// SPA 0x1b4 itself (the global 0xddfc5c that would hold every suspendable buff is never written: 0). False without a
// spell.
bool buff_countdown_held(const SpellData* spell, bool timers_on_hold);

// CSpellBookWnd::GetSpellScribeTicksLeft @0x75e620's countdown, run when the book's scribe gauge is drawn: 3 ticks
// off when the display clock (`now`, CDisplay::TimeStamp ms) moved on more than 1 ms (unsigned) since `last`
// (0xf749ac), which then becomes now. Below 1 tick the scribe is due: the ticks and `last` go to 0 and it returns
// true (the caller sends OP_MemorizeSpell {book slot, spell, 0}).
bool scribe_countdown(Scribing& scribing, std::uint32_t& last, std::uint32_t now);

} // namespace hailstone::game
