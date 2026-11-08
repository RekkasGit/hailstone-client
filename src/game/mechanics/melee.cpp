// Melee timing, range and DoAttack (melee.h). Addresses are eqgame.exe's; each function names the original it
// follows.
#include "game/mechanics/melee.h"

#include <cmath>

#include "core/wrap_math.h"

namespace hailstone::game::mechanics::melee {

namespace {

// Float constants of the exe, as the x87 loads them.
constexpr float kMinRange = 14.0f;     // 0x9c7334
constexpr float kRadiusShare = 0.75f;  // 0x9c59f8
constexpr float kRangeExtra = 2.0f;    // 0x9c58e8
constexpr float kMaxRange = 75.0f;     // 0x9ca704
constexpr float kPercent = 0.01f;      // 0x9c4be4
constexpr float kTwoTo32 = 4294967296.0f; // 0x9c3eac
constexpr float kQuiverShare = 0.0025f; // 0x9d7930
constexpr float kPcRangeScale = 1.25f; // 0x9c7ee8
constexpr float kNpcFacing = 80.0f;    // 0x9c7b44
constexpr float kPcFacing = 256.0f;    // 0x9c7328
constexpr float kWrapHigh = 472.0f, kWrapLow = 40.0f;     // 0x9d792c, 0x9c7c4c
constexpr float kPcWrapHigh = 350.0f, kPcWrapLow = 150.0f; // 0x9cefc0, 0x9c7b3c
constexpr float kRangedTooClose = 30.0f; // 0x9c59f0
constexpr double kBearingEpsilon = 9.999999974752427e-07; // 0x9ce730 (qword)
constexpr double kHalfTurnDegrees = 180.0;                 // 0x9ce728
constexpr double kInversePi = 0.3183099014828645;          // 0x9ce720
constexpr double kQuarterTurnDegrees = 90.0;               // 0x9ce718
constexpr double kThreeQuarterDegrees = 270.0;             // 0x9ce710
constexpr float kHeadingUnits = 511.5f;                    // 0x9cca68
constexpr float kPerDegree = 0.0027777778f;                // 0x9c58e0

constexpr std::uint32_t kStrInvulnerable = 12695; // 0x3197 You can't attack while invulnerable!
constexpr std::uint32_t kStrAttackSelf = 12696;   // 0x3198
constexpr std::uint32_t kStrNoTarget = 12697;     // 0x3199
constexpr std::uint32_t kStrNoControl = 12368;    // 0x3050
constexpr std::uint32_t kStrMustStand = 12369;    // 0x3051
constexpr std::uint32_t kStrNeedPiercer = 12874;  // 0x324a
constexpr std::uint32_t kStrNoRanged = 9150;      // 0x23be You do not have a ranged weapon equipped
constexpr std::uint32_t kTokTooClose = 12698;     // 0x319a (a token id) Your target is too close to use a ranged weapon!
constexpr std::uint32_t kTokTooFar = 124;         // 0x7c
constexpr std::uint32_t kTokCantSee = 108;        // 0x6c
constexpr std::uint32_t kTokCantHit = 123;        // 0x7b
constexpr std::uint32_t kTokStunned = 125;        // 0x7d
constexpr std::int32_t kColorCombat = 0x12f, kColorRed = 0xd;

// An x87 operand loaded from a float in memory (fld quiets a signalling NaN; the payload stays).
X87 loaded(float f) { return X87(static_cast<double>(f)); }

// fcom-style tests: NaN compares unordered (false).
bool less(double a, double b) { return a < b; }

// x87 add/sub/mul rounding to the precision, and the store of an extended value to a float.
double store_float(double v) { return Fpu::to_float(v); }

// The low dword of fistp qword under a chop control word (truncation toward zero).
std::uint32_t chop_low32(double v) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(v))));
}

} // namespace

// ---------------------------------------------------------------- __get_melee_range

