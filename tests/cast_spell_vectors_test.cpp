// CharacterZoneClient::CastSpell (game/mechanics/cast_spell.h) against the
// original's vectors (rof2decomp 978d6270): CastSpell_Checks (1000), _Buffs
// (600), _Items (1000), _Target (1000), _Range (1200), _BodyType (1200) and
// _Send (800).
//
// Each record holds the inputs, every mocked callee and fake virtual the
// original called (in order, with its arguments and answer), the caster fields
// and globals it wrote and the OP_CastSpell payload. Two answers of the
// generator's world are not in the record: CDisplay::PlayerDistance's float
// and whether the ChangePosition fake took effect (it sets the stand state). rof2decomp's Randomize
// (re/test/tests/cast_spell_test.cpp) is ported below with its random
// generator (one stream per file, seeds 0xca51..0xca57), and the regenerated
// inputs are first compared with the record's "in". The port then runs
// against a replay: each callee it calls must be the record's next call with
// the same arguments, and gets the recorded answer. Finally the return value,
// pinstTarget, the caster fields written (sentinels, as the generator), the
// globals and the payload are compared, and the payload net::encode builds
// for the same fields (OP_CastSpell) must give the same bytes.
#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/mechanics/cast_spell.h"
#include "net/spell_packets.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using hailstone::test::Json;
namespace cs = hailstone::game::mechanics::cast;

namespace {

// ---------------------------------------------------------------- the generator (rof2decomp test harness)

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 0x9e3779b9u) {}
    uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int Range(int lo, int hi) {
        return static_cast<int>(static_cast<uint32_t>(lo) +
                                Next() % (static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u));
    }
    int Range(std::size_t lo, std::size_t hi) { return Range(static_cast<int>(lo), static_cast<int>(hi)); }
    int Range(int lo, std::size_t hi) { return Range(lo, static_cast<int>(hi)); }
    bool Chance(int percent) { return static_cast<int>(Next() % 100) < percent; }
    int Interesting() {
        static const int edges[] = {0, 1, -1, 2, 9, 10, 11, 59, 60, 61, 99, 100, 255, 256, 330, 1000, 0x7fffffff,
                                    static_cast<int>(0x80000000), 10000000, 9999999};
        uint32_t pick = Next() % 4;
        if (pick == 0) return edges[Next() % (sizeof(edges) / sizeof(edges[0]))];
        if (pick == 3) return static_cast<int>(Next());
        return Range(-5, 400);
    }
};

enum Family : uint32_t { kChecks = 1, kSanctuary = 2, kItems = 4, kTarget = 8, kRange = 16, kBody = 32, kSend = 64 };
constexpr int kBuffs = 8;

struct Case {
    uint32_t focus;
    // arguments
    uint32_t slot; // argument 1 (a byte; CastSpell reads the low byte only)
    int32_t spellIdArg;
    bool hasItem;
    int32_t itemCastType; // argument 5
    uint8_t sendPacket;   // argument 6
    bool hasPosition;     // argument 9
    // caster spawn
    bool hasSpawn, casterIsLocal;
    uint8_t localBlock;      // byte 0xddf70c
    uint8_t casterType;      // +0x125
    uint32_t masterId;       // +0x38c
    uint8_t mercenary;       // +0x208
    uint8_t standState;      // +0x35c
    bool standTakesEffect;   // the ChangePosition fake sets +0x35c
    uint8_t autoStand;       // byte 0xde0d5b
    int32_t hideMode;        // +0x338
    int32_t castingSpellId;  // +0x448 (-1: not casting)
    int32_t hpMax;           // +0x2dc
    uint32_t casterId;       // +0x148
    uint32_t floorBits, ceilingBits, heightBits; // +0x28, +0x11b4, +0x138
    uint32_t xBits, yBits, zBits;                // +0x68, +0x64, +0x6c
    uint8_t casterVirtualDc;                     // caster vtable +0xdc
    // character (virtual base)
    uint8_t cacheValid;   // CharacterZoneClient +0x1a4
    uint8_t stunned;      // character +0xfc
    uint8_t charState;    // character +0x104
    uint8_t skipCost;     // character +0x460
    uint8_t char90;       // virtual +0x90
    int32_t rank;         // virtual +0x0 (3)
    int32_t char34;       // virtual +0x34
    int32_t level;        // virtual +0x68
    bool hasPcZone;       // virtual +0x4c
    uint8_t classId;      // the current profile's +0x3374
    bool isLocalPc;       // pinstLocalPC + 0x2dc8 == this
    // spell
    bool spellValid, spellFound;
    int32_t spellId;      // +0x174
    uint32_t rangeBits, minRangeBits; // +0x0, +0x22c
    uint32_t castTime;    // +0x10
    int32_t mana, endurance, hpCost; // +0x28, +0x194, +0x2c
    int32_t group, subgroup, rankCount; // +0x1d4, +0x1d8, +0x1dc
    bool rankSpellFound;
    int32_t ranksSpellId;
    uint8_t inCombatOk, outOfCombatOk, restOnly, notStanding, ability; // +0x23c, +0x23d, +0x23f, +0x240, +0x244
    uint8_t beneficial, targetType, field277, sneak; // +0x26b, +0x26e, +0x277, +0x4dc
    int32_t noSelf;       // +0x178
    int32_t targetRestriction; // +0x1cc
    int32_t field1e8;     // +0x1e8
    uint8_t levelNeeded;  // +0x247.. (per class)
    int32_t spa[12];      // +0x11c
    // target / pet / owner
    int initialTarget; // 0 none, 1 caster, 2 target
    uint8_t tType, tMercenary, tSummoned; uint32_t tMaster, tCharacter, tId; uint32_t tZBits, tHeightBits;
    bool petFound; uint8_t pType, pSummoned; uint32_t pId;
    bool ownerFound; uint8_t oType; uint32_t oId;
    // globals
    uint8_t petitionBlock;  // byte 0xddfc69 (SPA 25 block)
    int32_t zoneType;       // 0xdcee70
    int32_t eq5c0;          // CEverQuest +0x5c0
    uint8_t quiet;          // byte 0xddf654
    int32_t demo;           // 0xddf790
    bool connected;
    uint32_t now;           // CDisplay +0x154
    // mock answers
    uint8_t transact[2];
    int32_t buffSpa[3], aaSpa[3], itemSpa[3]; // SPA 40, 312, 324
    uint8_t inCombat, resting;
    int32_t itemRecastTime, itemRecastTimer, fastTime;
    int maxEffects;
    uint8_t buffActive[kBuffs];
    int buffKind[kBuffs]; // 0 no spell, 1 has 312, 2 ends (0xfe) before 312, 3 other
    uint8_t madeVisible;
    int32_t itemType;
    bool castingValid, castingFound;
    uint8_t bardCasting, bardMain;
    int32_t itemReqLevel;
    uint8_t attuneable; int32_t attuned, charges;
    int32_t effectType, wornSlot;
    uint8_t canUse;
    bool cursorItem, slot21Item;
    uint8_t canSeePos, canSee, notSameDeity;
    int32_t curMana, curEndurance;
    int32_t focusRange;
    uint32_t distanceBits;
    bool targetHasPc;
    uint8_t check12;
    uint8_t groupCaster, groupTarget, groupOwner;
    uint64_t properties;
    int32_t itemBaseType, itemSpellId;
    uint8_t spellAllowed;
    int32_t castMod, focusCastMod;
    int32_t itemId, itemCastTime, itemRecastType;
    uint32_t crc;
    uint8_t sendResult;
};

Case g;
float g_position[3];
uint32_t g_location[3];

