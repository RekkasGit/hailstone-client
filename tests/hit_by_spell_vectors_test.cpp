// EQ_Character::HitBySpell (game/mechanics/hit_by_spell.h) against the
// original's vectors (rof2decomp batch 249): HitBySpell_Validation (300),
// _Instant (400), _Items (300), _Moves (300) and _Buff (500).
//
// Each record holds the inputs, every mocked callee the original called (in
// order, with its arguments and answer) and the fake virtuals' tape (the
// spell manager, the CharacterBase, spawns, EQGraphicsDX9's scene, collision
// and sine table), then the state after the call. The fakes' answers that the
// record does not hold (the CharacterBase's level / mana / PcClient, the floor
// heights, the sine-table values, the collision answers, DoSpellCheck's
// record bytes) come from the generator's world: rof2decomp's MakeWorld
// (re/test/tests/hit_by_spell_test_eh.cpp) is ported below with its random
// generator, and every regenerated input is first compared with the record's
// "in". The port then runs against a replay: each callee it calls must be the
// record's next call with the same arguments (byte arguments the original
// pushes as dwords compare by their low byte; a pointer the record names
// "world" is any object of the world), and gets the recorded answer; each
// virtual must be the tape's next entry. Finally every recorded output is
// compared, floats bit for bit.
#include <doctest/doctest.h>

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <system_error>
#include <string>
#include <vector>

#include "game/mechanics/hit_by_spell.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using namespace hailstone::game::mechanics::hit;
using hailstone::test::Json;

namespace {

// ---------------------------------------------------------------- the generator (rof2decomp test harness)

struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed ? seed : 0x9e3779b9u) {}
    std::uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int Range(int lo, int hi) {
        std::uint32_t span = static_cast<std::uint32_t>(hi) - static_cast<std::uint32_t>(lo) + 1u;
        return static_cast<int>(static_cast<std::uint32_t>(lo) + Next() % span);
    }
    bool Chance(int percent) { return static_cast<int>(Next() % 100) < percent; }
    int Interesting() {
        static const int edges[] = {0, 1, -1, 2, 9, 10, 11, 59, 60, 61, 99, 100, 255, 256, 330, 1000, 0x7fffffff,
                                    static_cast<int>(0x80000000), 10000000, 9999999};
        std::uint32_t pick = Next() % 4;
        if (pick == 0) return edges[Next() % (sizeof(edges) / sizeof(edges[0]))];
        if (pick == 3) return static_cast<int>(Next());
        return Range(-5, 400);
    }
};

std::uint32_t fbits(float f) { return std::bit_cast<std::uint32_t>(f); }

std::uint32_t random_float(Rng& rng) {
    switch (rng.Range(0, 13)) {
    case 0: return 0;
    case 1: return 0x80000000;
    case 2: return 0x7fc00000 | (rng.Next() & 0x3fffff);
    case 3: return 0x7f800000 | (rng.Next() & 0x3fffff) | 1;
    case 4: return rng.Chance(50) ? 0x7f800000 : 0xff800000;
    case 5: return rng.Next() & 0x807fffff;
    case 6: return 0x3f800000;
    case 7: return rng.Next();
    default: return fbits(static_cast<float>(static_cast<std::int32_t>(rng.Next() % 4000001) - 2000000) / 997.0f);
    }
}
std::uint32_t plain_float(Rng& rng) {
    if (rng.Chance(8)) return random_float(rng);
    return fbits(static_cast<float>(static_cast<std::int32_t>(rng.Next() % 200001) - 100000) / 61.0f);
}
std::int32_t value(Rng& rng) {
    switch (rng.Range(0, 5)) {
    case 0: return rng.Interesting();
    case 1: return -rng.Range(0, 300);
    case 2: return rng.Range(0, 300);
    default: return rng.Range(-20, 20);
    }
}

constexpr std::int32_t kLoop1Handled[] = {15, 20, 25, 35, 36, 38, 42, 56, 61, 62, 68, 72, 74, 83, 91, 116, 145, 160,
                                          189, 338, 343, 358, 369, 379, 380, 381, 388, 389, 390, 417};
constexpr std::int32_t kInstantSpas[] = {160, 20, 68, 25, 72, 189, 74, 42, 56, 15, 358, 417, 343, 91, 338, 388};
constexpr std::int32_t kItemSpas[] = {61, 62, 38, 35, 36, 116, 369, 389, 390};
constexpr std::int32_t kMoveSpas[] = {83, 145, 379, 380, 381};

bool loop1_handled(std::int32_t spa) {
    for (std::int32_t h : kLoop1Handled)
        if (h == spa) return true;
    return false;
}
std::int32_t unhandled(Rng& rng) {
    for (;;) {
        std::int32_t spa = rng.Chance(3) ? static_cast<std::int32_t>(rng.Next()) : rng.Range(-3, 470);
        if (!loop1_handled(spa)) return spa;
    }
}

struct Family {
    const std::int32_t* spas;
    int count;
    bool buff;
    bool early;
};
const Family kEarly = {nullptr, 0, false, true};
const Family kInstant = {kInstantSpas, 16, false, false};
const Family kItems = {kItemSpas, 9, false, false};
const Family kMoves = {kMoveSpas, 5, false, false};
const Family kBuff = {nullptr, 0, true, false};

std::int32_t sweep_spa(const Family& f, std::uint32_t sweep) {
    if (f.buff) return static_cast<std::int32_t>(sweep % 475) - 3;
    std::uint32_t pool = static_cast<std::uint32_t>(f.count) + 476;
    std::uint32_t n = sweep % pool;
    if (n < static_cast<std::uint32_t>(f.count)) return f.spas[n];
    std::int32_t spa = static_cast<std::int32_t>(n - static_cast<std::uint32_t>(f.count)) - 3;
    return loop1_handled(spa) ? 254 : spa;
}
std::int32_t pick_spa(Rng& rng, const Family& f) {
    if (f.buff) return rng.Chance(5) ? static_cast<std::int32_t>(rng.Next()) : rng.Range(-3, 470);
    if (f.count != 0 && rng.Chance(60)) return f.spas[rng.Range(0, f.count - 1)];
    return unhandled(rng);
}

void set_text(char* to, const char* text) { std::strcpy(to, text); }

void make_spell(Rng& rng, Spell& s, const Family& f, std::uint32_t sweep, bool main) {
    s.range = random_float(rng);
    s.push_back = rng.Chance(70) ? 0 : (rng.Chance(20) ? 0x80000000 : random_float(rng));
    s.push_up = rng.Chance(70) ? 0 : random_float(rng);
    if (main) {
        if (f.buff) s.duration_formula = static_cast<std::uint32_t>(rng.Range(1, 9));
        else s.duration_formula = (f.early && rng.Chance(30)) ? static_cast<std::uint32_t>(rng.Range(1, 3)) : 0;
    } else {
        s.duration_formula = static_cast<std::uint32_t>(rng.Range(0, 2));
    }
    int used = rng.Chance(30) ? 12 : rng.Range(1, 12);
    for (int i = 0; i < 12; ++i) {
        std::int32_t spa = i == 0 && main ? sweep_spa(f, sweep) : pick_spa(rng, f);
        if (i >= used) spa = rng.Chance(80) ? 254 : spa;
        auto k = static_cast<std::size_t>(i);
        s.attrib[k] = spa;
        s.base[k] = rng.Chance(10) ? 0 : value(rng);
        s.base2[k] = value(rng);
        s.max[k] = value(rng);
    }
    s.recourse_178 = rng.Chance(60) ? 0 : static_cast<std::uint32_t>(rng.Range(1, 40));
    s.timer_id_198 = rng.Chance(70) ? static_cast<std::uint32_t>(rng.Range(0, 0x15)) : rng.Next();
    s.flag_240 = rng.Chance(80) ? 0 : 1;
    s.spell_type = static_cast<std::uint8_t>(rng.Chance(80) ? rng.Range(0, 1) : rng.Range(2, 255));
    s.byte_26c = static_cast<std::uint8_t>(rng.Next());
    s.target_type = static_cast<std::uint8_t>(rng.Chance(30) ? 0xd : (rng.Chance(30) ? 0x14 : rng.Range(0, 30)));
    const char* names[] = {"Spirit of Wolf", "Gate", "X"};
    set_text(s.name.data(), names[rng.Range(0, 2)]);
    if (rng.Chance(70)) set_text(s.teleport_zone.data(), rng.Chance(50) ? "qeynos" : "freportn");
}

constexpr std::uint32_t kIds[] = {0x3d6, 0x29a, 0x2a2, 0x628, 0xe6e, 0xad7, 0x57, 0x58, 0xad6, 0x2e6, 0x5c4};

enum Knob {
    kCasterPtr, kMyChar, kTheirChar, kBard, kStunned, kItemCast, kGroup, kSpellCheck, kMaxEffects, kIsPcMe,
    kIsPcCaster, kPvpDuration, kDuration, kCasterLevel, kCbLevel, kCbMana, kPc, kMeDC, kCasterDC, kInWorld,
    kActorOn, kTerrainOn, kLineHits, kTeleportOk, kFloorNone, kSlot, kAffectPtr, kRoll, kHasProperty, kGet150,
    kBindSpot, kExpansion, kZoneIndex, kHton, kSent, kItem, kItemType, kBookLang, kMagic, kPcAffect, kTime, kGemSeed,
    kFocus, kRandom100, kLowHigh, kRefused, kTotal, kChangeForm, kGeneric, kHasFlag, kVehicle, kMount, kWriteNormal,
    kThrowAt, kLoreText, kKnobs
};