float melee_range(const RangeSpawn* a, const RangeSpawn* b, const Fpu& f) {
    if (!a || !b) return kMinRange;
    // PlayerZoneClient_GetScaledMeleeRadius: fld radius; fmul scale (unrounded), stored as a float by the caller.
    double first = store_float(x87_mul(f, loaded(a->melee_radius), X87(a->scale)));
    double second = store_float(x87_mul(f, loaded(b->melee_radius), X87(b->scale)));
    // |height(a) - height(b)|: fld a (stored as a float), fld b, fsubr [a], stored, ABS_wrapper (fabs), stored.
    double ha = store_float(static_cast<double>(a->height));
    double gap = store_float(std::fabs(store_float(x87_sub(f, X87(ha), loaded(b->height)))));
    double r = store_float(x87_mul(f, x87_add(f, X87(first), X87(second)), X87(kRadiusShare)));
    // fld 14; fcomp r: raised to 14 unless 14 <= r (NaN kept).
    if (less(r, kMinRange)) r = kMinRange;
    double d = store_float(x87_add(f, x87_add(f, X87(r), X87(kRangeExtra)), X87(gap)));
    // fld 75; fcomp d: lowered to 75 when 75 < d (NaN kept).
    if (less(kMaxRange, d)) d = kMaxRange;
    return static_cast<float>(d);
}

// ---------------------------------------------------------------- ModifyAttackSpeed

std::uint32_t modify_attack_speed(std::uint32_t delay, std::uint32_t with_bags, std::int32_t haste, BagQueries* bags,
                                  const Fpu& f) {
    if (delay == 0) return 0;
    if (haste > 0) {
        // fild haste; fmul 0.01f; fild delay (+2^32 when negative); fdivrp: delay / percent; fistp qword (chop).
        double percent = f.mul(static_cast<double>(haste), static_cast<double>(kPercent));
        double d = static_cast<double>(static_cast<std::int32_t>(delay));
        if (static_cast<std::int32_t>(delay) < 0) d = f.add(d, static_cast<double>(kTwoTo32));
        delay = chop_low32(f.div(d, percent));
    }
    if (with_bags == 0 || !bags) return delay;
    for (std::int32_t slot = 0x17; slot <= 0x20; ++slot) {
        if (!bags->slot_available(slot)) continue;
        if (!bags->has_item(slot) || bags->item_class(slot) != 1) continue;
        if (bags->container_type(slot) != 2 || bags->weight_reduction(slot) <= 0) continue;
        std::int32_t reduction = bags->weight_reduction(slot);
        // fild wr; fmul 0.0025f; fild delay (+2^32 when negative); fmulp; __ftol2_sse.
        double share = f.mul(static_cast<double>(reduction), static_cast<double>(kQuiverShare));
        double d = static_cast<double>(static_cast<std::int32_t>(delay));
        if (static_cast<std::int32_t>(delay) < 0) d = f.add(d, static_cast<double>(kTwoTo32));
        std::int32_t cut = Fpu::ftol(f.mul(share, d));
        delay -= static_cast<std::uint32_t>(cut) + 1u;
        if (delay <= 0x2bcu) delay = 0x2bcu;
        break;
    }
    return delay;
}

// ---------------------------------------------------------------- GetHandToHandDelay

std::optional<std::int32_t> hand_to_hand_delay(const HandToHandInput& in) {
    if (!in.has_character) return std::nullopt;
    bool epic = in.hands_item && in.hands_item_id == 0x299c && in.level > 0x2e;
    std::int32_t cap = 0, extra = 0;
    if (epic) cap = 0x118;
    else if (in.profile_3370 == 0x80) extra = 1;
    std::int32_t capped = cap < in.skill ? cap : in.skill;
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(extra - capped / 21) + 0x26u) * 100u);
}

// ---------------------------------------------------------------- the rolls

bool check_double_attack(const DoubleAttackInput& in, RollQueries& q) {
    if ((in.class_id == 7 || in.class_id == 4) && in.has_spawn && in.do_special_melee != 0) return false;
    std::uint32_t chance = 0;
    if (q.adjusted_skill(0x14) > 0) {
        std::int32_t skill = q.skill(0x14);
        std::int32_t level = q.level();
        chance = static_cast<std::uint32_t>(skill) + static_cast<std::uint32_t>(level);
    } else if (q.total_effect(0xe1) == 0) {
        return false;
    }
    std::int32_t times_five = core::wrapped_div(static_cast<std::uint32_t>(q.total_effect(0xe1)) * 500u, 100);
    chance += static_cast<std::uint32_t>(times_five);
    if (std::int32_t bonus = q.total_effect(0xb1)) {
        chance += static_cast<std::uint32_t>(
            core::wrapped_div(static_cast<std::uint32_t>(bonus) * chance, 100));
    }
    return q.random() % 500 + 1 <= static_cast<std::int32_t>(chance);
}