// ---------------------------------------------------------------- random inputs
uint32_t Fbits(float f) {
    return std::bit_cast<uint32_t>(f);
}
float Fl(uint32_t bits) {
    return std::bit_cast<float>(bits);
}

bool Bad(Rng &rng, uint32_t family, int percent) {
    return (g.focus & family) ? rng.Chance(percent) : rng.Chance(1);
}

uint32_t RandomFloatBits(Rng &rng) {
    static const uint32_t kEdges[] = {0, 0x80000000u, 0x7fc00000u, 0x7f800000u, 0xff800000u, 0x3f800000u, 0xbf800000u,
        0x00000001u, 0x7f7fffffu};
    if (rng.Chance(40)) {
        return kEdges[rng.Range(0, 8)];
    }
    return Fbits(static_cast<float>(rng.Range(-2000, 2000)) * 0.25f);
}

// A float near `value`: equal, one ulp off either way, or a little off.
uint32_t Near(Rng &rng, float value) {
    uint32_t bits = Fbits(value);
    switch (rng.Range(0, 5)) {
    case 0:
    case 1:
        return bits;
    case 2:
        return bits + 1;
    case 3:
        return bits ? bits - 1 : bits;
    case 4:
        return Fbits(value + 0.5f);
    default:
        return Fbits(value - 0.5f);
    }
}

const uint8_t kSimpleTypes[] = {2, 3, 4, 19, 24, 25, 26, 29, 30, 31, 32, 33, 36, 37, 40, 41, 42, 44, 46};
const uint8_t kRangeTypes[] = {0, 1, 8, 21, 22, 23, 28, 45, 5, 9, 10, 11, 12, 13, 15, 16, 17, 18, 20, 27, 34, 35, 39,
    43, 47, 14, 38};
const uint8_t kBodyTypes[] = {9, 23, 10, 21, 11, 22, 12, 13, 20, 14, 15, 16, 17, 18, 19, 27, 28, 34, 35, 38, 39, 43, 6, 6, 6, 43,
    7, 5, 47};