// Everything the record's objects are, in one block (a pointer inside it that
// the record does not name is "world").
struct World {
    Character self;
    CharacterBase cb;
    Spawn me, caster, target, mount, vehicle;
    Character caster_char;
    // me's +0x2cc is the PcClient ("pc"); read as a CharacterZoneClient its +0x1a4 is 0 and its +8 (the
    // vbtable pointer) leads to zero bytes: a spawn of type 0.
    Character me_char;
    Spawn pc_as_spawn;
    Pc pc, caster_pc;
    char pc_name[64]{};
    std::array<std::int32_t, 12> timers{};
    Profile profile;
    std::int32_t drunkenness = 0; // profile +0x3300, for the PcBase mocks
    std::array<Spell, 8> spells;
    std::array<std::uint32_t, 8> spell_ids{};
    Hit hit;
    Launch launch;
    Affect affect;
    std::array<Affect, 10> buffs;
    char texts[4][0x30]{};
    std::array<std::uint32_t, kKnobs> k{};
    std::array<std::int32_t, 16> calc{}, cached{};
    std::uint32_t floors[4][2]{};
    std::uint32_t trig[2]{};
    std::uint32_t normal[3]{};
    Globals globals;
    bool has_me = true;
    bool caster_none = false, caster_me = false;
    std::uint32_t dd00e4 = 0;
    // Run state.
    std::uint32_t floor_calls = 0, line_calls = 0, teleport_calls = 0;
    const Launch* launch_used = nullptr;
    std::int32_t duration = -1;
};

void make_world(World& w, Rng& rng, const Family& f, std::uint32_t sweep) {
    w = World{};
    w.self.base = &w.cb;
    w.has_me = !(f.early && rng.Chance(2));
    w.self.spawn = w.has_me ? &w.me : nullptr;
    w.self.cache_valid = static_cast<std::uint8_t>(rng.Range(0, 1));
    w.self.aa_cache_valid = static_cast<std::uint8_t>(rng.Range(0, 2));
    w.self.stats_dirty = static_cast<std::uint8_t>(rng.Range(0, 2));
    w.cb.zone_id = rng.Range(1, 4);
    w.cb.y = plain_float(rng);
    w.cb.x = plain_float(rng);
    w.cb.z = plain_float(rng);
    w.cb.heading = plain_float(rng);
    w.cb.mesmerized = static_cast<std::uint8_t>(rng.Range(0, 2));
    for (Spawn* p : {&w.me, &w.caster}) {
        p->type = static_cast<std::uint8_t>(rng.Chance(75) ? 0 : rng.Range(1, 3));
        p->flag_208 = static_cast<std::uint8_t>(rng.Chance(70) ? 0 : 1);
        p->owner_38c = rng.Chance(70) ? 0 : static_cast<std::uint32_t>(rng.Range(1, 3));
        p->spawn_id = static_cast<std::uint32_t>(rng.Range(1, 3));
        std::uint32_t* fields[] = {&p->y, &p->x, &p->z, &p->speed_y, &p->speed_x, &p->speed_z, &p->rotation_speed,
                                   &p->heading};
        for (std::uint32_t* v : fields) *v = plain_float(rng);
        p->heading_speed = plain_float(rng);
        p->floor_z = plain_float(rng);
        p->model_height = plain_float(rng);
        p->casting_spell = rng.Chance(30) ? -1 : rng.Range(0, 12);
        p->stand_state = static_cast<std::uint8_t>(rng.Chance(50) ? 0x64 : rng.Range(0, 0x70));
        p->class_id = static_cast<std::uint8_t>(rng.Chance(20) ? 8 : rng.Range(1, 16));
        rng.Range(1, 100); // +0x250
    }
    w.caster.character = rng.Chance(75) ? &w.caster_char : nullptr;
    w.me.character = rng.Chance(75) ? &w.me_char : nullptr;
    w.me_char.spawn = &w.pc_as_spawn;
    w.caster_char.spawn = &w.caster;
    w.caster_char.cache_valid = static_cast<std::uint8_t>(rng.Range(0, 1));
    w.pc.gem_timers = rng.Chance(80) ? w.timers.data() : nullptr;
    w.pc.id_20a8 = static_cast<std::int32_t>(rng.Next());
    w.profile.class_id = static_cast<std::uint8_t>(rng.Chance(60) ? rng.Range(1, 16) : (rng.Chance(50) ? 8 : 7));
    w.profile.weight = rng.Chance(10) ? rng.Interesting() : rng.Range(0, 400);
    w.profile.mana = value(rng);
    w.profile.dword_3248 = static_cast<std::int32_t>(rng.Next());
    w.profile.dword_3264 = static_cast<std::int32_t>(rng.Next());
    w.drunkenness = rng.Range(0, 250);
    std::uint32_t main_id = rng.Chance(40) ? kIds[rng.Range(0, 10)] : static_cast<std::uint32_t>(rng.Range(100, 300));
    if (f.early && rng.Chance(3)) main_id = rng.Chance(50) ? 0 : 0xafc9;
    for (int s = 0; s < 8; ++s) {
        auto k = static_cast<std::size_t>(s);
        w.spell_ids[k] = s == 0 ? main_id : (s == 1 ? 0x11a8 : static_cast<std::uint32_t>(rng.Range(1, 60)));
        make_spell(rng, w.spells[k], f, sweep, s == 0);
    }
    if (f.early && rng.Chance(3)) w.spell_ids[0] = main_id + 1;
    w.hit.caster_id = static_cast<std::uint16_t>(rng.Range(1, 9));
    w.hit.item_level = static_cast<std::uint16_t>(rng.Chance(60) ? 0 : rng.Range(1, 300));
    w.hit.modifier = rng.Chance(40) ? 0x3f800000
                                    : (rng.Chance(30) ? fbits(static_cast<float>(rng.Range(1, 40)) / 8.0f)
                                                      : random_float(rng));
    w.hit.push_heading = rng.Chance(60) ? plain_float(rng) : random_float(rng);
    w.hit.word_1b = static_cast<std::uint16_t>(rng.Chance(50) ? 0 : w.spell_ids[static_cast<std::size_t>(rng.Range(0, 7))]);
    w.hit.song_value = value(rng);
    w.hit.spell_id = main_id;
    w.hit.caster_level = static_cast<std::uint8_t>(rng.Chance(20) ? rng.Range(200, 255) : rng.Range(1, 100));
    w.launch.byte_32 = static_cast<std::uint8_t>(rng.Next());
    w.launch.item_flag = static_cast<std::uint8_t>(rng.Range(0, 1));
    for (Affect& b : w.buffs) {
        b.type = static_cast<std::uint8_t>(rng.Chance(70) ? rng.Range(1, 4) : 0);
        b.level = static_cast<std::uint8_t>(rng.Range(1, 110));
        b.spell_id = static_cast<std::int32_t>(rng.Chance(80) ? w.spell_ids[static_cast<std::size_t>(rng.Range(0, 7))]
                                                              : rng.Next() % 100);
        for (auto& v : b.slot_data) v = rng.Range(-3, 10);
        b.flags_24 = rng.Next();
    }
    {
        std::uint32_t d[24];
        for (auto& v : d) v = rng.Next();
        Affect& a = w.affect;
        a.type = static_cast<std::uint8_t>(d[0]);
        a.level = static_cast<std::uint8_t>(d[0] >> 8);
        a.byte_2 = static_cast<std::uint8_t>(d[0] >> 16);
        a.byte_3 = static_cast<std::uint8_t>(d[0] >> 24);
        a.modifier = d[1];
        a.spell_id = static_cast<std::int32_t>(d[2]);
        a.duration = static_cast<std::int32_t>(d[3]);
        a.caster_10 = static_cast<std::int32_t>(d[4]);
        a.dword_14 = static_cast<std::int32_t>(d[5]);
        a.y_18 = d[6];
        a.x_1c = d[7];
        a.z_20 = d[8];
        a.flags_24 = d[9];
        for (int j = 0; j < 12; ++j) a.slot_data[static_cast<std::size_t>(j)] = static_cast<std::int32_t>(d[10 + j]);
    }
    w.target.type = static_cast<std::uint8_t>(rng.Range(0, 3));
    w.target.owner_38c = static_cast<std::uint32_t>(rng.Range(1, 3));
    w.target.spawn_id = rng.Next();
    set_text(w.target.name.data(), "Target");
    for (int off = 0; off < 8; ++off) {
        std::uint32_t m = plain_float(rng), v = plain_float(rng);
        auto field = [&](Spawn& s) -> std::uint32_t* {
            switch (off) {
            case 0: return &s.speed_y;
            case 1: return &s.speed_x;
            case 2: return &s.speed_z;
            case 3: return &s.rotation_speed;
            case 4: return &s.heading;
            case 7: return &s.heading_speed;
            default: return nullptr; // +0x84, +0x88
            }
        };
        if (auto* p = field(w.mount)) *p = m;
        if (auto* p = field(w.vehicle)) *p = v;
    }
    const char* texts[] = {"~*Lore Book", "#Book", "Plain Book", "Elvish"};
    for (int t = 0; t < 4; ++t) set_text(w.texts[t], texts[(t + rng.Range(0, 3)) & 3]);
    auto& k = w.k;
    if (f.early && rng.Chance(3)) {
        w.caster_none = true;
    } else {
        w.caster_me = rng.Chance(30);
    }
    k[kTheirChar] = rng.Chance(40) ? 0 : 1; // 0: pc, 1: pc + 0x100 (the caster's)
    k[kBard] = rng.Chance(30) ? 1 : 0;
    k[kStunned] = f.early && rng.Chance(10) ? 1 : 0;
    k[kItemCast] = rng.Chance(30);
    k[kGroup] = rng.Chance(70);
    {
        std::uint32_t a = rng.Next() & ~1u;
        k[kSpellCheck] = a | (rng.Chance(95) ? 1u : 0u);
    }
    k[kMaxEffects] = static_cast<std::uint32_t>(rng.Range(0, 10));
    k[kIsPcMe] = rng.Chance(50);
    k[kIsPcCaster] = rng.Chance(50);
    k[kPvpDuration] = static_cast<std::uint32_t>(value(rng));
    k[kDuration] = static_cast<std::uint32_t>(value(rng));
    k[kCasterLevel] = static_cast<std::uint32_t>(rng.Chance(50) ? rng.Range(30, 60) : rng.Range(0, 255));
    k[kCbLevel] = static_cast<std::uint32_t>(rng.Range(30, 60));
    k[kCbMana] = static_cast<std::uint32_t>(value(rng));
    k[kPc] = rng.Chance(85) ? 1 : 0;
    k[kMeDC] = rng.Chance(50);
    k[kCasterDC] = rng.Chance(50);
    k[kInWorld] = rng.Chance(85);
    {
        std::uint32_t a = static_cast<std::uint32_t>(rng.Range(0, 1));
        k[kActorOn] = a | (rng.Next() & 0xffffff00u);
    }
    k[kTerrainOn] = rng.Chance(60);
    k[kLineHits] = rng.Next();
    k[kTeleportOk] = rng.Next();
    k[kSlot] = static_cast<std::uint32_t>(rng.Range(0, 60));
    k[kAffectPtr] = rng.Chance(90) ? 1 : 0;
    k[kRoll] = static_cast<std::uint32_t>(rng.Chance(40) ? (rng.Chance(50) ? rng.Range(94, 96) : rng.Range(4, 6))
                                                         : rng.Range(1, 100));
    k[kHasProperty] = rng.Chance(30);
    k[kGet150] = rng.Chance(15);
    k[kBindSpot] = rng.Chance(60);
    k[kExpansion] = rng.Chance(60);
    k[kZoneIndex] = static_cast<std::uint32_t>(rng.Range(1, 4));
    k[kHton] = rng.Next();
    k[kSent] = rng.Chance(80);
    k[kItem] = rng.Chance(80) ? 0x1234500u + static_cast<std::uint32_t>(rng.Range(0, 3)) : 0;
    k[kItemType] = static_cast<std::uint32_t>(rng.Range(0, 2));
    k[kBookLang] = static_cast<std::uint32_t>(rng.Range(0, 5));
    k[kMagic] = rng.Chance(50);
    k[kPcAffect] = rng.Next();
    k[kTime] = rng.Next();
    k[kGemSeed] = rng.Next();
    k[kFocus] = rng.Next();
    k[kRandom100] = static_cast<std::uint32_t>(rng.Range(0, 100));
    k[kLowHigh] = static_cast<std::uint32_t>(rng.Chance(40) ? rng.Range(9, 11) : rng.Range(1, 100));
    k[kRefused] = static_cast<std::uint32_t>(rng.Chance(60) ? 0 : rng.Range(1, 2));
    k[kTotal] = static_cast<std::uint32_t>(rng.Chance(50) ? 0 : value(rng));
    k[kChangeForm] = rng.Chance(85);
    k[kGeneric] = static_cast<std::uint32_t>(value(rng));
    k[kHasFlag] = rng.Chance(50);
    k[kVehicle] = rng.Chance(30);
    k[kMount] = rng.Chance(40);
    k[kWriteNormal] = rng.Chance(60);
    k[kThrowAt] = 0;
    k[kLoreText] = static_cast<std::uint32_t>(rng.Range(0, 3));
    for (int j = 0; j < 16; ++j) {
        auto i = static_cast<std::size_t>(j);
        w.calc[i] = rng.Chance(35) ? rng.Range(-3, 9) : value(rng);
        w.cached[i] = rng.Chance(70) ? 0 : value(rng);
    }
    for (auto& fl : w.floors) {
        if (rng.Chance(25)) {
            fl[0] = 0xec4ecb8f;
            fl[1] = 0x3f800000;
        } else {
            fl[0] = plain_float(rng);
            fl[1] = rng.Chance(50) ? 0x3f800001 : 0x3f800000;
        }
    }
    w.trig[0] = rng.Chance(80) ? fbits(static_cast<float>(rng.Range(-1000, 1000)) / 1000.0f) : random_float(rng);
    w.trig[1] = rng.Chance(50) ? 0x3f800001 : 0x3f800000;
    w.normal[0] = rng.Chance(20) ? (0x7f800001 | (rng.Next() & 0x3ffffe)) : plain_float(rng);
    w.normal[1] = plain_float(rng);
    w.normal[2] = plain_float(rng);
    Globals& g = w.globals;
    g.target = rng.Chance(80) ? &w.target : nullptr;
    Spawn* players[] = {&w.me, &w.caster, &w.target, nullptr};
    g.local_player = players[rng.Range(0, 3)];
    g.controlled_player = players[rng.Range(0, 3)];
    g.connected = rng.Chance(70);
    g.zone_type = static_cast<std::uint8_t>(rng.Range(0, 5));
    g.ruleset = static_cast<std::uint32_t>(rng.Chance(50) ? 0 : rng.Range(1, 7));
    g.bind_blocked = static_cast<std::uint8_t>(rng.Chance(85) ? 0 : 1);
    g.no_send_log = static_cast<std::uint8_t>(rng.Range(0, 1));
    std::uint32_t pushed = rng.Next() & 0xfefefefe;
    g.controlled_pushed = static_cast<std::uint8_t>(pushed);
    g.local_pushed = static_cast<std::uint8_t>(pushed >> 8);
    g.save_reason = static_cast<std::int32_t>(rng.Next());
    std::uint32_t dd00e0 = rng.Next();
    w.dd00e4 = rng.Next();
    g.packet_opcode = static_cast<std::uint16_t>(dd00e0);
    g.packet_target = static_cast<std::int32_t>((dd00e0 >> 16) | (w.dd00e4 << 16));
    g.send_count = static_cast<std::int32_t>(rng.Next());
}