bool check_dual_wield(const DualWieldInput& in, RollQueries& q) {
    if (q.adjusted_skill(0x16) <= 0) return false;
    if (in.primary && in.primary_class == 0 &&
        (in.primary_type == 1 || in.primary_type == 4 || in.primary_type == 0x23))
        return false;
    std::int32_t bonus = q.total_effect(0x114);
    std::int32_t roll = q.random() % 375 + 1;
    std::int32_t skill = q.skill(0x16);
    std::int32_t level = q.level();
    std::uint32_t chance = static_cast<std::uint32_t>(level) +
                           (static_cast<std::uint32_t>(skill) + static_cast<std::uint32_t>(bonus));
    if (std::int32_t extra = q.total_effect(0xb0)) {
        chance += static_cast<std::uint32_t>(
            core::wrapped_div(static_cast<std::uint32_t>(extra) * chance, 100));
    }
    return roll <= static_cast<std::int32_t>(chance);
}

// ---------------------------------------------------------------- CanIHit

double distance_squared_3d(const Spawn& a, const Spawn& b, const Fpu& f) {
    double dy = x87_sub(f, loaded(a.y), X87(b.y));
    double dx = x87_sub(f, loaded(a.x), X87(b.x));
    double za = x87_sub(f, loaded(a.z), X87(a.height));
    double zb = x87_sub(f, loaded(b.z), X87(b.height));
    double dz = x87_sub(f, X87(za), X87(zb));
    double xx = x87_mul(f, X87(dx), X87(dx));
    double yy = x87_mul(f, X87(dy), X87(dy));
    double plane = x87_add(f, X87(yy), X87(xx));
    return x87_add(f, X87(plane), X87(x87_mul(f, X87(dz), X87(dz))));
}

double bearing(float y1, float x1, float y2, float x2, const Fpu& f) {
    double dy = std::fabs(x87_sub(f, loaded(y1), X87(y2)));
    double dx = std::fabs(x87_sub(f, loaded(x1), X87(x2)));
    if (kBearingEpsilon > dy) dy = kBearingEpsilon;
    // fpatan (st1 = atan(st1 / st0), not rounded to the precision control: a 64-bit significand), then
    // fmul 180.0 and fmul 1/pi at the precision.
    long double angle = std::atan2(static_cast<long double>(dx), static_cast<long double>(dy));
    long double scaled = angle * static_cast<long double>(kHalfTurnDegrees);
    double degrees = f.precision() == FpuPrecision::Double53 ? static_cast<double>(scaled)
                                                              : f.mul(static_cast<double>(scaled), 1.0);
    degrees = f.mul(degrees, kInversePi);
    double h;
    bool y_unordered = std::isnan(y1) || std::isnan(y2);
    bool x_unordered = std::isnan(x1) || std::isnan(x2);
    if (!y_unordered && !(y1 < y2)) {
        if (x_unordered) h = degrees;
        else if (x2 < x1) h = f.add(kHalfTurnDegrees, degrees);
        else h = f.add(kQuarterTurnDegrees, f.sub(kQuarterTurnDegrees, degrees));
    } else if (y_unordered) {
        h = degrees;
    } else if (!x_unordered && !(x2 > x1)) {
        h = f.add(f.sub(kQuarterTurnDegrees, degrees), kThreeQuarterDegrees);
    } else {
        h = degrees;
    }
    h = x87_mul(f, X87(h), X87(kHeadingUnits));
    return x87_mul(f, X87(h), X87(kPerDegree));
}