void Randomize(Rng &rng, uint32_t focus) {
    std::memset(&g, 0, sizeof(g));
    g.focus = focus;
    // target type first: it decides which checks matter
    if (focus & kRange) {
        g.targetType = kRangeTypes[rng.Range(0, sizeof(kRangeTypes) - 1)];
    }
    else if (focus & kBody) {
        g.targetType = kBodyTypes[rng.Range(0, sizeof(kBodyTypes) - 1)];
    }
    else if (rng.Chance(75)) {
        g.targetType = kSimpleTypes[rng.Range(0, sizeof(kSimpleTypes) - 1)];
    }
    else {
        static const uint8_t kOthers[] = {6, 0x0e, 0x26, 0x2d, 0x2f, 5, 0x30, 0xff, 1, 7};
        g.targetType = kOthers[rng.Range(0, 9)];
    }
    if ((focus & kChecks) && rng.Chance(8)) {
        g.targetType = 0x2d;
    }
    // arguments
    g.slot = rng.Chance(80) ? static_cast<uint32_t>(rng.Range(0, 0xb)) : static_cast<uint32_t>(rng.Range(0xc, 0x10));
    if (rng.Chance(3)) {
        g.slot = 0xff;
    }
    g.spellIdArg = rng.Chance(90) ? rng.Range(1, 9000) : (rng.Chance(50) ? 0x5de : 0xa9c);
    g.hasItem = rng.Chance((focus & kItems) ? 70 : ((focus & kSend) ? 40 : 12));
    g.itemCastType = rng.Chance(75) ? 0 : rng.Range(1, 12);
    g.sendPacket = static_cast<uint8_t>(rng.Chance(85) ? 1 : 0);
    g.hasPosition = rng.Chance(g.targetType == 0x2d ? 85 : 30);
    g_position[0] = static_cast<float>(rng.Range(-4000, 4000)) * 0.125f;
    g_position[1] = static_cast<float>(rng.Range(-4000, 4000)) * 0.125f;
    g_position[2] = static_cast<float>(rng.Range(-800, 800)) * 0.125f;
    g_location[0] = static_cast<uint32_t>(rng.Range(0, 2));
    g_location[1] = static_cast<uint32_t>(rng.Range(-1, 33));
    g_location[2] = static_cast<uint32_t>(rng.Chance(50) ? -1 : rng.Range(0, 9)) | (rng.Chance(30) ? 0xffff0000u : 0);
    // caster
    g.hasSpawn = !Bad(rng, kChecks, 3);
    g.casterIsLocal = rng.Chance(75);
    g.localBlock = static_cast<uint8_t>(Bad(rng, kChecks, 10) ? (rng.Chance(80) ? 1 : 2) : 0);
    g.casterType = static_cast<uint8_t>(rng.Chance(80) ? 0 : (rng.Chance(70) ? 1 : rng.Range(2, 3)));
    g.masterId = rng.Chance(70) ? 0 : static_cast<uint32_t>(rng.Range(1, 5000));
    g.mercenary = static_cast<uint8_t>(rng.Chance(85) ? 0 : 1);
    g.standState = 0x64;
    if (Bad(rng, kChecks, 22)) {
        static const uint8_t kStates[] = {0x6e, 0x66, 0x6f, 0x73, 0x78, 0x65, 0};
        g.standState = kStates[rng.Range(0, 6)];
    }
    g.standTakesEffect = rng.Chance(50);
    g.autoStand = static_cast<uint8_t>(rng.Chance(50) ? 1 : 0);
    g.hideMode = rng.Chance(70) ? 0 : (rng.Chance(80) ? rng.Range(1, 3) : -1);
    g.castingSpellId = -1;
    if (Bad(rng, kItems, 30)) {
        g.castingSpellId = g.spellIdArg + 1000;
    }
    g.hpMax = rng.Chance(80) ? rng.Range(1, 30000) : rng.Range(-50, 0);
    g.casterId = static_cast<uint32_t>(rng.Range(1, 0xffff));
    g.floorBits = Fbits(static_cast<float>(rng.Range(-200, 200)));
    g.heightBits = Fbits(static_cast<float>(rng.Range(1, 40)) * 0.5f);
    {
        float room = Fl(g.heightBits) * 3.0f;
        g.ceilingBits = rng.Chance(60) ? Near(rng, Fl(g.floorBits) + room)
                                         : Fbits(Fl(g.floorBits) + static_cast<float>(rng.Range(0, 100)));
        if (rng.Chance(5)) {
            g.ceilingBits = 0x7fc00000u;
        }
    }
    g.xBits = Fbits(static_cast<float>(rng.Range(-4000, 4000)) * 0.125f);
    g.yBits = Fbits(static_cast<float>(rng.Range(-4000, 4000)) * 0.125f);
    g.zBits = Fbits(static_cast<float>(rng.Range(-800, 800)) * 0.125f);
    g.casterVirtualDc = static_cast<uint8_t>(rng.Chance(80) ? 0 : 1);
    // character
    g.cacheValid = static_cast<uint8_t>(rng.Chance(50) ? 1 : 0);
    g.stunned = static_cast<uint8_t>(Bad(rng, kChecks, 10) ? 1 : (rng.Chance(10) ? 2 : 0));
    g.charState = 0x64;
    if (Bad(rng, kTarget, 15)) {
        static const uint8_t kStates[] = {0x73, 0x78, 0x6e, 0};
        g.charState = kStates[rng.Range(0, 3)];
    }
    g.skipCost = static_cast<uint8_t>(rng.Chance(85) ? 0 : 1);
    g.char90 = static_cast<uint8_t>(rng.Chance(70) ? 0 : 1);
    g.rank = rng.Chance(30) ? -1 : rng.Range(0, 4);
    g.char34 = rng.Chance(50) ? 0 : rng.Range(1, 3);
    g.level = rng.Range(1, 100);
    g.hasPcZone = !Bad(rng, kChecks, 10);
    g.classId = static_cast<uint8_t>(rng.Chance(20) ? 8 : rng.Range(1, 16));
    if (rng.Chance(2)) {
        g.classId = static_cast<uint8_t>(rng.Chance(50) ? 0 : 40);
    }
    g.isLocalPc = rng.Chance(85);
    // spell
    g.spellValid = !Bad(rng, kChecks, 4);
    g.spellFound = !Bad(rng, kChecks, 4);
    g.spellId = rng.Chance(90) ? g.spellIdArg : rng.Range(1, 9000);
    if (rng.Chance(3)) {
        g.spellId = rng.Chance(50) ? 0x3d6 : 0xad4;
    }
    if ((focus & kRange) && rng.Chance(10)) {
        g.spellId = 0x3d6; // the NPC range of 10000
    }
    if ((focus & kItems) && rng.Chance(10)) {
        g.spellId = 0xad4; // exempt from the SPA 32 cursor check
    }
    g.castTime = static_cast<uint32_t>(rng.Chance(70) ? rng.Range(0, 8000) : rng.Range(0, 200));
    g.mana = rng.Range(0, 800);
    g.endurance = rng.Chance(70) ? 0 : rng.Range(1, 500);
    g.hpCost = rng.Chance(80) ? rng.Range(-100, 100) : rng.Interesting();
    if (rng.Chance(30)) {
        g.hpCost = -g.hpMax + rng.Range(-1, 1);
    }
    g.group = rng.Chance(50) ? 0 : (rng.Chance(85) ? rng.Range(1, 5000) : -1);
    g.subgroup = rng.Range(0, 20);
    g.rankCount = rng.Range(0, 4);
    g.rankSpellFound = rng.Chance(85);
    g.inCombatOk = static_cast<uint8_t>(Bad(rng, kChecks, 20) ? 0 : 1);
    g.outOfCombatOk = static_cast<uint8_t>(Bad(rng, kChecks, 20) ? 0 : 1);
    g.restOnly = static_cast<uint8_t>(Bad(rng, kChecks, 15) ? 1 : 0);
    g.notStanding = static_cast<uint8_t>(rng.Chance(15) ? 1 : 0);
    g.ability = static_cast<uint8_t>(rng.Chance(30) ? 1 : 0);
    g.beneficial = static_cast<uint8_t>(rng.Chance(50) ? 1 : (rng.Chance(80) ? 0 : 2));
    g.field277 = static_cast<uint8_t>(rng.Chance(70) ? 3 : rng.Range(0, 4));
    g.sneak = static_cast<uint8_t>(Bad(rng, kSanctuary, 25) ? 1 : 0);
    g.noSelf = Bad(rng, kBody, 15) ? 1 : 0;
    g.targetRestriction = Bad(rng, kTarget, 30) ? rng.Range(1, 3) : 0;
    g.field1e8 = rng.Range(0, 50);
    g.levelNeeded = static_cast<uint8_t>(Bad(rng, kItems, 25) ? g.level + rng.Range(1, 20) : rng.Range(0, g.level));
    if (rng.Chance(2)) {
        g.levelNeeded = static_cast<uint8_t>(rng.Chance(50) ? 255 : 0);
    }
    // effect ids: the ones CastSpell tests (25, 27, 32, 91, 113, 152) mostly in their own families
    static const int32_t kPlain[] = {0, 1, 10, 3, 79, 40, 312, 254};
    for (int k = 0; k < 12; ++k) {
        g.spa[k] = rng.Chance(50) ? 254 : kPlain[rng.Range(0, 7)];
    }
    struct Tested {
        int32_t spa;
        uint32_t family;
        int percent;
    };
    static const Tested kTested[] = {{25, kItems, 25}, {32, kItems, 25}, {27, kTarget | kRange, 40},
        {91, kBody, 50}, {113, kTarget, 25}, {152, kTarget, 20}};
    for (const Tested &t : kTested) {
        if (Bad(rng, t.family, t.percent)) {
            g.spa[rng.Range(0, 11)] = t.spa;
        }
    }
    // target, pet, owner
    g.initialTarget = rng.Chance(10) ? 0 : (rng.Chance(20) ? 1 : 2);
    g.tType = static_cast<uint8_t>(rng.Chance(45) ? 0 : (rng.Chance(80) ? 1 : rng.Range(2, 3)));
    g.tMercenary = static_cast<uint8_t>(rng.Chance(80) ? 0 : 1);
    g.tSummoned = static_cast<uint8_t>(rng.Chance(50) ? 1 : 0);
    g.tMaster = rng.Chance(60) ? 0 : static_cast<uint32_t>(rng.Range(1, 5000));
    g.tCharacter = rng.Chance(30) ? 0 : 1;
    g.tId = static_cast<uint32_t>(rng.Range(1, 0xffff));
    g.tZBits = Fbits(Fl(g.zBits) + static_cast<float>(rng.Range(-60, 60)));
    g.tHeightBits = Fbits(static_cast<float>(rng.Range(1, 40)) * 0.5f);
    if (rng.Chance(40)) { // around the 39.99 height difference
        float diff = rng.Chance(50) ? 39.99f : -39.99f;
        g.tZBits = Near(rng, Fl(g.zBits) + diff + Fl(g.tHeightBits) - Fl(g.heightBits));
    }
    g.petFound = rng.Chance(70);
    g.pType = static_cast<uint8_t>(rng.Chance(80) ? 1 : 0);
    g.pSummoned = static_cast<uint8_t>(rng.Chance(70) ? 1 : 0);
    g.pId = static_cast<uint32_t>(rng.Range(1, 0xffff));
    g.ownerFound = rng.Chance(70);
    g.oType = static_cast<uint8_t>(rng.Chance(70) ? 0 : 1);
    g.oId = static_cast<uint32_t>(rng.Range(1, 0xffff));
    // globals
    g.petitionBlock = static_cast<uint8_t>(Bad(rng, kItems, 15) ? 1 : 0);
    g.zoneType = rng.Chance(30) ? 4 : rng.Range(0, 5);
    g.eq5c0 = rng.Chance(70) ? 0 : 1;
    g.quiet = static_cast<uint8_t>(Bad(rng, kSend, 15) ? 1 : 0);
    g.demo = Bad(rng, kSend, 10) ? 1 : 0;
    g.connected = !Bad(rng, kSend, 15);
    g.now = rng.Next();
    // mock answers
    g.transact[0] = static_cast<uint8_t>(Bad(rng, kChecks, 10) ? 0 : 1);
    g.transact[1] = static_cast<uint8_t>(g.transact[0] || !rng.Chance(60) ? 1 : 0);
    for (int k = 0; k < 3; ++k) {
        uint32_t family = k == 0 ? kChecks : (k == 1 ? kSanctuary : kTarget);
        g.buffSpa[k] = Bad(rng, family, 12) ? (rng.Chance(80) ? rng.Range(1, 3) : -1) : 0;
        g.aaSpa[k] = Bad(rng, family, 8) ? (rng.Chance(80) ? 1 : -1) : 0;
        g.itemSpa[k] = Bad(rng, family, 8) ? (rng.Chance(80) ? 1 : -2) : 0;
    }
    g.inCombat = static_cast<uint8_t>(rng.Chance(40) ? 1 : 0);
    g.resting = static_cast<uint8_t>(rng.Chance(60) ? 1 : 0);
    g.itemRecastTime = rng.Chance(50) ? 0 : (rng.Chance(85) ? rng.Range(1, 600) : -1);
    g.fastTime = rng.Range(1000, 100000);
    g.itemRecastTimer = Bad(rng, kChecks, 30) ? g.fastTime + rng.Range(1, 500) : g.fastTime - rng.Range(0, 500);
    g.maxEffects = rng.Range(0, kBuffs);
    for (int k = 0; k < kBuffs; ++k) {
        g.buffActive[k] = static_cast<uint8_t>(rng.Chance(70) ? 1 : 0);
        g.buffKind[k] = rng.Range(0, 3);
    }
    g.itemType = Bad(rng, kItems, 8) ? 0x14 : (rng.Chance(20) ? 0x15 : rng.Range(0, 30));
    g.castingValid = rng.Chance(80);
    g.castingFound = rng.Chance(80);
    g.bardCasting = static_cast<uint8_t>(rng.Chance(70) ? 1 : 0);
    g.bardMain = static_cast<uint8_t>(rng.Chance(30) ? 1 : 0);
    g.itemReqLevel = Bad(rng, kItems, 15) ? g.level + rng.Range(1, 10) : g.level - rng.Range(0, 10);
    g.attuneable = static_cast<uint8_t>(rng.Chance(30) ? 1 : 0);
    g.attuned = Bad(rng, kItems, 30) ? 0 : 1;
    g.charges = Bad(rng, kItems, 15) ? 0 : (rng.Chance(85) ? rng.Range(1, 10) : -1);
    g.effectType = rng.Chance(30) ? 4 : (rng.Chance(40) ? 5 : rng.Range(0, 7));
    g.wornSlot = rng.Chance(70) ? rng.Range(0, 0x16) : (rng.Chance(50) ? -1 : 0x17);
    g.canUse = static_cast<uint8_t>(Bad(rng, kItems, 20) ? 0 : 1);
    g.cursorItem = Bad(rng, kItems, 20);
    g.slot21Item = !Bad(rng, kBody, 30);
    g.canSeePos = static_cast<uint8_t>(Bad(rng, kTarget, 25) ? 0 : 1);
    g.canSee = static_cast<uint8_t>(Bad(rng, kTarget, 25) ? 0 : 1);
    g.notSameDeity = static_cast<uint8_t>(Bad(rng, kTarget, 40) ? 1 : 0);
    g.curMana = Bad(rng, kTarget, 20) ? g.mana - rng.Range(1, 50) : g.mana + rng.Range(0, 50);
    g.curEndurance = Bad(rng, kTarget, 20) ? g.endurance - rng.Range(1, 50) : g.endurance + rng.Range(0, 50);
    g.focusRange = rng.Chance(60) ? 0 : (rng.Chance(85) ? rng.Range(-50, 300) : rng.Interesting());
    g.targetHasPc = rng.Chance(80);
    g.check12 = static_cast<uint8_t>(Bad(rng, kBody, 40) ? 0 : 1);
    g.groupCaster = static_cast<uint8_t>(rng.Chance(60) ? 1 : 0);
    g.groupTarget = static_cast<uint8_t>(Bad(rng, kBody, 40) ? 0 : 1);
    g.groupOwner = static_cast<uint8_t>(rng.Chance(50) ? 1 : 0);
    g.properties = 0;
    if (!Bad(rng, kBody, 45)) {
        g.properties |= 1ull << rng.Range(0, 0x22);
    }
    if (rng.Chance(20)) {
        g.properties |= (static_cast<uint64_t>(rng.Next()) << 32 | rng.Next()) & 0x7ffffffffull;
    }
    g.itemBaseType = rng.Chance(60) ? 0 : 1;
    g.itemSpellId = rng.Chance(60) ? rng.Range(2, 9000) : rng.Range(-1, 1);
    g.spellAllowed = static_cast<uint8_t>(Bad(rng, kRange | kBody, 15) ? 0 : 1);
    g.castMod = rng.Chance(50) ? 0 : (rng.Chance(85) ? rng.Range(-3000, 1000) : rng.Interesting());
    g.focusCastMod = rng.Chance(50) ? 0 : (rng.Chance(85) ? rng.Range(-3000, 500) : rng.Interesting());
    if (rng.Chance(10)) {
        g.focusCastMod = -static_cast<int32_t>(g.castTime) - g.castMod + rng.Range(-1, 1);
    }
    g.itemId = rng.Range(1, 200000);
    g.itemCastTime = rng.Chance(40) ? rng.Range(0, 100) : rng.Range(101, 10000);
    if (rng.Chance(10)) {
        g.itemCastTime = rng.Chance(50) ? 100 : 101;
    }
    g.itemRecastType = rng.Range(-1, 30);
    g.crc = rng.Next();
    g.sendResult = static_cast<uint8_t>(Bad(rng, kSend, 15) ? 0 : 1);

    // ranges and distances
    static const float kRanges[] = {0.0f, 10.0f, 100.0f, 200.0f, 250.5f, 500.0f, 416.0f, 20.0f};
    g.rangeBits = rng.Chance(85) ? Fbits(kRanges[rng.Range(0, 7)]) : RandomFloatBits(rng);
    g.minRangeBits = rng.Chance(60) ? 0 : (rng.Chance(85) ? Fbits(static_cast<float>(rng.Range(1, 60))) : RandomFloatBits(rng));
    {
        float range = Fl(g.rangeBits);
        float effective = range + static_cast<float>(g.focusRange);
        if (effective > range * 5.0f) {
            effective = range * 5.0f;
        }
        float npcRange = range * 1.2f < 500.0f ? 500.0f : range * 1.2f;
        float pick = g.casterType == 1 ? (rng.Chance(50) ? range * 1.2f : npcRange) : effective;
        switch (rng.Range(0, 4)) {
        case 0:
        case 1:
            g.distanceBits = Near(rng, pick);
            break;
        case 2:
            g.distanceBits = Near(rng, Fl(g.minRangeBits));
            break;
        case 3:
            g.distanceBits = Fbits(static_cast<float>(rng.Range(0, 4000)) * 0.25f);
            break;
        default:
            g.distanceBits = RandomFloatBits(rng);
            break;
        }
        if (g.targetType == 45 && g.hasPosition && rng.Chance(70)) { // the distance comes from the position
            float d = Fl(Near(rng, rng.Chance(70) ? pick : Fl(g.minRangeBits)));
            g_position[0] = Fl(g.yBits) + d;
            g_position[1] = Fl(g.xBits);
            g_position[2] = Fl(g.zBits);
        }
    }
}