void early_tweak(World& w, Rng& rng) {
    if (rng.Chance(30)) {
        w.spells[0].attrib[static_cast<std::size_t>(rng.Range(0, 11))] = 123;
        w.spells[0].spell_type = 0;
        if (rng.Chance(60))
            for (int j = 0; j < 16; ++j) {
                w.cached[static_cast<std::size_t>(j)] = 0;
                w.calc[static_cast<std::size_t>(j)] = rng.Range(-1, 1);
            }
    }
}
void buff_tweak(World& w, Rng& rng) {
    static constexpr std::int32_t kBiased[] = {0x3a, 0x9c, 0x1e, 0x11, 0x12, 0x56, 35, 36, 116, 369, 3, 156};
    for (int i = 1; i < 12; ++i)
        if (rng.Chance(25)) w.spells[0].attrib[static_cast<std::size_t>(i)] = kBiased[rng.Range(0, 11)];
    if (rng.Chance(15)) {
        static constexpr std::int32_t kUnscaled[] = {0x3a, 0x9c, 0x1e, 0x11, 0x12, 0x56, 254};
        for (int i = 1; i < 12; ++i) w.spells[0].attrib[static_cast<std::size_t>(i)] = kUnscaled[rng.Range(0, 6)];
        static constexpr std::int32_t kFirst[] = {0x56, 0x12, 3, 0x9c, 200};
        w.spells[0].attrib[0] = kFirst[rng.Range(0, 4)];
    }
    if (rng.Chance(30)) {
        static constexpr std::int32_t kPairs[] = {-2, -1, 8, 9, 7, 6};
        for (int j = 0; j < 16; ++j) w.calc[static_cast<std::size_t>(j)] = kPairs[rng.Range(0, 5)];
    }
    if (rng.Chance(30)) w.hit.modifier = fbits(static_cast<float>(rng.Range(2, 40)) / 8.0f);
}
void moves_tweak(World& w, Rng& rng) {
    for (int i = 0; i < 12; ++i)
        if (w.spells[0].attrib[static_cast<std::size_t>(i)] == 379)
            w.spells[0].base[static_cast<std::size_t>(i)] =
                rng.Chance(10) ? -rng.Range(0, 0x7fffffff) : rng.Range(-2, 16);
    w.k[kThrowAt] = rng.Chance(6) ? static_cast<std::uint32_t>(rng.Range(1, 6)) : 0;
}

// ---------------------------------------------------------------- names

constexpr std::uint32_t kExeBase = 0x1000000; // where the recording loaded eqgame.exe
std::uint32_t unrebase(std::uint32_t v) {
    return v >= kExeBase && v < kExeBase + 0x1200000 ? v - kExeBase + 0x400000 : v;
}

// A value as the record writes it: the name of the object it points at, or the integer.
struct Named {
    std::optional<std::string> name;
    std::uint32_t value = 0;
};

class Names {
public:
    explicit Names(World& w) : w_(w) {
        add(&w.me, "me");
        add(&w.caster, "caster");
        for (int k = 0; k < 8; ++k) add(&w.spells[static_cast<std::size_t>(k)], "spell" + std::to_string(k));
        for (int k = 0; k < 10; ++k) add(&w.buffs[static_cast<std::size_t>(k)], "buff" + std::to_string(k));
        add(&w.affect, "affect");
        add(&w.pc, "pc");
        add(&w.me_char, "pc");
        add(&w.caster_pc, "caster_pc");
        by_value_[reinterpret_cast<std::uintptr_t>(&w.pc) + kPcCharacter] = "pc_character";
        by_value_[reinterpret_cast<std::uintptr_t>(&w.caster_pc) + kPcCharacter] = "caster_pc_character";
        add(&w.target, "target");
        add(&w.self, "self");
        add(&w.cb, "cb");
        add(&w.caster_char, "caster_character");
        add(&w.vehicle, "vehicle");
        add(&w.mount, "mount");
    }
    Named of(std::uintptr_t v) const {
        if (auto it = by_value_.find(v); it != by_value_.end()) return {it->second, 0};
        auto lo = reinterpret_cast<std::uintptr_t>(&w_);
        if (v >= lo && v < lo + sizeof(World)) return {std::string("world"), 0};
        return {std::nullopt, unrebase(static_cast<std::uint32_t>(v))};
    }
    bool in_world(const void* p) const {
        auto v = reinterpret_cast<std::uintptr_t>(p), lo = reinterpret_cast<std::uintptr_t>(&w_);
        return v >= lo && v < lo + sizeof(World);
    }
    std::uintptr_t object(const std::string& name) const {
        if (name == "pc") return reinterpret_cast<std::uintptr_t>(&w_.pc);
        for (const auto& [v, n] : by_value_)
            if (n == name) return v;
        return 0;
    }

private:
    template <typename T> void add(T* p, const std::string& n) { by_value_[reinterpret_cast<std::uintptr_t>(p)] = n; }
    World& w_;
    std::map<std::uintptr_t, std::string> by_value_;
};