bool can_i_hit(const Spawn& me, const Spawn* target, float extra, HitQueries& q, const Fpu& f) {
    if (!q.has_character()) return false;
    if (q.total_effect(0x28) > 0) {
        q.chat(kStrInvulnerable, kColorCombat);
        return false;
    }
    if (target == &me) {
        q.chat(kStrAttackSelf, kColorCombat);
        return false;
    }
    if (!target) {
        q.chat(kStrNoTarget, kColorCombat);
        return false;
    }
    const Spawn& t = *target;
    if (!q.allowed_to_attack(t)) return false;
    if (!me.has_actor || !t.has_actor) return false;
    RangeSpawn ra = me.range(), rb = t.range();
    double range = melee_range(&ra, &rb, f);
    // fucompp 0 against extra: equal (not unordered) and both players: range * 1.25.
    if (extra == 0.0f && me.type == 0 && t.type == 0) range = x87_mul(f, X87(range), X87(kPcRangeScale));
    double reach = store_float(x87_add(f, X87(range), X87(extra)));
    double d2 = distance_squared_3d(me, t, f);
    double reach2 = x87_mul(f, X87(reach), X87(reach));
    if (less(reach2, d2)) {
        q.token(kTokTooFar, kColorCombat);
        return false;
    }
    double b = bearing(me.y, me.x, t.y, t.x, f);
    const double heading = me.heading;
    if (less(kWrapHigh, b) && less(heading, kWrapLow)) b = heading;
    if (less(b, kWrapLow) && less(kWrapHigh, heading)) b = heading;
    double tolerance = kNpcFacing;
    if (t.type == 0 && me.type == 0) {
        tolerance = kPcFacing;
        if (less(kPcWrapHigh, b) && less(heading, kPcWrapLow)) b = heading;
        if (less(b, kPcWrapLow) && less(kPcWrapHigh, heading)) b = heading;
    }
    if (less(tolerance, std::fabs(x87_sub(f, X87(b), X87(me.heading))))) {
        q.token(kTokCantSee, kColorCombat);
        return false;
    }
    if (!q.can_see(t)) {
        q.token(kTokCantHit, kColorCombat);
        return false;
    }
    return true;
}

bool can_melee_attack_now(const MeleeNowInput& in, HitQueries& q) {
    if (in.casting && in.class_id != 8) return false;
    if (in.has_character && in.type == 0) {
        if (in.char_state == 1) {
            q.token(kTokStunned, kColorRed);
            return false;
        }
        if (in.char_state >= 2 && in.char_state <= 4) return false;
    }
    if (in.corpse) return false;
    if (in.type == 0 && in.has_character) {
        if (in.pc_state == 7 || in.pc_state == 4) return false;
        if (in.pc_vf60) return false;
    }
    return true;
}

// ---------------------------------------------------------------- DoAttack