// ---------------------------------------------------------------- the world of one record (the generator's Apply)

constexpr uint32_t kSentinel = 0xeeeeeeeeu;

struct World {
    cs::Spawn caster, target, pet, owner, other;
    cs::Spell spell, rank_spell, casting_spell;
    std::array<cs::Spell, kBuffs> buff_spell{};
    std::array<cs::Affect, kBuffs> buffs{};
    cs::Item item, other_item;
    cs::State st;
    cs::Args args;
};

void build_spawn(cs::Spawn& s, uint8_t type, uint32_t id) {
    s = cs::Spawn{};
    s.type = type;
    s.spawn_id = id;
    s.stand_state = 0;
}

void build(World& w) {
    build_spawn(w.caster, g.casterType, g.casterId);
    cs::Spawn& c = w.caster;
    c.stand_state = g.standState;
    c.master_id = g.masterId;
    c.mercenary = g.mercenary;
    c.hide_mode = g.hideMode;
    c.hp_max = g.hpMax;
    c.floor_height = g.floorBits;
    c.ceiling = g.ceilingBits;
    c.height = g.heightBits;
    c.y = g.yBits;
    c.x = g.xBits;
    c.z = g.zBits;
    c.gem_eta.fill(kSentinel);
    c.cooldown_eta = kSentinel;
    c.casting.eta = kSentinel;
    c.casting.spell_id = g.castingSpellId;
    c.casting.slot = 0xee;
    c.casting.item_location.fill(kSentinel);
    c.casting.item_cast_type = kSentinel;
    c.casting.item_id = kSentinel;
    c.casting.y = c.casting.x = kSentinel;
    c.casting.target_id = kSentinel;
    c.casting.free_target = 0xee;
    c.casting.target_position.fill(kSentinel);
    build_spawn(w.target, g.tType, g.tId);
    w.target.mercenary = g.tMercenary;
    w.target.summoned = g.tSummoned;
    w.target.master_id = g.tMaster;
    w.target.has_character = g.tCharacter != 0;
    w.target.z = g.tZBits;
    w.target.height = g.tHeightBits;
    build_spawn(w.pet, g.pType, g.pId);
    w.pet.summoned = g.pSummoned;
    build_spawn(w.owner, g.oType, g.oId);
    build_spawn(w.other, 0, 1);
    for (cs::Spell* s : {&w.spell, &w.rank_spell, &w.casting_spell}) {
        *s = cs::Spell{};
        s->range = g.rangeBits;
        s->min_range = g.minRangeBits;
        s->cast_time = g.castTime;
        s->mana = g.mana;
        s->base0 = g.hpCost;
        s->id = g.spellId;
        s->no_self = g.noSelf;
        s->endurance = g.endurance;
        s->pcnpc_only = g.targetRestriction;
        s->group = g.group;
        s->subgroup = g.subgroup;
        s->rank = g.rankCount;
        s->req_association = g.field1e8;
        s->can_cast_in_combat = g.inCombatOk;
        s->can_cast_out_of_combat = g.outOfCombatOk;
        s->only_during_fast_regen = g.restOnly;
        s->cast_not_standing = g.notStanding;
        s->is_skill = g.ability;
        for (std::size_t k = 1; k <= 35; ++k) s->class_level[k] = g.levelNeeded;
        s->spell_type = g.beneficial;
        s->target_type = g.targetType;
        s->travel_type = g.field277;
        s->sneak = g.sneak;
        for (std::size_t k = 0; k < 12; ++k) s->spa[k] = g.spa[k];
    }
    w.rank_spell.id = g.spellId + 1;
    for (std::size_t k = 0; k < kBuffs; ++k) {
        w.buffs[k].active = g.buffActive[k];
        w.buffs[k].spell_id = 20000 + static_cast<int32_t>(k);
        cs::Spell& s = w.buff_spell[k];
        s = cs::Spell{};
        s.spa.fill(3);
        if (g.buffKind[k] == 1) s.spa[k % 12] = 312;
        if (g.buffKind[k] == 2) {
            s.spa[k % 6] = 254;
            s.spa[k % 6 + 1] = 312;
        }
    }
    w.item.charges = g.charges;
    w.item.attuned = g.attuned;
    w.other_item = cs::Item{};
    cs::State& st = w.st;
    st = cs::State{};
    st.caster = g.hasSpawn ? &w.caster : nullptr;
    st.local_player = g.casterIsLocal ? &w.caster : &w.other;
    st.target = g.initialTarget == 0 ? nullptr : (g.initialTarget == 1 ? &w.caster : &w.target);
    st.is_local_pc = g.isLocalPc;
    st.local_block = g.localBlock;
    st.auto_stand = g.autoStand;
    st.spa25_block = g.petitionBlock;
    st.zone_type = g.zoneType;
    st.free_target_tracker = g.eq5c0;
    st.quiet = g.quiet;
    st.demo = g.demo;
    st.connected = g.connected;
    st.now = g.now;
    st.effects_cached = g.cacheValid;
    st.stunned = g.stunned;
    st.state_104 = g.charState;
    st.skip_cost = g.skipCost;
    st.class_id = g.classId;
    cs::Args& a = w.args;
    a = cs::Args{};
    a.slot = static_cast<uint8_t>(g.slot);
    a.spell_id = g.spellIdArg;
    a.item = g.hasItem ? &w.item : nullptr;
    for (std::size_t k = 0; k < 3; ++k) a.item_location[k] = g_location[k];
    a.item_cast_type = g.itemCastType;
    a.send_packet = g.sendPacket;
    if (g.hasPosition)
        a.position = std::array<uint32_t, 3>{std::bit_cast<uint32_t>(g_position[0]), std::bit_cast<uint32_t>(g_position[1]),
                                             std::bit_cast<uint32_t>(g_position[2])};
}