// ---------------------------------------------------------------- the replay

struct Arg {
    enum Kind { Int, Byte, Ptr, Text, Skip } kind = Skip;
    std::uint32_t v = 0;
    std::uintptr_t p = 0;
    const char* t = nullptr;
};
Arg I(std::int64_t v) { return {Arg::Int, static_cast<std::uint32_t>(v), 0, nullptr}; }
Arg B(std::uint32_t v) { return {Arg::Byte, v, 0, nullptr}; }
template <typename T> Arg P(T* p) { return {Arg::Ptr, 0, reinterpret_cast<std::uintptr_t>(p), nullptr}; }
Arg H(Handle h) { return {Arg::Ptr, 0, h, nullptr}; }
Arg T(const char* t) { return {Arg::Text, 0, 0, t}; }
Arg S() { return {}; }

class Replay final : public HitQueries {
public:
    Replay(World& w, const Json& out, const Fpu& fpu) : w_(w), names_(w), fpu_(fpu) {
        for (const Json& c : out["calls"].items) {
            const std::string& fn = c["fn"].text;
            if (fn == "HashTableInt ctor" || fn == "HashTableInt dtor" || fn == "operator_delete" ||
                fn == "collision info dtor")
                continue;
            calls_.push_back(&c);
        }
        for (const Json& v : out["virtuals"].items) tape_.push_back(&v);
    }
    std::string error;
    HitFrame frame;
    std::string finish() {
        // Texts in the frame are what the frame holds at the end (the recording reads them then).
        for (const auto& [rec, ptr] : deferred_)
            if (error.empty() && *rec != ptr) error = "frame text " + std::string(ptr) + " recorded " + *rec;
        if (!error.empty()) return error;
        if (call_ < calls_.size()) return "call " + (*calls_[call_])["fn"].text + " not made";
        if (tape_at_ < tape_.size()) return "virtual " + (*tape_[tape_at_])[0].text + " not made";
        return "";
    }
    const Names& names() const { return names_; }

    // ---- virtuals
    const Spell* spell_by_id(std::uint32_t id) override {
        tape("SpellManager::GetSpellByID(id)", {id});
        for (std::size_t k = 0; k < 8; ++k)
            if (w_.spell_ids[k] == id) return &w_.spells[k];
        return nullptr;
    }
    Handle cb_pc() override {
        tape("CharacterBase::vf4c (PcClient)", {});
        return w_.k[kPc] ? reinterpret_cast<Handle>(&w_.pc) : 0;
    }
    std::uint32_t cb_level() override {
        tape("CharacterBase::vf68 (level)", {});
        return w_.k[kCbLevel];
    }
    std::uint32_t cb_max_mana() override {
        tape("CharacterBase::vf6c (max mana)", {});
        return w_.k[kCbMana];
    }
    std::uint8_t spawn_vf_dc(Spawn* s) override {
        bool me = s == &w_.me;
        tape("spawn vfdc (is_me)", {me ? 1u : 0u});
        return static_cast<std::uint8_t>(me ? w_.k[kMeDC] : w_.k[kCasterDC]);
    }
    void pc_base_vf58(Handle base) override {
        tape("PcClient base vf58", {base == reinterpret_cast<Handle>(&w_.pc) + kPcBase ? 1u : 0u});
    }
    std::uint8_t point_in_world(const std::array<std::uint32_t, 3>& p) override {
        tape("pinstSceneGraph vf4c IsPointInWorld(x,y,z)", {p[0], p[1], p[2]});
        return static_cast<std::uint8_t>(w_.k[kInWorld]);
    }
    std::uint8_t actor_collision_enabled() override {
        tape("pinstCollision vf48 IsActorCollisionEnabled", {});
        return static_cast<std::uint8_t>(w_.k[kActorOn]);
    }
    void set_actor_collision(std::uint8_t flag) override {
        tape("pinstCollision vf50 SetActorCollisionEnabled(flag)", {flag}, {0xff});
    }
    std::uint8_t collide_sphere(CollisionSphere& s) override {
        const CollisionInfo& i = s.info;
        tape("pinstCollision vf08 CollideSphere: vtable, from x,y,z, to x", {i.vtable, i.from[0], i.from[1], i.from[2], i.to[0]});
        tape("  sphere: to y,z, radius, r^2, 1/r", {i.to[1], i.to[2], s.radius, s.radius_sq, s.inverse_radius});
        tape("  sphere: filter, callback, +0x94, +0x98, +0x9c", {s.filter, s.callback, s.dword_94, s.dword_98, s.byte_9c});
        tape("  sphere before the test: normal x,y,z (stale frame bytes), +0x34, +0x68",
             {i.normal[0], i.normal[1], i.normal[2], i.inverse_length, i.moving});
        if (w_.k[kWriteNormal]) s.info.normal = {w_.normal[0], w_.normal[1], w_.normal[2]};
        return 1;
    }
    std::uint8_t terrain_collision_enabled() override {
        tape("pinstCollision vf44 IsTerrainCollisionEnabled", {});
        return static_cast<std::uint8_t>(w_.k[kTerrainOn]);
    }
    std::uint8_t collide_line(CollisionLine& l) override {
        const CollisionInfo& i = l.info;
        tape("pinstCollision vf00 CollideLineSegment: vtable, from x,y,z, to x", {i.vtable, i.from[0], i.from[1], i.from[2], i.to[0]});
        tape("  line: to y,z, mask +0x64, +0x68, +0x8c",
             {i.to[1], i.to[2], static_cast<std::uint32_t>(i.mask_64), i.moving, l.byte_8c});
        tape("  line: +0x74..+0x84", {l.ray[0], l.ray[1], l.ray[2], l.ray[3], l.ray[4]});
        tape("  line: +0x88, 1/len +0x34, t +0x38, dir x,y", {l.ray[5], i.inverse_length, i.fraction, i.direction[0], i.direction[1]});
        std::uint32_t n = w_.line_calls++;
        return static_cast<std::uint8_t>((w_.k[kLineHits] >> (n & 31)) & 1);
    }
    double floor_height(Spawn* s, std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        tape("GetPlayerFloorHeight(is_me, x, y, z, flag)", {s == &w_.me ? 1u : 0u, x, y, z, 1});
        const std::uint32_t* f = w_.floors[w_.floor_calls++ & 3];
        return x87_mul(fpu_, X87(std::bit_cast<float>(f[0])), X87(std::bit_cast<float>(f[1])));
    }
    double table_cos(std::uint32_t angle) override {
        tape("sine table vf0c cos(table, angle)", {1, angle});
        return trig();
    }
    double table_sin(std::uint32_t angle) override {
        tape("sine table vf08 sin(table, angle)", {1, angle});
        return trig();
    }