namespace {

constexpr std::uint32_t kRangeSlot = 0xb, kHandsSlot = 0xc, kPrimarySlot = 0xd, kSecondarySlot = 0xe;
constexpr std::uint32_t kAmmoSlot = 0x16, kAbilitySlot = 0x64, kStrikeSlot = 0x65;
constexpr std::uint32_t kSkillHandToHand = 0x1c, kSkillIntimidation = 0x47;
constexpr std::int32_t kTypeBow = 5, kTypeArrow = 0x1b, kTypeHandToHand = 0x16, kTypeD = 0xd, kTypePiercing = 2;

// Whether the slot holds an item (the lookup's pointer; the reference is released at once).
bool present(AttackQueries& q, std::int32_t slot) { return q.item(slot) != kNoItem; }

// The model of `item_slot`'s item (ItemBase_GetIDFile(0x22)) in held slot `held`.
void held_model_from(AttackQueries& q, std::int32_t held, std::int32_t item_slot) {
    ItemRef item = q.item(item_slot);
    q.set_held_model(held, q.id_file(item));
}

// The slot 13 / 14 preamble's models after a ranged attack: 7 from the primary, 8 from the secondary, "0" for an
// empty slot.
void melee_models(AttackState& s, AttackQueries& q) {
    if (s.last_attack != kRangeSlot) return;
    if (present(q, kPrimarySlot)) held_model_from(q, 7, kPrimarySlot);
    else q.set_held_model(7, kNoModel);
    if (present(q, kSecondarySlot)) held_model_from(q, 8, kSecondarySlot);
    else q.set_held_model(8, kNoModel);
}

// The stance message: 12368 when +0x35c is 0x66, else 12369 (colour 13).
void stance_message(std::uint8_t stand, AttackQueries& q) { q.chat(stand == 0x66 ? kStrNoControl : kStrMustStand, kColorRed); }

// The fist's definition for a strike with `skill` (damage, its copy, type 0x16, the skill in Delay), then its
// ItemClient's refresh.
void set_fist(AttackState& s, std::int32_t damage, std::uint8_t skill, AttackQueries& q) {
    s.fist.damage = damage;
    s.fist.damage_copy = damage;
    s.fist.item_type = kTypeHandToHand;
    s.fist.delay = skill;
    q.fist_set_definition();
}

// fild dword / fstp dword; fild dword / fadd dword / fstp dword.
float fild(std::int32_t v) { return static_cast<float>(v); }
float fiadd(std::int32_t v, float to, const Fpu& f) {
    return static_cast<float>(f.add(static_cast<double>(v), static_cast<double>(to)));
}

// Slot 100 (@0x5a5546): a combat skill through the fist, on the skill's own timer.
bool do_attack_skill(AttackState& s, Spawn& me, const Spawn* target, std::uint32_t skill_arg, AttackQueries& q) {
    const std::uint8_t skill = static_cast<std::uint8_t>(skill_arg);
    if (!s.fist.present) return false;
    s.fist.damage = q.base_damage(skill_arg, target);
    s.fist.damage_copy = s.fist.damage;
    s.fist.item_type = kTypeHandToHand;
    s.fist.delay = skill;
    q.fist_set_definition();
    if (skill == 8) {
        // Backstab: a piercer in the primary (a player only; 12874).
        ItemRef primary = q.item(kPrimarySlot);
        bool piercer = primary != kNoItem && q.item_type(primary) == kTypePiercing;
        // Deliberate difference, a server extension's switch: a piercer in the secondary also does (a server that
        // takes the better hand). The original and that server's patch ask the primary
        // alone; the secondary is only read when the primary fails.
        if (!piercer && q.backstab_either_hand()) {
            ItemRef secondary = q.item(kSecondarySlot);
            piercer = secondary != kNoItem && q.item_type(secondary) == kTypePiercing;
        }
        if (!piercer && me.type == 0) {
            q.chat2(kStrNeedPiercer);
            return false;
        }
    }
    std::uint32_t index = q.combat_timer_index(skill) & 3;
    std::uint32_t delay = q.modify_attack_speed(s.combat_ticks[index], false);
    index = q.combat_timer_index(skill) & 3;
    std::uint32_t& timer = s.combat_used[index];
    const std::uint32_t now = q.now(), last = timer, elapsed = now - last;
    bool straight_to_stance = false; // a player's AltAttack skips the timer
    if (elapsed < delay) {
        if (me.type == 0 && s.alt_attack != 0) straight_to_stance = true;
        else if (me.type == 0 && skill == kSkillIntimidation) {
        } else if (me.type != 1 || me.is_attacking <= 1) {
            return false;
        }
    }
    if (!straight_to_stance && s.alt_attack == 0 && skill != kSkillIntimidation) {
        timer = now;
        std::uint32_t at = q.combat_timer_index(skill) & 3;
        std::uint32_t reuse = q.skill_reuse_time(skill);
        s.combat_ticks[at] = reuse;
        q.set_skill_timer_duration(skill, reuse);
    }
    if (me.stand_state != 100) {
        stance_message(me.stand_state, q);
        s.alt_attack = 0;
        timer = last;
        return false;
    }
    if (q.can_i_hit(target, 0.0f) == 0 && !q.hit_anyway()) {
        s.alt_attack = 0;
        timer = last;
        if (target) q.debug_cant_hit();
        return false;
    }
    q.send_attack(target, q.weapon_ref_item(kAbilitySlot, true, false), kAbilitySlot, skill);
    s.alt_attack = 0;
    return true;
}

// Slot 101 (@0x5a581f): a strike with the primary, or the fist when it is empty or not usable; no timer.
bool do_attack_strike(AttackState& s, Spawn& me, const Spawn* target, std::uint8_t skill, AttackQueries& q) {
    ItemRef primary = q.item(kPrimarySlot);
    bool fist = false;
    if (primary == kNoItem || q.can_use_item(primary) == 0) {
        set_fist(s, q.base_damage(kSkillHandToHand, target), kSkillHandToHand, q);
        fist = true;
    }
    if (me.stand_state != 100) {
        stance_message(me.stand_state, q);
        return false;
    }
    if (q.can_i_hit(target, 0.0f) == 0 && !q.hit_anyway()) {
        if (target) q.debug_cant_hit();
        return false;
    }
    q.send_attack(target, q.weapon_ref_item(kPrimarySlot, fist, false), kPrimarySlot, skill);
    return true;
}

} // namespace