// The record's "in" against the regenerated inputs (the streams must agree).
std::string check_inputs(const Json& in) {
    auto f = [](const Json& j, uint32_t bits) {
        float v = std::bit_cast<float>(bits);
        if (v != v) return true; // NaN: the writer's text form
        return hailstone::test::f32_bits(j) == bits;
    };
    if (in["slot"].i64() != static_cast<uint8_t>(g.slot) || in["spell_id_arg"].i32() != g.spellIdArg ||
        in["has_item"].truthy() != g.hasItem || in["item_cast_type"].i32() != g.itemCastType ||
        in["send_packet"].i64() != g.sendPacket)
        return "regenerated arguments differ";
    for (std::size_t k = 0; k < 3; ++k)
        if (in["item_location"][k].u32() != g_location[k]) return "regenerated item location differs";
    if (in["position"].is_null() == g.hasPosition) return "regenerated position flag differs";
    if (g.hasPosition)
        for (std::size_t k = 0; k < 3; ++k)
            if (!f(in["position"][k], std::bit_cast<uint32_t>(g_position[k]))) return "regenerated position differs";
    const Json& c = in["caster"];
    if (c.is_null() == g.hasSpawn) return "regenerated caster differs";
    if (g.hasSpawn && (c["spawn_id"].u32() != g.casterId || c["stand_state"].i64() != g.standState ||
                       c["hp_max"].i32() != g.hpMax || !f(c["floor_height"], g.floorBits) ||
                       !f(c["field_11b4"], g.ceilingBits) || !f(c["avatar_height"], g.heightBits) ||
                       !f(c["y"], g.yBits) || !f(c["x"], g.xBits) || !f(c["z"], g.zBits)))
        return "regenerated caster fields differ";
    const Json& s = in["spell"];
    if (s["id"].i32() != g.spellId || !f(s["range"], g.rangeBits) || !f(s["min_range"], g.minRangeBits) ||
        s["cast_time"].u32() != g.castTime || s["mana"].i32() != g.mana || s["target_type"].i64() != g.targetType ||
        s["field_1e8"].i32() != g.field1e8)
        return "regenerated spell differs";
    const Json& t = in["target"];
    if (t["spawn_id"].u32() != g.tId || !f(t["z"], g.tZBits) || !f(t["avatar_height"], g.tHeightBits))
        return "regenerated target differs";
    if (in["now"].u32() != g.now || in["buffs"].items.size() != static_cast<std::size_t>(g.maxEffects))
        return "regenerated globals differ";
    return "";
}

// ---------------------------------------------------------------- the replay

class Replay final : public cs::Queries {
public:
    Replay(World& w, const Json& calls, bool stand_effect) : w_(w), calls_(calls), stand_effect_(stand_effect) {}
    std::string error;
    std::size_t used() const { return next_; }