    // ---- functions
    Spawn* spawn_by_id(std::uint32_t id) override {
        return obj<Spawn>(call("GetSpawnByID", std::nullopt, {I(id)}));
    }
    void recache_spell_effects(Character* c) override { call("ReCacheSpellEffects", P(c), {}); }
    std::int32_t cached_buff_effect(Character* c, std::int32_t spa) override {
        return ret(call("GetCachedBuffEffect", P(c), {I(spa), I(0)}));
    }
    std::int32_t cached_aa_effect(Character* c, std::int32_t spa) override {
        return ret(call("GetCachedAAEffect", P(c), {I(spa), I(0)}));
    }
    std::int32_t cached_item_effect(Character* c, std::int32_t spa) override {
        return ret(call("GetCachedItemEffect", P(c), {I(spa), I(0)}));
    }
    std::uint32_t bard_cast_bard(const Spell* s, std::uint32_t cls) override {
        return static_cast<std::uint32_t>(ret(call("BardCastBard", P(&w_.self), {P(s), I(cls)})));
    }
    std::uint8_t sub_00433830(const Spell* s, Character* c) override {
        return static_cast<std::uint8_t>(ret(call("sub_00433830", P(&w_.self), {P(s), P(c)})));
    }
    Handle character_of(Spawn* s) override { return handle(call("FUN_0059f730", P(s), {})); }
    void token_to_client(Handle to, std::uint32_t id, std::uint32_t color) override {
        call("token_string_to_client", std::nullopt, {H(to), I(id), I(color), I(0)});
    }
    std::uint8_t is_item_cast(const Launch* l) override {
        return static_cast<std::uint8_t>(ret(call("IsItemCast", P(l), {})));
    }
    std::uint8_t is_self_or_group_member(Handle pc, const char* name) override {
        return static_cast<std::uint8_t>(ret(call("IsSelfOrGroupMember", H(pc), {T(name)})));
    }
    const char* pc_name(Handle pc) override {
        return pc == reinterpret_cast<Handle>(&w_.pc) || pc == reinterpret_cast<Handle>(&w_.caster_pc) ? w_.pc_name
                                                                                                          : "";
    }
    std::uint32_t do_spell_check(std::uint32_t id, SpellCheckRecord& r) override {
        const Json* c = call("DoSpellCheck", std::nullopt, {I(id), S(), I(1)});
        std::uint32_t seed = w_.k[kSpellCheck];
        for (std::uint32_t i = 0; i < 0x144; ++i) r[i] = static_cast<std::uint8_t>((seed >> 8) + i * 13 + (i >> 3));
        return static_cast<std::uint32_t>(ret(c));
    }
    void log_exit_game() override {
        call("DebugLog_Printf", std::nullopt, {I(0x9c5900), I(0x9c5948), S()});
        tape("DebugLog_Printf line", {0x395b});
    }
    void execute_cmd(std::uint32_t cmd) override { call("__ExecuteCmd", std::nullopt, {I(cmd), I(0), I(0)}); }
    std::uint8_t max_effects(Character* c) override {
        return static_cast<std::uint8_t>(ret(call("GetMaxEffects", P(c), {})));
    }
    Affect* get_effect(Character* c, std::uint32_t i) override {
        return obj<Affect>(call("GetEffect", P(c), {I(i)}));
    }
    void remove_affect(Affect* a) override { call("RemoveAffect", P(&w_.self), {P(a), I(1), I(0)}); }
    void cur_hp(std::uint32_t type) override { call("Cur_HP", P(&w_.self), {I(type), I(1)}); }
    std::uint8_t is_pc_or_mercenary(Spawn* s) override {
        return static_cast<std::uint8_t>(ret(call("Spawn_IsPCOrMercenary", std::nullopt, {P(s)})));
    }
    std::int32_t pvp_spell_duration(const Spell* s, std::uint32_t level) override {
        return ret(call("PvPSpellDuration", P(&w_.self), {P(s), B(level), I(0)}));
    }
    std::int32_t spell_duration(const Spell* s, std::uint32_t level) override {
        return ret(call("SpellDuration", P(&w_.self), {P(s), B(level), I(0)}));
    }
    std::int32_t calc_affect_change(const Spell* s, std::uint32_t level, std::uint32_t index, Spawn* caster,
                                    std::uint32_t flag) override {
        return ret(call("CalcAffectChange", P(&w_.self),
                        {P(s), B(level), B(index), I(0), I(-1), P(caster), I(0), I(-1), I(flag)}));
    }
    void build_string(char* out, std::uint32_t id, const char* arg) override {
        call("build_token_string_PARAM", std::nullopt,
             {S(), I(id), T(arg), I(0), I(0), I(0), I(0), I(0), I(0), I(0), I(0)});
        std::snprintf(out, 6, "B%04x", id & 0xffff);
    }
    void dsp_chat(const char* text, std::uint32_t color) override {
        call("dsp_chat", std::nullopt, {T(text), I(color), I(1), I(1)});
    }
    void dsp_chat_2(const char* text) override { call("dsp_chat_2", std::nullopt, {T(text)}); }
    std::uint8_t get_level(Spawn* s) override {
        return static_cast<std::uint8_t>(ret(call("PlayerZoneClient::GetLevel", P(s), {})));
    }
    Profile* current_profile() override {
        call("GetCurrentProfile", std::nullopt, {});
        return &w_.profile;
    }
    void record_position_sample(Spawn* s, std::uint32_t kind) override {
        call("RecordPositionSample", P(s), {I(kind), I(0), I(0)});
    }
    Spawn* vehicle_of(Spawn* s) override { return obj<Spawn>(call("FUN_0058cf90", P(s), {})); }
    Spawn* mount_spawn(Spawn* s) override { return obj<Spawn>(call("GetMountSpawn", P(s), {})); }
    std::int32_t get_drunkenness(Handle pc) override { return ret(call("GetDrunkenness", H(pc), {})); }
    void set_drunkenness(Handle pc, std::int32_t v) override {
        call("SetDrunkenness", H(pc), {I(v)});
        w_.drunkenness = v;
    }
    std::int32_t random_percent() override { return ret(call("RandomPercentRoll", std::nullopt, {})); }
    std::uint8_t has_property(Spawn* s, std::uint32_t prop) override {
        return static_cast<std::uint8_t>(ret(call("HasProperty", P(s), {I(prop), I(0), I(0)})));
    }
    std::uint32_t get_150(Spawn* s) override {
        return static_cast<std::uint32_t>(ret(call("unk_PlayerClient_Get150", P(s), {})));
    }
    std::uint32_t bind_spot_ok(Spawn* where) override {
        return static_cast<std::uint32_t>(ret(call("sub_00432e90", P(&w_.self), {P(where)})));
    }
    std::uint32_t expansion_zone(std::int32_t zone) override {
        return static_cast<std::uint32_t>(ret(call("ExpansionZone", std::nullopt, {I(zone)})));
    }
    void set_bind_zone(Handle pc, std::uint32_t slot, std::int32_t zone) override {
        call("SetBindZoneID", H(pc), {I(slot), I(zone)});
    }
    void set_bind_y(Handle pc, std::uint32_t slot, std::uint32_t v) override { call("SetBindY", H(pc), {I(slot), I(v)}); }
    void set_bind_x(Handle pc, std::uint32_t slot, std::uint32_t v) override { call("SetBindX", H(pc), {I(slot), I(v)}); }
    void set_bind_z(Handle pc, std::uint32_t slot, std::uint32_t v) override { call("SetBindZ", H(pc), {I(slot), I(v)}); }
    void set_bind_heading(Handle pc, std::uint32_t slot, std::uint32_t v) override {
        call("SetBindHeading", H(pc), {I(slot), I(v)});
    }
    void save_pc() override { call("CEverQuest::SavePC", std::nullopt, {I(0), I(1), I(0xe)}); }
    void set_150(Spawn* s) override { call("FUN_008cfd10", P(s), {I(0)}); }
    void play_sound(Spawn* s, std::uint32_t sound) override { call("PlaySound", P(s), {I(sound)}); }
    std::int32_t zone_index(const char* name) override {
        return ret(call("GetIndexFromZoneName", std::nullopt, {T(name)}));
    }
    void move_to_zone(std::int32_t zone, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::int32_t heading) override {
        call("MoveToZone", std::nullopt,
             {I(zone), I(0x9c58ec), I(0), I(7), I(x), I(y), I(z), I(heading), I(-1), I(-1), I(0)});
    }
    void update_player_actor(Spawn* s) override { call("UpdatePlayerActor", P(s), {}); }
    void sub_00432bc0(std::uint32_t range) override { call("sub_00432bc0", P(&w_.self), {I(range)}); }
    std::uint16_t hton(std::uint32_t op) override {
        return static_cast<std::uint16_t>(ret(call("hton", std::nullopt, {I(op), I(0)})));
    }
    std::uint8_t send_world(std::uint32_t size) override {
        return static_cast<std::uint8_t>(ret(call("UdpConnection::Send", std::nullopt, {I(4), I(0xdd00e0), I(size)})));
    }
    void close_world() override { call("NetMgr_CloseConnection", std::nullopt, {I(0xdd25ac)}); }
    void log_void_connection(std::uint32_t op) override {
        call("DebugLog_Printf", std::nullopt, {I(0x9c3ed8), I(op), S()});
    }
    const char* get_string(std::uint32_t id) override {
        call("getString", std::nullopt, {I(id), I(0)});
        return w_.texts[(id ^ (id >> 4)) & 3];
    }
    void set_endurance(std::int32_t v) override { call("FUN_00441e50", P(&w_.cb), {I(v)}); }
    void cur_endurance() override { call("Cur_Endurance", P(&w_.self), {I(1)}); }
    void cur_mana() override { call("Cur_Mana", P(&w_.self), {I(1)}); }
    void feign(std::uint32_t id, std::int32_t v) override { call("StopCastAndFeignDeath", P(&w_.self), {I(id), I(v)}); }
    Handle get_item(std::uint32_t slot) override {
        call("GetItem", P(&w_.cb), {S(), I(slot)});
        return w_.k[kItem];
    }
    void release_item() override { call("VePointer::~VePointer", std::nullopt, {}); }
    const char* lore_name(Handle item) override {
        call("GetLoreName", H(item), {});
        return w_.texts[w_.k[kLoreText] & 3];
    }
    std::int32_t item_type(Handle item) override { return ret(call("GetType", H(item), {})); }
    std::uint32_t book_lang(Handle item) override {
        return static_cast<std::uint32_t>(ret(call("GetBookLang", H(item), {})));
    }
    const char* lang_desc(std::uint32_t lang) override {
        call("GetLangDesc", std::nullopt, {I(lang)});
        return w_.texts[lang & 3];
    }
    std::uint8_t is_magic(Handle item) override {
        return static_cast<std::uint8_t>(ret(call("IsMagic", H(item), {I(1)})));
    }
    Affect* pc_spell_affect(std::int32_t spa) override {
        return obj<Affect>(call("GetPCSpellAffect", P(&w_.self), {I(spa), I(0), I(0)}));
    }
    void remove_pc_affect(Affect* a, std::uint32_t id) override {
        call("RemovePCAffectex", P(&w_.self), {P(a), I(id)});
    }
    std::int32_t eq_get_time() override { return ret(call("__EQGetTime", std::nullopt, {})); }
    std::int32_t memorized_spell(Handle, std::uint32_t gem) override {
        return ret(call("GetMemorizedSpell", std::nullopt, {I(gem)}));
    }
    std::int32_t spell_focus_pct(const Spell* gem_spell, const Spell* s) override {
        return ret(call("GetSpellFocusPct", P(&w_.self), {P(gem_spell), P(s), I(1), I(0)}));
    }
    void set_spell_recast_timer(Handle pc, std::uint32_t gem) override {
        call("SetSpellRecastTimer", H(pc), {I(gem), I(0)});
    }
    std::uint32_t fast_time() override { return static_cast<std::uint32_t>(ret(call("FastTime", std::nullopt, {}))); }
    void set_linked_reuse(Handle pc, std::uint32_t timer, std::uint32_t when) override {
        call("SetLinkedSpellReuseTimer", H(pc), {I(timer), I(when)});
    }
    std::int32_t random_below(std::uint32_t n) override { return ret(call("FUN_00402870", std::nullopt, {I(n)})); }
    void stop_current_spell_cast() override { call("StopCurrentSpellCast", P(&w_.self), {I(1)}); }
    void apply_fling(Spawn* local, const FlingRecord& r) override {
        call("ApplyFling", P(local), {S()});
        tape("ApplyFling record +0..+0x10", {r.words[0], r.words[1], r.words[2], r.words[3], r.words[4]});
        tape("ApplyFling record +0x14..+0x1c", {r.words[5], r.words[6], r.words[7]});
    }
    std::uint8_t is_valid_teleport(Spawn* s, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t h,
                                   std::uint32_t d) override {
        return static_cast<std::uint8_t>(ret(call("IsValidTeleport", P(s), {I(x), I(y), I(z), I(h), I(d)})));
    }
    std::uint8_t find_buff_level(std::uint32_t id) override {
        return static_cast<std::uint8_t>(ret(call("FindBuffLevelBySpellID", P(&w_.self), {I(id), I(0)})));
    }
    Affect* find_affect_slot(std::uint32_t id, Spawn* caster, std::int32_t* slot, std::uint32_t level,
                             std::uint32_t launch) override {
        const Json* c = call("FindAffectSlot", P(&w_.self), {I(id), P(caster), S(), I(0), I(level), I(0), I(0), B(launch)});
        if (c && (*c)["ret"].type == Json::Type::String) *slot = static_cast<std::int32_t>(w_.k[kSlot]);
        else if (c) *slot = static_cast<std::int32_t>(w_.k[kSlot]);
        return obj<Affect>(c);
    }
    void set_buff_caster_id(std::uint32_t slot, std::uint32_t id) override {
        call("SetBuffCasterID", P(&w_.cb), {I(slot), I(id)});
    }
    std::uint8_t has_buff_flag(const Spell* s) override {
        return static_cast<std::uint8_t>(ret(call("FUN_00453520", P(&w_.self), {P(s)})));
    }
    void on_new_buff(std::uint32_t slot) override { call("FUN_0045b250", P(&w_.self), {I(slot)}); }
    std::int32_t refused_mez(std::int32_t max, Spawn* caster, const Spell* s) override {
        return ret(call("EmptyFunction_004432a0", P(&w_.self), {I(max), P(caster), P(s)}));
    }
    std::int32_t total_effect(Character* c, std::int32_t spa) override {
        return ret(call("TotalEffect", P(c), {I(spa), I(1), I(0), I(1), I(1)}));
    }
    void root_aa(std::int32_t total) override {
        call("EmptyFunction_00443570", P(&w_.self), {I(0x9c58c4), I(total)});
    }
    void movement_rate(std::int32_t v) override { call("eqspa_movement_rate", P(&w_.self), {I(v)}); }
    std::uint32_t change_form(const Spell* s, std::uint32_t id, std::int32_t form, Affect* a) override {
        return static_cast<std::uint32_t>(
            ret(call("eqspa_change_form", P(&w_.self), {P(s), I(id), I(form), P(a), I(0)})));
    }
    void lose_control_message(Spawn* s) override { call("ShowLoseControlMessage", P(s), {}); }
    void update_all_visibility() override { call("UpdateAllPlayersVisibility", std::nullopt, {}); }
    void set_view_actor(const char* name) override { call("SetViewActorByName", std::nullopt, {T(name)}); }
    void spa87(std::int32_t v) override { call("FUN_004940b0", std::nullopt, {I(v)}); }
    void spa57(std::int32_t v) override { call("FUN_00432b20", P(&w_.self), {I(v)}); }
    void update_visible_status() override { call("UpdateMyVisibleStatus", P(&w_.self), {I(0)}); }
    std::int32_t random_low_high(std::int32_t lo, std::int32_t hi) override {
        return ret(call("RandomNumberLowHigh", std::nullopt, {I(lo), I(hi)}));
    }
    std::int32_t calc_affect_change_generic(const Spell* s, std::uint32_t id, std::uint32_t index, Affect* a,
                                            std::int32_t base) override {
        return ret(call("CalcAffectChangeGeneric", P(&w_.self), {P(s), I(id), B(index), P(a), I(base), I(1)}));
    }
    void add_physics_effect(Spawn* s, std::uint32_t id, std::uint32_t caster, std::uint32_t slot,
                            std::int32_t v) override {
        call("AddPhysicsEffect", P(s), {I(id), I(caster), I(slot), I(v)});
    }
    void apply_post_effect(const Spell* s, std::uint32_t index) override {
        call("ApplyPostEffect", P(&w_.self), {P(s), I(index), I(1)});
    }
    void spa436() override { call("FUN_004325e0", P(&w_.self), {I(1)}); }

private:
    double trig() const {
        return x87_mul(fpu_, X87(std::bit_cast<float>(w_.trig[0])), X87(std::bit_cast<float>(w_.trig[1])));
    }
    void fail(const std::string& why) {
        if (error.empty()) error = why;
    }
    // Compares one recorded value with what the port passed.
    bool same(const Json& rec, const Arg& a) const {
        switch (a.kind) {
        case Arg::Skip: return true;
        case Arg::Int:
            return rec.type == Json::Type::Number && unrebase(rec.u32()) == unrebase(a.v);
        case Arg::Byte:
            if (rec.type == Json::Type::String) return rec.text == "world"; // (the player pointer's upper bytes)
            return (rec.u32() & 0xff) == (a.v & 0xff);
        case Arg::Ptr: {
            Named n = names_.of(a.p);
            if (n.name) return rec.type == Json::Type::String && rec.text == *n.name;
            return rec.type == Json::Type::Number && rec.u32() == n.value;
        }
        case Arg::Text: {
            if (a.t == nullptr) return rec.type == Json::Type::Number && rec.u32() == 0;
            if (rec.type == Json::Type::String && rec.text == "world") return names_.in_world(a.t);
            if (rec.type != Json::Type::String || names_.in_world(a.t)) return false;
            auto p = reinterpret_cast<const char*>(&frame), q = reinterpret_cast<const char*>(&frame + 1);
            if (a.t >= p && a.t < q) {
                deferred_.emplace_back(&rec.text, a.t);
                return true;
            }
            return rec.text == a.t;
        }
        }
        return false;
    }
    const Json* call(const char* fn, std::optional<Arg> self, std::initializer_list<Arg> args) {
        if (!error.empty()) return nullptr;
        if (call_ >= calls_.size()) {
            fail(std::string("extra call ") + fn);
            return nullptr;
        }
        const Json& c = *calls_[call_++];
        std::string where = "call " + std::to_string(call_ - 1) + " " + fn;
        if (c["fn"].text != fn) {
            fail(where + ": the original called " + c["fn"].text);
            return nullptr;
        }
        if (self && self->kind == Arg::Ptr && c.has("this")) {
            const Json& t = c["this"];
            Named n = names_.of(self->p);
            bool ok = n.name ? (t.type == Json::Type::String && (t.text == *n.name || t.text == "world"))
                             : (t.type == Json::Type::Number && t.u32() == n.value);
            if (!ok) fail(where + ": this differs");
        }
        int i = 0;
        for (const Arg& a : args) {
            std::string key = "a" + std::to_string(i++);
            if (a.kind == Arg::Skip) continue;
            if (!c.has(key)) {
                fail(where + ": " + key + " missing");
                continue;
            }
            if (!same(c[key], a)) {
                std::string got = a.kind == Arg::Text ? std::string(a.t ? a.t : "(null)")
                                                      : std::to_string(static_cast<std::int32_t>(a.kind == Arg::Ptr ? names_.of(a.p).value : a.v));
                fail(where + ": " + key + " " + got + " recorded " + (c[key].type == Json::Type::String ? c[key].text : c[key].text));
            }
        }
        return &c;
    }
    void tape(const char* op, std::initializer_list<std::uint32_t> args,
              std::initializer_list<std::uint32_t> masks = {}) {
        if (!error.empty()) return;
        if (tape_at_ >= tape_.size()) {
            fail(std::string("extra virtual ") + op);
            return;
        }
        const Json& r = *tape_[tape_at_++];
        std::string where = "virtual " + std::to_string(tape_at_ - 1) + " " + op;
        if (r[0].text != op) {
            fail(where + ": the original made " + r[0].text);
            return;
        }
        std::size_t i = 0;
        auto m = masks.begin();
        for (std::uint32_t a : args) {
            std::uint32_t mask = m != masks.end() ? *m++ : 0xffffffffu;
            std::uint32_t rec = r[i + 1].u32();
            if ((rec & mask) != (a & mask)) {
                char b[160];
                std::snprintf(b, sizeof b, ": argument %zu %08x recorded %08x", i, a, rec);
                fail(where + b);
            }
            ++i;
        }
    }
    static std::int32_t ret(const Json* c) { return c ? c->operator[]("ret").i32() : 0; }
    template <typename T> T* obj(const Json* c) const {
        if (!c) return nullptr;
        const Json& r = (*c)["ret"];
        if (r.type != Json::Type::String) return nullptr;
        return reinterpret_cast<T*>(names_.object(r.text));
    }
    Handle handle(const Json* c) const {
        if (!c) return 0;
        const Json& r = (*c)["ret"];
        if (r.type != Json::Type::String) return r.u32();
        return names_.object(r.text);
    }