bool do_attack(AttackState& s, Spawn& me, const Spawn* target, std::uint32_t slot_arg, std::uint32_t skill_arg,
               AttackQueries& q, const Fpu& f) {
    const std::uint8_t slot = static_cast<std::uint8_t>(slot_arg), skill = static_cast<std::uint8_t>(skill_arg);
    if (skill == kSkillIntimidation) {
        // Intimidation: not again within its reuse time less 250 ms (GetTickCount, unsigned).
        if (s.last_intimidate != 0) {
            std::uint32_t since = q.tick_count() - s.last_intimidate;
            if (since < static_cast<std::uint32_t>(q.reuse_time(kSkillIntimidation)) - 0xfau) return false;
        }
        s.last_intimidate = q.tick_count();
    }
    if (q.attack_blocked() && q.is_player_spawn() != 0) return false;
    if (!q.has_character()) return false;
    if (q.can_melee_attack_now(target) == 0) return false;
    if (me.type == 0 && q.total_effect(0x138) != 0) {
        // Sanctuary (SPA 312) ends with an attack.
        for (std::uint32_t a = q.spell_affect(0x138); a != 0; a = q.spell_affect(0x138)) q.remove_affect(a);
    }
    q.vf_c8();
    if (q.can_use_combat_skill(slot, skill) == 0) return false;

    // The slot preambles (jump table 0x5a6374): the timer sampled (AttackTimer_Sample: __EQGetTime), the models.
    std::uint32_t* timer;
    switch (slot) {
    case kPrimarySlot:
        timer = &s.last_primary;
        break;
    case kSecondarySlot:
        timer = &s.last_secondary;
        break;
    case kRangeSlot:
        timer = &s.last_primary;
        break;
    case kAbilitySlot:
        return do_attack_skill(s, me, target, skill_arg, q);
    case kStrikeSlot:
        return do_attack_strike(s, me, target, skill, q);
    default:
        return false;
    }
    const std::uint32_t now = q.now(), last = *timer, elapsed = now - last;
    if (slot == kRangeSlot) {
        if (s.last_attack != kRangeSlot) {
            q.set_held_model(7, kNoModel);
            q.set_held_model(8, kNoModel);
            if (present(q, kRangeSlot)) held_model_from(q, 8, kRangeSlot);
        }
        if (q.auto_fire()) {
            q.set_auto_range_attack(false);
            return false;
        }
    } else {
        melee_models(s, q);
    }
    s.last_attack = slot;

    // The weapon (@0x5a59c3): the slot's item when it is a weapon the player can use; else for the range slot
    // nothing (9150), for the others the fist (the profile's damage, else GetSkillBaseDamage(28)).
    ItemRef item = q.item(slot);
    bool usable = false;
    if (item != kNoItem) {
        if (q.is_weapon(item) == 0) return false;
        usable = q.can_use_item(item) != 0;
    }
    bool fist = false;
    if (!usable) {
        if (slot == kRangeSlot) {
            q.chat(kStrNoRanged, kColorCombat);
        } else {
            std::int32_t damage = q.profile().fist_damage;
            if (damage <= 0) damage = q.base_damage(kSkillHandToHand, target);
            set_fist(s, damage, kSkillHandToHand, q);
            fist = true;
        }
    }
    // @0x5a5b0c: a weapon, a type 0xd or a type 0x16 item, else nothing to swing (true).
    auto weapon = [&] { return q.weapon_ref_item(slot, fist, true); };
    if (q.weapon_ref_item(slot, fist, false) == kNoItem) return true;
    if (q.is_weapon(weapon()) == 0 && q.item_type(weapon()) != kTypeD && q.item_type(weapon()) != kTypeHandToHand)
        return true;

    // The delay: a hand-to-hand item's skill reuse, GetHandToHandDelay's, or the profile's for the fist; else
    // Delay * 100. Then ModifyAttackSpeed (a bow's quiver).
    std::uint32_t speed;
    if (q.item_type(weapon()) == kTypeHandToHand) {
        std::int32_t delay = q.reuse_time(q.item_delay(weapon()));
        q.hand_to_hand_delay(delay, q.item(kHandsSlot));
        speed = q.item_delay(weapon());
        bool from_profile = false;
        if (fist && speed == kSkillHandToHand) {
            speed = static_cast<std::uint32_t>(q.profile().fist_delay);
            from_profile = static_cast<std::int32_t>(speed) > 0;
        }
        if (!from_profile) speed = static_cast<std::uint32_t>(delay);
    } else {
        speed = q.item_delay(weapon()) * 100u;
    }
    const std::uint32_t delay = q.modify_attack_speed(speed, q.item_type(weapon()) == kTypeBow);

    // Too early (@0x5a5dc8): no swing, unless the target has +0x251 set, an NPC is in its attack (+0x21c above 1)
    // or +0x364 (DoSpecialMelee) is set; a waiting NPC's +0x21c below 2 becomes 0.
    if ((!target || target->always_hit == 0) && elapsed < delay) {
        if (!(me.type == 1 && me.is_attacking > 1) && me.do_special_melee == 0) {
            if (me.type == 1 && me.is_attacking < 2) me.is_attacking = 0;
            return true;
        }
    }
    // The cadence (@0x5a5e2e): last + delay while within one more delay of it, else now.
    *timer = elapsed >= delay && delay != 0 && elapsed < delay + delay ? *timer + delay : now;

    if (me.type != 0) {
        if (me.type == 1 && me.is_attacking < 2) me.is_attacking = 1;
    } else {
        if (me.stand_state != 100) {
            stance_message(me.stand_state, q);
            return false;
        }
        float range = 0.0f;
        if (slot == kRangeSlot) {
            // fcomp 30.0f: closer than 30 is too close (12698); NaN is not.
            if (target && less(q.player_distance(target), kRangedTooClose)) {
                q.token(kTokTooClose, 0xf);
                return false;
            }
            // The reach: a bow's range plus its arrows' (ammo slot 22, type 0x1b); else the ranged item's or the
            // ammo slot's (a non-weapon there: 9150).
            if (q.item_type(weapon()) == kTypeBow) {
                range = fild(q.item_range(weapon()));
                if (present(q, kAmmoSlot) && q.item_type(q.item(kAmmoSlot)) == kTypeArrow)
                    range = fiadd(q.item_range(q.item(kAmmoSlot)), range, f);
            } else {
                std::int32_t from = present(q, kRangeSlot) ? kRangeSlot : present(q, kAmmoSlot) ? kAmmoSlot : 0;
                if (from != 0) {
                    if (q.is_weapon(q.item(from)) == 0) {
                        q.chat(kStrNoRanged, kColorCombat);
                        return false;
                    }
                    range = fild(q.item_range(q.item(from)));
                }
            }
        }
        if (q.can_i_hit(target, range) == 0 && !q.hit_anyway()) {
            if (target) q.debug_cant_hit();
            if (slot == kRangeSlot) *timer = last;
            return false;
        }
        if (target && me.body_wet != target->body_wet) {
            q.token(kTokCantHit, kColorCombat);
            return false;
        }
    }
    // The attack (@0x5a6211).
    if (slot == kPrimarySlot) {
        q.send_attack(target, q.weapon_ref_item(slot, fist, false), kPrimarySlot, skill);
        if (q.check_double_attack(target, kPrimarySlot) == 1)
            q.send_attack(target, q.weapon_ref_item(slot, fist, false), kPrimarySlot, skill);
    } else if (slot == kSecondarySlot) {
        if (q.check_dual_wield(target) == 1) {
            q.send_attack(target, q.weapon_ref_item(slot, fist, false), kSecondarySlot, skill);
            if ((q.profile().double_attack > 0x95 || q.total_effect(0xe1) != 0) &&
                q.check_double_attack(target, kSecondarySlot) == 1)
                q.send_attack(target, q.weapon_ref_item(slot, fist, false), kSecondarySlot, skill);
        }
    } else {
        q.ranged_attack(target);
    }
    return true;
}