    std::string role(const cs::Spawn* p) const {
        if (!p) return "none";
        if (p == &w_.caster) return "caster";
        if (p == &w_.target) return "target";
        if (p == &w_.pet) return "pet";
        if (p == &w_.owner) return "owner";
        if (p == &w_.other) return "local_player";
        return "other";
    }
    std::string spell_role(const cs::Spell* p) const {
        if (!p) return "none";
        if (p == &w_.spell) return "spell";
        if (p == &w_.rank_spell) return "rank_spell";
        if (p == &w_.casting_spell) return "casting_spell";
        for (const cs::Spell& b : w_.buff_spell)
            if (p == &b) return "buff_spell";
        return "other";
    }

    bool is_valid_spell_id(int32_t id) override {
        auto* c = take("is_valid_spell_id");
        arg(c, "id", id);
        return ret(c) != 0;
    }
    const cs::Spell* spell_by_id(int32_t id) override {
        auto* c = take("get_spell_by_id");
        arg(c, "id", id);
        return spell_named(c, id);
    }
    bool character_virtual_90() override { return ret(take("character_virtual_90")) != 0; }
    int32_t character_rank(int32_t a) override {
        auto* c = take("character_virtual_00");
        arg(c, "arg", a);
        return static_cast<int32_t>(ret(c));
    }
    const cs::Spell* spell_by_group_and_rank(int32_t group, int32_t sub, int32_t rank, int32_t a4) override {
        auto* c = take("get_spell_by_group_and_rank");
        arg(c, "group", group);
        arg(c, "subgroup", sub);
        arg(c, "rank", rank);
        arg(c, "arg4", a4);
        return spell_named(c, 0);
    }
    bool ok_to_transact() override { return ret(take("is_ok_to_transact")) != 0; }
    void pending_request_report(int32_t reason) override { arg(take("pending_request_report"), "reason", reason); }
    void dsp_chat(uint32_t eqstr, int32_t color, uint8_t a3, uint8_t a4) override {
        auto* c = take("dsp_chat");
        arg(c, "eqstr", eqstr);
        arg(c, "color", color);
        arg(c, "arg3", a3);
        arg(c, "arg4", a4);
    }
    void change_position(cs::Spawn& s, uint8_t state) override {
        auto* c = take("change_position");
        arg(c, "state", state);
        arg_s(c, "spawn", role(&s));
        if (stand_effect_) s.stand_state = state;
    }
    void message(uint32_t eqstr, int32_t color, int32_t a4) override {
        auto* c = take("token_string_to_client");
        arg(c, "eqstr", eqstr);
        arg(c, "color", color);
        arg(c, "arg4", a4);
    }
    void recache_spell_effects() override { take("recache_spell_effects"); }
    int32_t cached_buff_effect(int32_t spa, int32_t sub) override { return spa_call("cached_buff_effect", spa, sub); }
    int32_t cached_aa_effect(int32_t spa, int32_t sub) override { return spa_call("cached_aa_effect", spa, sub); }
    int32_t cached_item_effect(int32_t spa, int32_t sub) override { return spa_call("cached_item_effect", spa, sub); }
    cs::Handle pc_zone() override { return named(take("character_pc_zone"), "pc_zone") ? 1 : 0; }
    bool in_combat() override { return ret(take("is_in_combat")) != 0; }
    bool resting_for_fast_regen() override { return ret(take("is_resting_for_fast_regen")) != 0; }
    int32_t item_spell_recast_time(int32_t i) override { return indexed("item_spell_recast_time", i); }
    int32_t item_recast_timer() override { return static_cast<int32_t>(ret(take("get_item_recast_timer"))); }
    int32_t fast_time() override { return static_cast<int32_t>(ret(take("fast_time"))); }
    void create_target_indicator(uint8_t slot, const cs::Spell& spell, int32_t ict) override {
        auto* c = take("create_target_indicator");
        arg(c, "slot", slot);
        arg_s(c, "spell", spell_role(&spell));
        arg(c, "item_cast_type", ict);
    }
    uint8_t max_effects() override { return static_cast<uint8_t>(ret(take("get_max_effects"))); }
    cs::Affect* effect(int32_t index) override {
        arg(take("get_effect"), "index", index);
        return &w_.buffs[static_cast<std::size_t>(index >= 0 && index < kBuffs ? index : kBuffs - 1)];
    }
    void remove_affect(cs::Affect& e, int32_t a2, int32_t a3) override {
        auto* c = take("remove_affect");
        arg(c, "buff_index", &e - w_.buffs.data());
        arg(c, "arg2", a2);
        arg(c, "arg3", a3);
        e.active = 0;
    }
    void make_me_visible(int32_t a1, int32_t a2) override {
        auto* c = take("make_me_visible");
        arg(c, "arg1", a1);
        arg(c, "arg2", a2);
    }
    int32_t item_type() override { return static_cast<int32_t>(ret(take("item_type"))); }
    int32_t bard_cast_bard(const cs::Spell* spell, uint8_t class_id) override {
        auto* c = take("bard_cast_bard");
        arg_s(c, "spell", spell_role(spell));
        arg(c, "class", class_id);
        return static_cast<int32_t>(ret(c));
    }
    int32_t item_spell_required_level(int32_t i) override { return indexed("item_spell_required_level", i); }
    int32_t character_level() override { return static_cast<int32_t>(ret(take("character_level"))); }
    int32_t item_is_attuneable() override { return static_cast<int32_t>(ret(take("item_is_attuneable"))); }
    int32_t item_spell_effect_type(int32_t i) override { return indexed("item_spell_effect_type", i); }
    const cs::Item* inventory_item(int32_t slot) override {
        auto* c = take("inventory_get_item");
        arg(c, "slot", slot);
        return item_named(c);
    }
    bool can_use_item(int32_t a2, uint8_t a3) override {
        auto* c = take("can_use_item");
        arg(c, "arg2", a2);
        arg(c, "arg3", a3);
        return ret(c) != 0;
    }
    const cs::Item* pc_item(int32_t slot) override {
        auto* c = take("pc_get_item");
        arg(c, "slot", slot);
        return item_named(c);
    }
    int32_t character_virtual_34() override { return static_cast<int32_t>(ret(take("character_virtual_34"))); }
    cs::Spawn* pet_spawn() override { return spawn_named(take("get_pet_spawn")); }
    cs::Spawn* spawn_by_id(uint32_t id) override {
        auto* c = take("get_spawn_by_id");
        arg(c, "id", static_cast<int32_t>(id));
        return spawn_named(c);
    }
    bool can_see_position(cs::Spawn& s, const std::array<uint32_t, 3>&) override {
        auto* c = take("can_see_position");
        arg_s(c, "spawn", role(&s));
        return ret(c) != 0;
    }
    bool can_see(cs::Spawn& s, cs::Spawn& other) override {
        auto* c = take("can_see");
        arg_s(c, "spawn", role(&s));
        arg_s(c, "other", role(&other));
        return ret(c) != 0;
    }
    bool spawn_virtual_dc(cs::Spawn& s) override {
        auto* c = take("spawn_virtual_dc");
        arg_s(c, "spawn", role(&s));
        return ret(c) != 0;
    }
    int32_t not_on_same_deity(cs::Spawn& s, cs::Spawn& caster, const cs::Spell& spell) override {
        auto* c = take("not_on_same_deity");
        arg_s(c, "spawn", role(&s));
        arg_s(c, "caster", role(&caster));
        arg_s(c, "spell", spell_role(&spell));
        return static_cast<int32_t>(ret(c));
    }
    int32_t cur_mana(int32_t a) override {
        auto* c = take("cur_mana");
        arg(c, "arg", a);
        return static_cast<int32_t>(ret(c));
    }
    int32_t cur_endurance(int32_t a) override {
        auto* c = take("cur_endurance");
        arg(c, "arg", a);
        return static_cast<int32_t>(ret(c));
    }
    bool spell_allowed(cs::Spawn* t, int32_t association) override {
        auto* c = take("spell_manager_virtual_08");
        arg_s(c, "spawn", role(t));
        arg(c, "value", association);
        return ret(c) != 0;
    }
    int32_t focus_range_modifier(const cs::Spell& spell) override {
        auto* c = take("focus_range_modifier");
        arg_s(c, "spell", spell_role(&spell));
        return static_cast<int32_t>(ret(c));
    }
    uint32_t player_distance(cs::Spawn& from, cs::Spawn& to, uint32_t extra) override {
        auto* c = take("player_distance");
        arg_s(c, "from", role(&from));
        arg_s(c, "to", role(&to));
        arg(c, "extra_bits", static_cast<int32_t>(extra));
        return g.distanceBits;
    }
    cs::Handle spawn_character_pc(cs::Spawn& s) override {
        auto* c = take("spawn_character_pc");
        arg_s(c, "spawn", role(&s));
        return named(c, "pc") ? 2 : 0;
    }
    bool target_type_12_check(cs::Spawn& s) override {
        auto* c = take("target_type_12_check");
        arg_s(c, "spawn", role(&s));
        return ret(c) != 0;
    }
    bool is_self_or_group_member(cs::Handle, const cs::Spawn* who) override {
        auto* c = take("is_self_or_group_member");
        arg_s(c, "who", who ? role(who) : "caster_character");
        return ret(c) != 0;
    }
    bool has_property(cs::Spawn& s, int32_t p, int32_t a2, int32_t a3) override {
        auto* c = take("has_property");
        arg_s(c, "spawn", role(&s));
        arg(c, "property", p);
        arg(c, "arg2", a2);
        arg(c, "arg3", a3);
        return ret(c) != 0;
    }
    int32_t item_base_type() override { return static_cast<int32_t>(ret(take("item_base_type"))); }
    int32_t item_spell_id(int32_t i) override { return indexed("item_spell_id", i); }
    int32_t largest_cached_effect(int32_t spa, int32_t sub) override { return spa_call("largest_cached_effect", spa, sub); }
    int32_t casting_time_modifier(const cs::Spell& spell) override {
        auto* c = take("casting_time_modifier");
        arg_s(c, "spell", spell_role(&spell));
        return static_cast<int32_t>(ret(c));
    }
    int32_t focus_casting_time_modifier(const cs::Spell& spell, int32_t a3) override {
        auto* c = take("focus_casting_time_modifier");
        arg_s(c, "spell", spell_role(&spell));
        arg(c, "arg3", a3);
        return static_cast<int32_t>(ret(c));
    }
    void start_casting(int32_t id, uint16_t spawn_id, uint32_t t) override {
        auto* c = take("start_casting");
        arg(c, "spell_id", id);
        arg(c, "spawn_id", spawn_id);
        arg(c, "cast_time", static_cast<int32_t>(t));
    }
    void launch_spell_data_reset() override { arg_s(take("launch_spell_data_reset"), "object", "caster_casting_data"); }
    int32_t item_id() override { return static_cast<int32_t>(ret(take("item_id"))); }
    int32_t item_spell_cast_time(int32_t i) override { return indexed("item_spell_cast_time", i); }
    int32_t item_spell_recast_type(int32_t i) override { return indexed("item_spell_recast_type", i); }
    void set_item_recast_timer(int32_t time, int32_t type) override {
        auto* c = take("set_item_recast_timer");
        arg(c, "time", time);
        arg(c, "type", type);
    }
    uint32_t spell_crc(int32_t id) override {
        auto* c = take("spell_crc");
        arg(c, "spell_id", id);
        return static_cast<uint32_t>(ret(c));
    }
    void hton(uint32_t opcode, int32_t a2) override {
        auto* c = take("hton");
        arg(c, "opcode", opcode);
        arg(c, "arg2", a2);
    }
    bool send(int32_t channel, const cs::Packet&, int32_t length) override {
        auto* c = take("send");
        arg(c, "channel", channel);
        arg(c, "length", length);
        return ret(c) != 0;
    }
    void operator_delete(int32_t p) override { arg(take("operator_delete"), "pointer", p); }
    void close_connection() override { take("close_connection"); }
    void debug_log(uint32_t opcode) override { arg(take("debug_log"), "opcode", opcode); }
    void push_interact_reason(int32_t r) override { arg(take("push_interact_reason"), "reason", r); }
    void push_pending_reason(int32_t r) override { arg(take("push_pending_reason"), "reason", r); }

private:
    const Json* take(const char* fn) {
        if (!error.empty()) return nullptr;
        if (next_ >= calls_.items.size()) {
            error = "call " + std::to_string(next_) + ": extra " + fn;
            return nullptr;
        }
        const Json& c = calls_[next_++];
        if (c["fn"].text != fn) {
            error = "call " + std::to_string(next_ - 1) + ": expected " + c["fn"].text + ", got " + fn;
            return nullptr;
        }
        return &c;
    }
    void arg(const Json* c, const char* k, int64_t v) {
        if (c && error.empty() && (*c)[k].i64() != v)
            error = "call " + std::to_string(next_ - 1) + " " + (*c)["fn"].text + ": " + k + " " +
                    std::to_string((*c)[k].i64()) + ", got " + std::to_string(v);
    }
    void arg_s(const Json* c, const char* k, const std::string& v) {
        if (c && error.empty() && (*c)[k].text != v)
            error = "call " + std::to_string(next_ - 1) + " " + (*c)["fn"].text + ": " + k + " " + (*c)[k].text +
                    ", got " + v;
    }
    static int64_t ret(const Json* c) { return c ? (*c)["ret"].i64() : 0; }
    static bool named(const Json* c, const char* v) { return c && (*c)["ret"].text == v; }
    int32_t spa_call(const char* fn, int32_t spa, int32_t sub) {
        auto* c = take(fn);
        arg(c, "spa", spa);
        arg(c, "arg2", sub);
        return static_cast<int32_t>(ret(c));
    }
    int32_t indexed(const char* fn, int32_t i) {
        auto* c = take(fn);
        arg(c, "index", i);
        return static_cast<int32_t>(ret(c));
    }
    const cs::Spell* spell_named(const Json* c, int32_t id) {
        if (!c) return nullptr;
        const std::string& r = (*c)["ret"].text;
        if (r == "spell") return &w_.spell;
        if (r == "rank_spell") return &w_.rank_spell;
        if (r == "casting_spell") return &w_.casting_spell;
        if (r == "buff_spell" && id >= 20000 && id < 20000 + kBuffs) return &w_.buff_spell[static_cast<std::size_t>(id - 20000)];
        return nullptr;
    }
    const cs::Item* item_named(const Json* c) {
        if (!c) return nullptr;
        const std::string& r = (*c)["ret"].text;
        if (r == "cast_item") return &w_.item;
        if (r == "other_item") return &w_.other_item;
        return nullptr;
    }
    cs::Spawn* spawn_named(const Json* c) {
        if (!c) return nullptr;
        const std::string& r = (*c)["ret"].text;
        if (r == "caster") return &w_.caster;
        if (r == "target") return &w_.target;
        if (r == "pet") return &w_.pet;
        if (r == "owner") return &w_.owner;
        if (r == "local_player") return &w_.other;
        return nullptr;
    }