    World& w_;
    Names names_;
    Fpu fpu_;
    mutable std::vector<std::pair<const std::string*, const char*>> deferred_;
    std::vector<const Json*> calls_, tape_;
    std::size_t call_ = 0, tape_at_ = 0;
};

// ---------------------------------------------------------------- comparisons

struct Compare {
    std::string error;
    void u(const char* what, std::uint32_t got, std::uint32_t want) {
        if (error.empty() && got != want) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %08x, recorded %08x", what, got, want);
            error = b;
        }
    }
    void i(const char* what, const Json& rec, std::int64_t got) { u(what, static_cast<std::uint32_t>(got), rec.u32()); }
    void f(const char* what, const Json& rec, std::uint32_t got) { u(what, got, hailstone::test::f32_bits(rec)); }
    void name(const char* what, const Json& rec, const Names& n, const void* p) {
        if (!error.empty()) return;
        Named v = n.of(reinterpret_cast<std::uintptr_t>(p));
        bool ok = v.name ? rec.type == Json::Type::String && rec.text == *v.name
                         : rec.type == Json::Type::Number && rec.u32() == v.value;
        if (!ok) error = std::string(what) + " differs";
    }
    void spawn(const char* what, const Json& j, const Spawn& s) {
        std::string p = what;
        auto key = [&](const char* k) { return (p + "." + k); };
        i(key("type").c_str(), j["type"], s.type);
        i(key("flag_208").c_str(), j["flag_208"], s.flag_208);
        i(key("owner_38c").c_str(), j["owner_38c"], s.owner_38c);
        i(key("spawn_id").c_str(), j["spawn_id"], s.spawn_id);
        f(key("floor_z").c_str(), j["floor_z"], s.floor_z);
        f(key("y").c_str(), j["y"], s.y);
        f(key("x").c_str(), j["x"], s.x);
        f(key("z").c_str(), j["z"], s.z);
        f(key("speed_y").c_str(), j["speed_y"], s.speed_y);
        f(key("speed_x").c_str(), j["speed_x"], s.speed_x);
        f(key("speed_z").c_str(), j["speed_z"], s.speed_z);
        f(key("rotation_speed").c_str(), j["rotation_speed"], s.rotation_speed);
        f(key("heading").c_str(), j["heading"], s.heading);
        f(key("heading_speed_8c").c_str(), j["heading_speed_8c"], s.heading_speed);
        f(key("model_height_138").c_str(), j["model_height_138"], s.model_height);
        i(key("class").c_str(), j["class"], s.class_id);
        i(key("casting_spell").c_str(), j["casting_spell"], s.casting_spell);
        i(key("stand_state_35c").c_str(), j["stand_state_35c"], s.stand_state);
        i(key("flag_428").c_str(), j["flag_428"], s.flag_428);
        u(key("has_character").c_str(), s.character != nullptr, j["has_character"].truthy());
    }
};