// ---------------------------------------------------------------- ranged attacks

std::uint8_t handle_ammo(const Spawn& me, AttackQueries& q) {
    constexpr std::int32_t kTypeThrown = 0x13, kTypeBow6 = 6, kTypeAmmo6 = 0x1c;
    constexpr std::uint32_t kStrNoAmmo = 0x324b;
    const ItemRef range = q.item(kRangeSlot);
    const ItemRef ammo = q.item(kAmmoSlot);
    // Every check asks GetItemType again, as the original does.
    if (q.item_type(range) != kTypeThrown && q.item_type(range) != kTypeBow && q.item_type(range) != kTypeBow6)
        return q.item_range(range) != 0 ? 0 : 2;
    const std::int32_t type = q.item_type(range);
    if (ammo == kNoItem) {
        if (type == kTypeThrown && q.item_stack(range) > 1 && me.type == 0) return 2;
        if (q.item_type(range) == kTypeThrown && q.item_stack(range) == 1) return 0;
    } else {
        if (type == kTypeThrown) {
            // The ammo's id, then the range item's, as written (an operator's order is the compiler's).
            const auto ammo_id = q.item_id(ammo);
            if (ammo_id == q.item_id(range)) return q.item_stack(ammo) > 0 ? 1 : 0;
        }
        if ((q.item_type(range) == kTypeBow && q.item_type(ammo) == kTypeArrow) ||
            (q.item_type(range) == kTypeBow6 && q.item_type(ammo) == kTypeAmmo6)) {
            if (q.item_stack(ammo) > 0) return 1;
        } else if (q.item_type(range) == kTypeThrown && q.item_stack(ammo) > 0) {
            return 2;
        }
    }
    q.chat2(kStrNoAmmo);
    return 2;
}