    World& w_;
    const Json& calls_;
    bool stand_effect_;
    std::size_t next_ = 0;
};

// ---------------------------------------------------------------- the outputs

std::string check_outputs(const World& w, const Replay& r, const Json& out, uint8_t value,
                          const std::optional<cs::Packet>& sent) {
    if (out["value"].i64() != value) return "value " + std::to_string(out["value"].i64()) + ", got " + std::to_string(value);
    if (out["target"].text != r.role(w.st.target)) return "target " + out["target"].text + ", got " + r.role(w.st.target);
    // The caster fields written.
    std::map<std::string, int64_t> got;
    const cs::Spawn& c = w.caster;
    for (std::size_t k = 0; k < 13; ++k)
        if (c.gem_eta[k] != kSentinel) got["gem_eta_" + std::to_string(k)] = static_cast<int32_t>(c.gem_eta[k]);
    auto put = [&](const char* key, uint32_t v) {
        if (v != kSentinel) got[key] = static_cast<int32_t>(v);
    };
    put("cooldown_eta", c.cooldown_eta);
    put("casting_eta", c.casting.eta);
    if (c.casting.spell_id != g.castingSpellId) got["casting_spell_id"] = c.casting.spell_id;
    if (c.casting.slot != 0xee) got["casting_slot"] = c.casting.slot;
    put("casting_item_location_0", c.casting.item_location[0]);
    put("casting_item_location_1", c.casting.item_location[1]);
    put("casting_item_location_2", c.casting.item_location[2]);
    put("casting_item_cast_type", c.casting.item_cast_type);
    put("casting_item_id", c.casting.item_id);
    if (c.casting.y != kSentinel) got["casting_y"] = c.casting.y;
    if (c.casting.x != kSentinel) got["casting_x"] = c.casting.x;
    put("casting_target_id", c.casting.target_id);
    if (c.casting.free_target != 0xee) got["casting_free_target"] = c.casting.free_target;
    if (c.casting.target_position[0] != kSentinel) {
        got["casting_target_y"] = c.casting.target_position[0];
        got["casting_target_x"] = c.casting.target_position[1];
        got["casting_target_z"] = c.casting.target_position[2];
    }
    std::map<std::string, int64_t> want;
    for (const auto& [k, v] : out["caster_written"].fields) {
        bool is_float = k == "casting_y" || k == "casting_x" || k.rfind("casting_target_", 0) == 0;
        if (k == "casting_target_id") is_float = false;
        want[k] = is_float ? static_cast<int64_t>(hailstone::test::f32_bits(v)) : v.i64();
    }
    if (got != want) {
        std::string d = "caster fields:";
        for (const auto& [k, v] : want)
            if (!got.count(k) || got[k] != v) d += " " + k + "=" + std::to_string(v) + "/" + (got.count(k) ? std::to_string(got[k]) : "unset");
        for (const auto& [k, v] : got)
            if (!want.count(k)) d += " extra " + k + "=" + std::to_string(v);
        return d;
    }
    if (out.has("character_468") != w.st.character_468.has_value() ||
        (w.st.character_468 && out["character_468"].i64() != *w.st.character_468))
        return "character +0x468";
    if (out.has("last_beneficial") != w.st.last_beneficial.has_value() ||
        (w.st.last_beneficial && out["last_beneficial"].i64() != *w.st.last_beneficial))
        return "0xde0880";
    if (out["currently_interacting"].i64() != w.st.currently_interacting || out["item_pending"].i64() != w.st.item_pending ||
        out["packet_count_delta"].i64() != w.st.packet_count)
        return "request counters";
    const Json& p = out["packet"];
    if (p.is_null() != !sent.has_value()) return p.is_null() ? "a packet the original did not send" : "no packet";
    if (!sent) return "";
    if (p["slot"].i64() != sent->slot || p["spell_id"].i32() != sent->spell_id || p["target_id"].u32() != sent->target_id ||
        p["crc"].u32() != sent->crc || p["has_position"].i64() != sent->has_position ||
        p["position_set"].truthy() != sent->position_set)
        return "packet fields";
    for (std::size_t k = 0; k < 3; ++k)
        if (p["item_location"][k].u32() != sent->item_location[k]) return "packet item location";
    if (sent->position_set) {
        const char* keys[] = {"y", "x", "z"};
        for (std::size_t k = 0; k < 3; ++k)
            if (hailstone::test::f32_bits(p[keys[k]]) != sent->position[k]) return std::string("packet position ") + keys[k];
    }
    // The bytes the client sends for these fields: net::encode(CastSpell) against the record's payload.
    std::array<uint8_t, 0x2c> want_bytes{};
    auto put32 = [&](std::size_t at, uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) want_bytes[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put32(0, p["slot"].u32());
    put32(4, p["spell_id"].u32());
    for (std::size_t k = 0; k < 3; ++k) put32(8 + 4 * k, p["item_location"][k].u32());
    put32(0x14, p["target_id"].u32());
    put32(0x18, p["crc"].u32());
    want_bytes[0x1c] = static_cast<uint8_t>(p["has_position"].i64());
    if (p["position_set"].truthy()) {
        put32(0x20, hailstone::test::f32_bits(p["y"]));
        put32(0x24, hailstone::test::f32_bits(p["x"]));
        put32(0x28, hailstone::test::f32_bits(p["z"]));
    }
    hailstone::net::CastSpell n = hailstone::net::cast_spell_packet(*sent);
    hailstone::net::AppPacket ap = hailstone::net::encode(n);
    if (ap.opcode != 0x1287 || ap.data.size() != want_bytes.size()) return "encoded OP_CastSpell size";
    for (std::size_t i = 0; i < want_bytes.size(); ++i) {
        if (i >= 0x1d && i < 0x20) continue; // never written by the original
        if (i >= 0x20 && !p["position_set"].truthy()) continue;
        if (std::to_integer<uint8_t>(ap.data[i]) != want_bytes[i]) return "encoded OP_CastSpell byte +" + std::to_string(i);
    }
    return "";
}

std::string run_record(const Json& rec) {
    World w;
    build(w);
    if (std::string why = check_inputs(rec["in"]); !why.empty()) return why;
    const Json& out = rec["out"];
    const FpuPrecision precision = rec["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53;
    build(w);
    w.st.precision = precision;
    Replay r(w, out["calls"], g.standTakesEffect);
    std::optional<cs::Packet> sent;
    uint8_t value = cs::cast_spell(w.st, w.args, r, &sent);
    if (!r.error.empty()) return r.error;
    if (r.used() != out["calls"].items.size())
        return "missing call " + std::to_string(r.used()) + ": " + out["calls"][r.used()]["fn"].text;
    return check_outputs(w, r, out, value, sent);
}

void check_file(const char* file, uint32_t focus, uint32_t seed) {
    Rng rng(seed);
    hailstone::test::each_vector(file, [&](const Json& rec) {
        Randomize(rng, focus);
        return run_record(rec);
    });
}

} // namespace

TEST_CASE("CastSpell_Checks: the early checks") { check_file("CastSpell_Checks.jsonl", kChecks, 0xca51); }
TEST_CASE("CastSpell_Buffs: Sanctuary, invisibility and sneak attacks") { check_file("CastSpell_Buffs.jsonl", kSanctuary, 0xca52); }
TEST_CASE("CastSpell_Items: item casts, songs, level, SPA 25 / 32") { check_file("CastSpell_Items.jsonl", kItems, 0xca53); }
TEST_CASE("CastSpell_Target: retargets, line of sight, mount, deity, bards, mana") { check_file("CastSpell_Target.jsonl", kTarget, 0xca54); }
TEST_CASE("CastSpell_Range: the target-type switch and its ranges") { check_file("CastSpell_Range.jsonl", kRange, 0xca55); }
TEST_CASE("CastSpell_BodyType: body types, pets, corpses, group, self") { check_file("CastSpell_BodyType.jsonl", kBody, 0xca56); }
TEST_CASE("CastSpell_Send: cast time, CastingData, timers and OP_CastSpell") { check_file("CastSpell_Send.jsonl", kSend, 0xca57); }