// The regenerated world against the record's inputs.
std::string check_inputs(const Json& in, World& w, const Names& names) {
    Compare c;
    const Json& h = in["hit"];
    c.i("hit.caster_id", h["caster_id"], w.hit.caster_id);
    c.i("hit.item_level", h["item_level"], w.hit.item_level);
    c.f("hit.modifier", h["modifier"], w.hit.modifier);
    c.f("hit.push_heading", h["push_heading"], w.hit.push_heading);
    c.i("hit.spell_1b", h["spell_1b"], w.hit.word_1b);
    c.i("hit.song_value_1d", h["song_value_1d"], w.hit.song_value);
    c.i("hit.spell_id", h["spell_id"], w.hit.spell_id);
    c.i("hit.caster_level", h["caster_level"], w.hit.caster_level);
    c.u("launch", in["launch"].is_null(), w.launch_used == nullptr);
    if (w.launch_used) {
        c.i("launch.byte_32", in["launch"]["byte_32"], w.launch.byte_32);
        c.i("launch.item_flag_40", in["launch"]["item_flag_40"], w.launch.item_flag);
    }
    c.i("duration_override", in["duration_override"], w.duration);
    const Json& s = in["spell"];
    const Spell& sp = w.spells[0];
    c.i("spell.id", s["id"], w.spell_ids[0]);
    c.f("spell.range", s["range"], sp.range);
    c.f("spell.push_back", s["push_back"], sp.push_back);
    c.f("spell.push_up", s["push_up"], sp.push_up);
    c.i("spell.duration_formula", s["duration_formula"], sp.duration_formula);
    for (std::size_t k = 0; k < 12; ++k) {
        c.i("spell.attrib", s["attrib"][k], sp.attrib[k]);
        c.i("spell.base", s["base"][k], sp.base[k]);
        c.i("spell.base2", s["base2"][k], sp.base2[k]);
        c.i("spell.max", s["max"][k], sp.max[k]);
    }
    c.i("spell.recourse", s["recourse_178"], sp.recourse_178);
    c.i("spell.timer", s["timer_id_198"], sp.timer_id_198);
    c.i("spell.flag_240", s["flag_240"], sp.flag_240);
    c.i("spell.type", s["spell_type_26b"], sp.spell_type);
    c.i("spell.26c", s["byte_26c"], sp.byte_26c);
    c.i("spell.target_type", s["target_type_26e"], sp.target_type);
    if (c.error.empty() && (s["name"].text != sp.name.data() || s["teleport_zone"].text != sp.teleport_zone.data()))
        c.error = "spell texts differ";
    for (std::size_t k = 1; k < 8; ++k) {
        const Json& o = in["other_spells"][k - 1];
        c.i("other.id", o["id"], w.spell_ids[k]);
        c.i("other.duration_formula", o["duration_formula"], w.spells[k].duration_formula);
        for (std::size_t j = 0; j < 12; ++j) c.i("other.attrib", o["attrib"][j], w.spells[k].attrib[j]);
        c.i("other.recourse", o["recourse_178"], w.spells[k].recourse_178);
        c.i("other.timer", o["timer_id_198"], w.spells[k].timer_id_198);
        c.i("other.flag_240", o["flag_240"], w.spells[k].flag_240);
    }
    c.u("me present", !in["me"].is_null(), w.has_me);
    if (w.has_me) c.spawn("me", in["me"], w.me);
    std::string caster = w.caster_none ? "none" : (w.caster_me ? "me" : "caster");
    if (c.error.empty() && in["caster"].text != caster) c.error = "caster differs";
    c.spawn("caster_spawn", in["caster_spawn"], w.caster);
    const Json& ch = in["character"];
    c.i("cache_valid", ch["cache_valid_1a4"], w.self.cache_valid);
    c.i("aa_cache_valid", ch["aa_cache_valid_1ac"], w.self.aa_cache_valid);
    c.i("stats_dirty", ch["stats_dirty_c"], w.self.stats_dirty);
    c.i("zone_id", ch["zone_id"], w.cb.zone_id);
    c.i("class", ch["class"], w.profile.class_id);
    c.i("weight", ch["weight_31a8"], w.profile.weight);
    c.i("mana", ch["mana_338c"], w.profile.mana);
    c.i("drunkenness", ch["drunkenness"], w.drunkenness);
    for (std::size_t k = 0; k < 10; ++k) {
        const Json& b = in["buffs"][k];
        c.i("buff.type", b["type"], w.buffs[k].type);
        c.i("buff.level", b["level"], w.buffs[k].level);
        c.i("buff.spell_id", b["spell_id"], w.buffs[k].spell_id);
        for (std::size_t j = 0; j < 12; ++j) c.i("buff.slot", b["slot_data"][j], w.buffs[k].slot_data[j]);
    }
    const Json& g = in["globals"];
    c.i("ruleset", g["ruleset_dcee70"], w.globals.ruleset);
    c.i("zone_type", g["zone_type"], w.globals.zone_type);
    c.i("bind_blocked", g["bind_blocked_ddfc69"], w.globals.bind_blocked);
    c.u("connected", g["connected"].truthy(), w.globals.connected);
    c.name("target", g["target"], names, w.globals.target);
    c.name("local_player", g["local_player"], names, w.globals.local_player);
    c.name("controlled_player", g["controlled_player"], names, w.globals.controlled_player);
    return c.error;
}

std::string check_outputs(const Json& out, World& w, std::uint8_t result) {
    Compare c;
    c.i("value", out["value"], result);
    if (w.has_me && out.has("me")) c.spawn("me", out["me"], w.me);
    c.f("vehicle.speed_y", out["vehicle"]["speed_y"], w.vehicle.speed_y);
    c.f("vehicle.speed_x", out["vehicle"]["speed_x"], w.vehicle.speed_x);
    c.f("vehicle.speed_z", out["vehicle"]["speed_z"], w.vehicle.speed_z);
    c.f("mount.rotation_speed", out["mount"]["rotation_speed"], w.mount.rotation_speed);
    c.f("mount.heading", out["mount"]["heading"], w.mount.heading);
    c.f("mount.heading_speed", out["mount"]["heading_speed_8c"], w.mount.heading_speed);
    const Json& a = out["affect"];
    const Affect& af = w.affect;
    c.i("affect.type", a["type"], af.type);
    c.i("affect.level", a["level"], af.level);
    c.i("affect.byte_2", a["byte_2"], af.byte_2);
    c.i("affect.byte_3", a["byte_3"], af.byte_3);
    c.f("affect.modifier", a["modifier"], af.modifier);
    c.i("affect.spell_id", a["spell_id"], af.spell_id);
    c.i("affect.duration", a["duration"], af.duration);
    c.i("affect.caster_10", a["caster_10"], af.caster_10);
    c.i("affect.dword_14", a["dword_14"], af.dword_14);
    c.f("affect.y_18", a["y_18"], af.y_18);
    c.f("affect.x_1c", a["x_1c"], af.x_1c);
    c.f("affect.z_20", a["z_20"], af.z_20);
    c.i("affect.flags_24", a["flags_24"], af.flags_24);
    for (std::size_t j = 0; j < 12; ++j) c.i("affect.slot_data", a["slot_data"][j], af.slot_data[j]);
    for (std::size_t k = 0; k < 10; ++k)
        for (std::size_t j = 0; j < 12; ++j) c.i("buff_slot_data", out["buff_slot_data"][k][j], w.buffs[k].slot_data[j]);
    const Json& ch = out["character"];
    c.i("cache_valid_1a4", ch["cache_valid_1a4"], w.self.cache_valid);
    c.i("aa_cache_valid_1ac", ch["aa_cache_valid_1ac"], w.self.aa_cache_valid);
    c.i("stats_dirty_c", ch["stats_dirty_c"], w.self.stats_dirty);
    c.f("cb.y", ch["y"], w.cb.y);
    c.f("cb.x", ch["x"], w.cb.x);
    c.f("cb.z", ch["z"], w.cb.z);
    c.f("cb.heading", ch["heading"], w.cb.heading);
    c.i("mesmerized_fc", ch["mesmerized_fc"], w.cb.mesmerized);
    c.i("mana_338c", ch["mana_338c"], w.profile.mana);
    c.i("drunkenness", ch["drunkenness"], w.drunkenness);
    c.i("dword_3248", ch["dword_3248"], w.profile.dword_3248);
    c.i("dword_3264", ch["dword_3264"], w.profile.dword_3264);
    for (std::size_t g = 0; g < 12; ++g) c.i("gem_timers", ch["gem_timers"][g], w.timers[g]);
    const Json& g = out["globals"];
    c.i("local_pushed", g["local_pushed_ddfd59"], w.globals.local_pushed);
    c.i("controlled_pushed", g["controlled_pushed_ddfd58"], w.globals.controlled_pushed);
    c.i("save_reason", g["save_reason_ddfd74"], w.globals.save_reason);
    c.i("packet_opcode", g["packet_opcode_dd00e0"], w.globals.packet_opcode);
    c.i("packet_target", g["packet_target_dd00e2"], w.globals.packet_target);
    c.i("send_count", g["send_count_f99584"], w.globals.send_count);
    return c.error;
}