bool do_ranged_attack(const Spawn& me, const Spawn* target, AttackQueries& q) {
    if (!present(q, kRangeSlot)) return false;
    const std::uint8_t ammo = handle_ammo(me, q);
    // The held models after a shot: 8 from the range item, else slot 14's, else "0".
    auto model_8 = [&] {
        if (present(q, kRangeSlot)) held_model_from(q, 8, kRangeSlot);
        else if (present(q, kSecondarySlot)) held_model_from(q, 8, kSecondarySlot);
        else q.set_held_model(8, kNoModel);
    };
    if (ammo == 0) {
        if (!present(q, kRangeSlot)) return true;
        if (!target) return false;
        q.send_attack(target, q.weapon_ref_item(kRangeSlot, false, false), kRangeSlot, 0xffffffffu);
        q.set_held_model(8, kNoModel);
        return true;
    }
    if (ammo == 1) {
        if (!present(q, kAmmoSlot)) return true;
        const ItemRef shot = q.item(kAmmoSlot);
        if (!target) return false;
        q.send_attack(target, shot, kRangeSlot, 0xffffffffu);
        q.set_held_model(7, kNoModel);
        model_8();
        return true;
    }
    if (ammo != 2) return true;
    if (!present(q, kRangeSlot)) return true;
    if (q.item_type(q.item(kRangeSlot)) != 0x13) return true;
    if (!target) return false;
    q.send_attack(target, q.item(kRangeSlot), kRangeSlot, 0xffffffffu);
    if (present(q, kPrimarySlot)) held_model_from(q, 7, kPrimarySlot);
    else q.set_held_model(7, kNoModel);
    model_8();
    return true;
}

// ---------------------------------------------------------------- DoPassageOfTime

void attack_on_reset(AttackState& s, bool attack_on, AttackQueries& q) {
    if (!attack_on || !s.primary_ready) return;
    s.primary_ready = false;
    s.secondary_ready = false;
    s.last_primary = q.now();
    s.last_secondary = q.now();
    s.last_ranged = q.now();
}

void swing_ready(AttackState& s, std::uint32_t now, AttackQueries& q) {
    // A slot's weapon: none (or the fist) when its delay is below 1 or the player cannot use it.
    auto weapon = [&](std::int32_t slot, bool fist_fallback) {
        ItemRef w = q.item(slot);
        if (w != kNoItem && (q.item_delay(w) < 1 || q.can_use_item(w) == 0)) w = kNoItem;
        if (w == kNoItem && fist_fallback) w = kFistItem;
        return w;
    };
    // IsWeapon, or item type 0xd or 0x16.
    auto attack_weapon = [&](ItemRef w) {
        return w != kNoItem && (q.is_weapon(w) != 0 || q.item_type(w) == kTypeD || q.item_type(w) == kTypeHandToHand);
    };
    auto delay_of = [&](ItemRef w, bool bow) {
        std::uint32_t raw = q.item_type(w) == kTypeHandToHand ? static_cast<std::uint32_t>(q.reuse_time(q.item_delay(w)))
                                                              : q.item_delay(w) * 100u;
        return q.modify_attack_speed(raw, bow);
    };
    ItemRef primary = weapon(kPrimarySlot, true);
    std::uint32_t primary_delay = attack_weapon(primary) ? delay_of(primary, false) : 0;
    ItemRef ranged = weapon(kRangeSlot, false);
    std::uint32_t ranged_delay = attack_weapon(ranged) ? delay_of(ranged, q.item_type(ranged) == kTypeBow) : 0;
    std::uint32_t elapsed = now - s.last_primary;
    if (s.last_attack == kRangeSlot && ranged_delay != 0) primary_delay = ranged_delay;
    if (elapsed >= primary_delay && !s.primary_ready) s.primary_ready = true;
    // (The skill timers' loop runs here: SkillTimers::update.)
    ItemRef secondary = weapon(kSecondarySlot, true);
    std::uint32_t secondary_delay = attack_weapon(secondary) ? delay_of(secondary, false) : primary_delay;
    if (now - s.last_secondary >= secondary_delay) s.secondary_ready = true;
}

} // namespace hailstone::game::mechanics::melee