void run_file(const char* file, const Family& family, std::uint32_t seed, void (*tweak)(World&, Rng&)) {
    static World world; // large; one per run
    Rng rng(seed);
    int it = 0;
    hailstone::test::each_vector(file, [&](const Json& r) -> std::string {
        World& w = world;
        Rng local(rng.Next());
        make_world(w, local, family, static_cast<std::uint32_t>(it) * 7);
        ++it;
        if (tweak) tweak(w, local);
        w.k[kThrowAt] = 0;
        bool no_launch = local.Chance(2) && family.early;
        w.launch_used = no_launch ? nullptr : &w.launch;
        w.duration = local.Chance(80) ? -1 : value(local);
        Names names(w);
        if (std::string why = check_inputs(r["in"], w, names); !why.empty()) return "regenerated input: " + why;
        Fpu fpu(r["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53);
        Replay replay(w, r["out"], fpu);
        HitState state;
        state.self = &w.self;
        state.globals = &w.globals;
        state.frame = &replay.frame;
        // GetSpawnByID answers the record's caster; the world's knob says which.
        std::uint8_t result = hit_by_spell(state, w.hit, w.launch_used, w.duration, replay, fpu);
        if (std::string why = replay.finish(); !why.empty()) return why;
        return check_outputs(r["out"], w, result);
    });
}

// The Moves records' worlds again, with each SPA 379 base raised to 2^26: there 2^26 - 2 rounds back to 2^26, and
// the shadow step's distance loop never ended (the original's x87 code would not end either). The run stops at
// the first step that leaves the distance unchanged; the replay's later mismatches are expected and ignored.
int shadow_step_fixed_points() {
    static World world;
    Rng rng(0x7e44);
    int it = 0, raised = 0;
    hailstone::test::each_vector("HitBySpell_Moves.jsonl", [&](const Json& r) -> std::string {
        World& w = world;
        Rng local(rng.Next());
        make_world(w, local, kMoves, static_cast<std::uint32_t>(it) * 7);
        ++it;
        moves_tweak(w, local);
        w.k[kThrowAt] = 0;
        bool no_launch = local.Chance(2) && kMoves.early;
        w.launch_used = no_launch ? nullptr : &w.launch;
        w.duration = local.Chance(80) ? -1 : value(local);
        Names names(w);
        if (!check_inputs(r["in"], w, names).empty()) return "";
        bool any = false;
        for (std::size_t i = 0; i < 12; ++i)
            if (w.spells[0].attrib[i] == 379 && w.spells[0].base[i] > 5) {
                w.spells[0].base[i] = 1 << 26;
                any = true;
            }
        if (!any) return "";
        ++raised;
        Fpu fpu(r["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53);
        Replay replay(w, r["out"], fpu);
        HitState state;
        state.self = &w.self;
        state.globals = &w.globals;
        state.frame = &replay.frame;
        (void)hit_by_spell(state, w.hit, w.launch_used, w.duration, replay, fpu);
        return "";
    });
    return raised;
}

} // namespace

TEST_CASE("HitBySpell: a shadow step (SPA 379) whose float distance cannot shrink ends") {
    const int raised = shadow_step_fixed_points();
    // An empty HAILSTONE_VECTORS_DIR gives a directory with no files in it: the file itself decides.
    const auto dir = hailstone::test::vector_dir();
    std::error_code ec;
    if (!dir || !std::filesystem::is_regular_file(*dir / "HitBySpell_Moves.jsonl", ec))
        MESSAGE("no vectors (HAILSTONE_VECTORS_DIR); nothing checked");
    else
        CHECK(raised > 0);
}

TEST_CASE("HitBySpell vectors: validation") { run_file("HitBySpell_Validation.jsonl", kEarly, 0x7e41, early_tweak); }
TEST_CASE("HitBySpell vectors: instant effects") { run_file("HitBySpell_Instant.jsonl", kInstant, 0x7e42, nullptr); }
TEST_CASE("HitBySpell vectors: items") { run_file("HitBySpell_Items.jsonl", kItems, 0x7e43, nullptr); }
TEST_CASE("HitBySpell vectors: moves") { run_file("HitBySpell_Moves.jsonl", kMoves, 0x7e44, moves_tweak); }
TEST_CASE("HitBySpell vectors: buffs") { run_file("HitBySpell_Buff.jsonl", kBuff, 0x7e45, buff_tweak); }

// ---------------------------------------------------------------- the client's landing (game/hit_landing.h)

#include "game/hit_landing.h"
#include "game/sine_table.h"
#include "game/spells.h"
#include "game/world_state.h"

namespace {

std::string landing_spell_line(std::initializer_list<std::pair<int, std::string>> set) {
    std::vector<std::string> cols(237, "0");
    for (int i = 1; i <= 8; ++i) cols[static_cast<std::size_t>(i)].clear();
    for (int i = 86; i <= 97; ++i) cols[static_cast<std::size_t>(i)] = "254";
    for (const auto& [c, v] : set) cols[static_cast<std::size_t>(c)] = v;
    std::string line;
    for (std::size_t i = 0; i < cols.size(); ++i) line += (i ? "^" : "") + cols[i];
    return line;
}

} // namespace

TEST_CASE("A spell landing on the player: its push, and SPA 123 immunity") {
    hailstone::game::SpellTable table;
    std::string text = landing_spell_line({{0, "300"}, {1, "Shove"}, {11, "10"}, {12, "2"}, {83, "0"}, {98, "5"}}) +
                       "\n" + landing_spell_line({{0, "301"}, {1, "Immune"}, {20, "5"}, {86, "123"}, {83, "0"}});
    REQUIRE(table.parse(text) == 2);
    hailstone::game::WorldState world;
    world.player_id = 1;
    world.spawns[1].id = 1;
    world.spawns[1].level = 10;
    world.spawns[2].id = 2;
    world.spawns[2].is_npc = true;
    hailstone::game::SpellState state;
    hailstone::game::HitLandingContext ctx;
    ctx.spells = &table;
    ctx.world = &world;
    ctx.spell_state = &state;
    ctx.level = 10;
    // Pushed toward heading 128: SpeedY += cos * 10 (table entry 0), SpeedX += sin * 10, SpeedZ += 2.
    auto r = hailstone::game::land_spell(hailstone::game::hit_record(2, 0, 1.0f, 128.0f, 0, 0, 300, 10), ctx);
    CHECK(r.value == 1);
    CHECK(r.velocity_change.x == hailstone::game::cmath_cos(128.0f) * 10.0f);
    CHECK(r.velocity_change.y == doctest::Approx(10.0f));
    CHECK(r.velocity_change.z == 2.0f);
    // A detrimental SPA 123 with a non-negative change is refused (message 0x30a0).
    r = hailstone::game::land_spell(hailstone::game::hit_record(2, 0, 1.0f, 0.0f, 0, 0, 301, 10), ctx);
    CHECK(r.value == 0);
    CHECK(r.velocity_change == glm::vec3(0.0f));
}

TEST_CASE("A spell landing on the player: SPA 74 feigns death (StopCastAndFeignDeath)") {
    hailstone::game::SpellTable table;
    // Imitate Death's shape: a beneficial self spell whose first slot is SPA 74 (base 100), and a spell without it.
    std::string text = landing_spell_line({{0, "400"}, {1, "Feign"}, {20, "100"}, {86, "74"}, {83, "1"}}) + "\n" +
                       landing_spell_line({{0, "401"}, {1, "Shove"}, {11, "10"}, {83, "0"}, {98, "5"}});
    REQUIRE(table.parse(text) == 2);
    hailstone::game::WorldState world;
    world.player_id = 1;
    world.spawns[1].id = 1;
    world.spawns[1].level = 10;
    hailstone::game::SpellState state;
    hailstone::game::HitLandingContext ctx;
    ctx.spells = &table;
    ctx.world = &world;
    ctx.spell_state = &state;
    ctx.level = 10;
    auto r = hailstone::game::land_spell(hailstone::game::hit_record(1, 0, 1.0f, 0.0f, 0, 0, 400, 10), ctx);
    CHECK(r.feigned);
    r = hailstone::game::land_spell(hailstone::game::hit_record(1, 0, 1.0f, 0.0f, 0, 0, 401, 10), ctx);
    CHECK_FALSE(r.feigned);
}
