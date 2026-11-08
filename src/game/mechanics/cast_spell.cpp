// CharacterZoneClient::CastSpell @0x43a830 (cast_spell.h). Addresses in the
// comments are the original's.

#include "game/mechanics/cast_spell.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace hailstone::game::mechanics::cast {

namespace {

constexpr std::int32_t kChatSpells = 0x121; // the spell message colour (289)
constexpr float kRangeCap = 5.0f;          // 0x9c58a0
constexpr float kNpcRange = 1.2f;          // 0x9c58a8
constexpr float kNpcMinRange = 500.0f;     // 0x9c5898
constexpr float kCazicRange = 10000.0f;    // 0x9c58a4 (spell 0x3d6)
constexpr float kAeHeight = 39.99f;        // 0x9c589c
constexpr float kMountRoom = 3.0f;         // 0x9c3920

double fl(std::uint32_t bits) { return static_cast<double>(std::bit_cast<float>(bits)); }
std::uint32_t bits_of(float f) { return std::bit_cast<std::uint32_t>(f); }

// fld dword / fstp dword: a copy through the FPU quiets a signalling NaN.
std::uint32_t fpu_copy(std::uint32_t bits) {
    float f = std::bit_cast<float>(bits);
    if (std::isnan(f)) return bits | 0x00400000u;
    return bits;
}

// The SpellCache total CastSpell builds for an SPA (@0x43ab05, @0x43ad52, @0x43b689): buffs + AAs, plus items for
// a player, a pet or a mercenary.
std::int32_t cached_total(const State& s, Queries& q, std::int32_t spa) {
    if (!s.effects_cached) q.recache_spell_effects();
    // add: wraps (the AA term is the server's bases, any size).
    auto t = static_cast<std::uint32_t>(q.cached_buff_effect(spa, 0));
    t += static_cast<std::uint32_t>(q.cached_aa_effect(spa, 0));
    const Spawn* me = s.caster;
    if (me && (me->type == 0 || me->master_id != 0 || me->mercenary != 0))
        t += static_cast<std::uint32_t>(q.cached_item_effect(spa, 0));
    return static_cast<std::int32_t>(t);
}

// A PC caster's range: Range + GetFocusRangeModifier (fiadd), stored as a float, at most 5 x Range (compared at the
// x87's precision before the store) (@0x43b7bf and its copies).
std::uint32_t focus_range(const Fpu& fpu, const Spell& sp, Queries& q) {
    std::int32_t focus = q.focus_range_modifier(sp);
    double range = fl(sp.range);
    double sum = fpu.add(range, static_cast<double>(focus));
    double capped = fpu.mul(range, kRangeCap);
    if (sum > capped) return bits_of(static_cast<float>(capped));
    return bits_of(static_cast<float>(sum));
}

// An NPC caster's: Range x 1.2 (@0x43b7a5).
std::uint32_t npc_range(const Fpu& fpu, const Spell& sp) {
    return bits_of(static_cast<float>(fpu.mul(fl(sp.range), kNpcRange)));
}

// @0x442080: the squared distance from the spawn's position to a point.
double squared_distance(const Fpu& fpu, const Spawn& me, const std::array<std::uint32_t, 3>& p) {
    double dy = fpu.sub(fl(me.y), fl(p[0]));
    double dx = fpu.sub(fl(me.x), fl(p[1]));
    double dz = fpu.sub(fl(me.z), fl(p[2]));
    double sum = fpu.add(fpu.mul(dx, dx), fpu.mul(dy, dy));
    return fpu.add(sum, fpu.mul(dz, dz));
}

void put32(std::array<std::uint8_t, 0x2c>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}

} // namespace

std::array<std::uint8_t, 0x2c> Packet::bytes() const {
    std::array<std::uint8_t, 0x2c> b{};
    put32(b, 0x00, slot);
    put32(b, 0x04, static_cast<std::uint32_t>(spell_id));
    for (std::size_t k = 0; k < 3; ++k) put32(b, 0x08 + 4 * k, item_location[k]);
    put32(b, 0x14, target_id);
    put32(b, 0x18, crc);
    b[0x1c] = has_position;
    for (std::size_t k = 0; k < 3; ++k) put32(b, 0x20 + 4 * k, position[k]);
    return b;
}

bool spell_affects(const Spell& s, std::int32_t spa) {
    for (std::int32_t v : s.spa)
        if (v == spa) return true;
    return false;
}

std::uint8_t spell_level_needed(const Spell& s, std::uint32_t class_id) {
    if (class_id - 1u <= 0x22u) return s.class_level[class_id];
    return s.class_level[1];
}

bool bard_cast_bard(const Spell* s, std::uint32_t class_id) {
    if (!s || class_id != 8 || s->is_skill) return false;
    std::uint8_t level = spell_level_needed(*s, 8);
    return level != 0 && level < 0xff;
}

bool target_type_needs_target(std::uint8_t t) {
    switch (t) {
    case 5: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
    case 19: case 20: case 21: case 22: case 23: case 27: case 28: case 30: case 34: case 35: case 38: case 39:
    case 43: case 46: case 47:
        return true;
    default:
        return false;
    }
}

std::uint8_t cast_spell(State& s, const Args& a, Queries& q, std::optional<Packet>* sent) {
    const Fpu fpu(s.precision);
    Spawn* me = s.caster;
    if (!me) return 0;
    if (me == s.local_player && s.local_block) return 0;

    // The spell, and the local PC's rank swap (@0x43a8b9..0x43a95f).
    const Spell* sp = nullptr;
    if (q.is_valid_spell_id(a.spell_id)) sp = q.spell_by_id(a.spell_id);
    if (s.is_local_pc && !q.character_virtual_90()) {
        if (!sp) return 0;
        if (sp->group > 0 && a.slot <= 0xb) {
            std::int32_t rank = q.character_rank(3);
            if (rank != -1 && rank < sp->rank) sp = q.spell_by_group_and_rank(sp->group, sp->subgroup, rank, 1);
        }
    }
    if (!sp) return 0;
    const Spell& spell = *sp;
    const std::int32_t id = spell.id; // the argument's slot now holds the record's id (@0x43a9e0)
    const bool has_item = a.item != nullptr;
    const bool loud = !has_item;      // [esp+0x1c] (@0x43a9ed): item casts show no refusal text
    const std::uint8_t tt = spell.target_type;

    // Ends the call with a message (token_string_to_client), or silently.
    auto refuse = [&](std::uint32_t eqstr, std::int32_t color = kChatSpells, std::int32_t arg4 = 0) -> std::uint8_t {
        q.message(eqstr, color, arg4);
        return 0;
    };
    auto refuse_loud = [&](std::uint32_t eqstr, std::int32_t color = kChatSpells) -> std::uint8_t {
        if (loud) q.message(eqstr, color, 0);
        return 0;
    };
    auto refuse_chat = [&](std::uint32_t eqstr, std::int32_t color = kChatSpells) -> std::uint8_t {
        q.dsp_chat(eqstr, color, 1, 1);
        return 0;
    };
    auto no_target = [&]() -> std::uint8_t { return refuse_loud(spell.is_skill ? 0x16c1 : 0xd6); };

    // IsOkToTransact (@0x43a9f2).
    if (!q.ok_to_transact()) q.pending_request_report(0x3e);
    if (!q.ok_to_transact() && !has_item) return refuse_chat(spell.is_skill ? 0x16bb : 0x3098);

    // Standing (@0x43aa4f).
    if (!spell.cast_not_standing) {
        if (me->stand_state != 100) {
            if (me->stand_state == 110 && s.auto_stand) q.change_position(*me, 100);
            if (me->stand_state != 100) {
                if (loud) q.message(spell.is_skill ? 300 : 0x3099, kChatSpells, 0);
                if (me->stand_state != 0x66 || !loud) return 0;
                return refuse(0x3050);
            }
        }
        if (cached_total(s, q, 40) > 0) return refuse_loud(0xae); // invulnerable
        if (me->type == 0 && s.stunned == 1) return refuse_loud(spell.is_skill ? 0x16bc : 0xaf);
    }

    // The combat state (@0x43abcb); these messages show for item casts too.
    if (q.pc_zone()) {
        q.pc_zone();
        bool combat = q.in_combat();
        if (!spell.can_cast_in_combat && combat) return refuse(spell.is_skill ? 0x23e8 : 0x23e6);
        if (!spell.can_cast_out_of_combat && !combat) return refuse(spell.is_skill ? 0x23ea : 0x23e7);
        if (spell.only_during_fast_regen) {
            q.pc_zone();
            if (!q.resting_for_fast_regen()) return refuse(spell.is_skill ? 0x193d : 0x193c);
        }
    }

    // The item's recast timer (@0x43acab).
    if (has_item && q.item_spell_recast_time(0) > 0 && a.item_cast_type == 0 && me->type == 0) {
        q.pc_zone();
        std::int32_t timer = q.item_recast_timer();
        if (q.fast_time() < timer) return refuse(0xec, kChatSpells, a.item_cast_type);
    }

    // Target type 45 without a free-target tracker: place the indicator first (@0x43ad0d).
    if (tt == 0x2d && s.free_target_tracker == 0) {
        q.create_target_indicator(a.slot, spell, a.item_cast_type);
        return 0;
    }

    // Sanctuary (SPA 312) ends (@0x43ad42).
    if (cached_total(s, q, 312) != 0) {
        bool keep = spell.spell_type != 0 &&
                    ((target_type_needs_target(tt) && s.target == me) || (!target_type_needs_target(tt) && tt == 6));
        if (!keep) {
            for (;;) {
                Affect* found = nullptr;
                for (std::int32_t i = 0; !found && i < static_cast<std::int32_t>(q.max_effects()); ++i) {
                    Affect* e = q.effect(i);
                    if (!e->active) continue;
                    const Spell* bs = q.spell_by_id(e->spell_id);
                    if (!bs) continue;
                    for (std::int32_t v : bs->spa) {
                        if (v == 0xfe) break;
                        if (v == 0x138) {
                            found = e;
                            break;
                        }
                    }
                }
                if (!found) break;
                q.remove_affect(*found, 1, 0);
            }
        }
    }

    // Invisibility, sneak attacks (@0x43af00).
    if (!(spell.sneak || spell.cast_not_standing) && loud) {
        q.make_me_visible(0xb0, 1);
    } else if (spell.sneak && me->hide_mode <= 0) {
        return refuse_loud(0x2418);
    }

    if (has_item) {
        // An item cast (@0x43af34).
        s.character_468 = 0;
        if (q.item_type() == 0x14) return 0;
        if (me->casting.spell_id != -1) {
            if (!q.is_valid_spell_id(me->casting.spell_id)) return 0;
            const Spell* casting = q.spell_by_id(me->casting.spell_id);
            if (!q.bard_cast_bard(casting, s.class_id)) return 0;
        }
        if (a.item_cast_type == 0) {
            if (me->type == 0) {
                std::int32_t required = q.item_spell_required_level(0);
                if (q.character_level() < required) return refuse_loud(0x88, 0xd);
                if (q.item_is_attuneable() != 0 && a.item->attuned == 0) return refuse_loud(0x1da2, 0xd);
                std::int32_t effect = q.item_spell_effect_type(0);
                if (effect == 4) { // must be equipped
                    bool worn = false;
                    for (std::int32_t slot = 0; slot <= 0x16; ++slot)
                        if (q.inventory_item(slot) == a.item) worn = true;
                    if (!worn) return refuse_loud(0xb3, 0xd);
                    if (!q.can_use_item(1, loud)) return 0;
                } else if (effect == 5) {
                    if (!q.can_use_item(1, loud)) return 0;
                }
            }
            if (a.item->charges == 0) return refuse_loud(0xb6, 0x111);
        }
        if (spell_affects(spell, 0x19) && s.spa25_block) return refuse(0x69);
    } else {
        // A spell (@0x43b1aa).
        if (me->casting.spell_id != -1 && s.class_id != 8) return refuse_chat(0x309a);
        if (a.slot > 0xe) return 0;
        if (a.slot <= 0xb) {
            std::int32_t need = spell_level_needed(spell, s.class_id);
            if (q.character_level() < need) return refuse_chat(0x309b);
        }
        if (spell_affects(spell, 0x20) && spell.id != 0xad4 && q.pc_item(9)) return refuse_chat(0xc79, 0xf);
        if (q.character_virtual_34() == 0 && me->type == 0) s.character_468 = 0;
    }

    // @0x43b315.
    if (spell_affects(spell, 0x98) && tt != 6 && s.target == me) return no_target();
    if ((id == 0x5de || id == 0xa9c) &&
        static_cast<std::int32_t>(static_cast<std::uint32_t>(spell.base0) + static_cast<std::uint32_t>(me->hp_max)) <= 0)
        return refuse_loud(0x107);
    if (tt == 0x0e || tt == 0x26) s.target = q.pet_spawn();
    auto owner_of_npc = [&]() -> Spawn* {
        if (me->type == 1 && me->master_id != 0) return q.spawn_by_id(me->master_id);
        return nullptr;
    };
    if (tt == 0x2f) s.target = owner_of_npc();
    if (tt == 0x2d) {
        if (!a.position) return 0;
        if (!q.can_see_position(*me, *a.position)) return refuse_chat(0x6c);
    }
    // Line of sight (@0x43b413).
    if (target_type_needs_target(tt) && s.target && me->type == 0 &&
        (spell.spell_type != 1 || spell_affects(spell, 0x1b))) {
        if (!q.can_see(*me, *s.target)) return refuse_chat(0x6c);
    }
    // A mount needs three heights of room (@0x43b470).
    if (me == s.local_player && spell_affects(spell, 0x71)) {
        double room = fpu.sub(fl(me->ceiling), fl(me->floor_height));
        double need = fpu.mul(fl(me->height), kMountRoom);
        if (need > room) return refuse_chat(0xfe3);
    }
    // The PvP deity rule (@0x43b4dd).
    if (s.zone_type == 4 && me->type == 0 && s.target && s.target->type == 0 && spell.spell_type == 1 &&
        !q.spawn_virtual_dc(*me) && tt == 5 && !spell_affects(spell, 0x1b) &&
        q.not_on_same_deity(*s.target, *me, spell) != 0)
        return refuse_loud(0xd2);
    // Bards (@0x43b544).
    if (q.bard_cast_bard(&spell, s.class_id)) {
        if (s.state_104 == 0x73 || s.state_104 == 0x78) return refuse_loud(0xd4);
    } else if (!spell.cast_not_standing && s.state_104 != 100) {
        return refuse_loud(0xd5);
    }
    // PCNPCOnlyFlag (@0x43b5d0).
    if (spell.pcnpc_only != 0 && tt != 0x2d) {
        const Spawn* t = s.target;
        if (spell.pcnpc_only == 1 && t && t->type != 0 && !t->mercenary) return refuse_loud(0x16e8);
        if (spell.pcnpc_only == 2 && t && !(t->type == 1 && !t->mercenary)) return refuse_loud(0x16f5);
    }
    // Mana and endurance (@0x43b657).
    if (!s.skip_cost && !has_item) {
        std::int32_t mana = spell.mana;
        if (cached_total(s, q, 0x144) == 0 && q.cur_mana(1) < mana) return refuse_loud(199);
        if (spell.endurance != 0 && q.cur_endurance(1) < spell.endurance) return refuse_loud(0x16ad);
    }

    // The target-type switch (@0x43b747); a case that passes goes on to the +0x178 check (@0x43c31c).
    std::uint8_t refused = 0;
    bool done = false; // a refusal decided the call
    auto finish = [&](std::uint8_t r) {
        refused = r;
        done = true;
    };
    switch (tt) {
    case 8: case 21: case 22: case 23: case 28: { // targeted AE (@0x43b765)
        if (!s.target) {
            finish(refuse_loud(0xd6));
            break;
        }
        if (!q.spell_allowed(s.target, spell.req_association)) {
            finish(refuse_loud(0x23d3));
            break;
        }
        std::uint32_t range;
        if (me->type == 1) {
            range = npc_range(fpu, spell);
            if (id == 0x3d6) range = bits_of(kCazicRange);
        } else {
            range = focus_range(fpu, spell, q);
        }
        Spawn& t = *s.target;
        double dz = fpu.sub(fpu.sub(fl(t.z), fl(t.height)), fpu.sub(fl(me->z), fl(me->height)));
        bool too_high = std::fabs(dz) > static_cast<double>(kAeHeight);
        double dist = fl(q.player_distance(t, *me, 0));
        if (!(fl(range) < dist) && !(dist < fl(spell.min_range)) && !too_high) {
            break;
        }
        if (loud) q.message(dist < fl(spell.min_range) ? 0x11fa : 100, kChatSpells, 0);
        finish(0);
        break;
    }
    case 45: { // the free target (@0x43b89c)
        if (!a.position) {
            finish(refuse_loud(0xd6));
            break;
        }
        std::uint32_t range = me->type == 1 ? npc_range(fpu, spell) : focus_range(fpu, spell, q);
        double d2 = squared_distance(fpu, *me, *a.position);
        double r2 = fpu.mul(fl(range), fl(range));
        double m2 = fpu.mul(fl(spell.min_range), fl(spell.min_range));
        if (m2 > r2) {
            finish(refuse_loud(0x11fa));
            break;
        }
        if (r2 < d2) {
            finish(refuse_loud(100));
            break;
        }
        break;
    }
    case 0: case 1: { // (@0x43b96b)
        if (!s.target) {
            finish(refuse_loud(0xd6));
            break;
        }
        if (!q.spell_allowed(s.target, spell.req_association)) {
            finish(refuse_loud(0x23d3));
            break;
        }
        if (spell.travel_type != 3) {
            break;
        }
        std::uint32_t range;
        if (me->type == 1) {
            range = npc_range(fpu, spell);
            if (id == 0x3d6) range = bits_of(kCazicRange);
        } else {
            range = focus_range(fpu, spell, q);
        }
        double dist = fl(q.player_distance(*s.target, *me, 0));
        if (loud) { // an item cast skips the range here
            if (dist < fl(spell.min_range)) {
                finish(refuse(0x11fa));
                break;
            }
            if (dist > fl(range)) {
                finish(refuse(100));
                break;
            }
        }
        break;
    }
    case 6: { // (@0x43baa6) SPA 91 (summon corpse): a grouped player, or vtable +0xdc
        if (spell_affects(spell, 0x5b)) {
            if (me->type != 0) {
                finish(0);
                break;
            }
            bool allowed = false;
            if (s.target && s.target->type == 0) {
                Handle pc = q.spawn_character_pc(*s.target);
                allowed = q.spawn_virtual_dc(*me) || !s.target->has_character || q.is_self_or_group_member(pc, nullptr);
            }
            if (!allowed) {
                finish(refuse_loud(0xd7));
                break;
            }
        }
        if (!q.spell_allowed(s.target, spell.req_association)) {
            finish(refuse_loud(0x23d3));
            break;
        }
        break;
    }
    case 7: { // (@0x43c2cf) the slot 0x21 item
        if (has_item) {
            finish(0);
            break;
        }
        if (!q.inventory_item(0x21)) {
            finish(refuse_loud(0xe9));
            break;
        }
        break;
    }
    case 14: case 38:
    case 5: case 9: case 10: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 20: case 27: case 34:
    case 35: case 39: case 43: case 47: { // single targets (@0x43bb7d / @0x43bb8c)
        if (tt == 14 || tt == 38) s.target = q.pet_spawn();
        if (tt == 0x2f) s.target = owner_of_npc();
        if (tt == 0x26 && s.target && !(s.target->type == 1 && s.target->summoned)) s.target = nullptr;
        if (has_item && q.item_base_type() == 0 && q.item_type() == 0x15 && q.item_spell_id(0) >= 2)
            s.target = s.local_player;
        if (!s.target) {
            finish(no_target());
            break;
        }
        std::uint32_t range;
        if (me->type == 1) {
            double r = fpu.mul(fl(spell.range), kNpcRange);
            range = bits_of(static_cast<float>(r));
            if (static_cast<double>(kNpcMinRange) > r) range = bits_of(kNpcMinRange);
            if (id == 0x3d6) range = bits_of(kCazicRange);
        } else {
            range = focus_range(fpu, spell, q);
        }
        double dist = fl(q.player_distance(*s.target, *me, 0));
        if (dist < fl(spell.min_range)) {
            finish(refuse_loud(0x11fa));
            break;
        }
        if (dist > fl(range)) {
            finish(refuse_loud(100));
            break;
        }
        if (!q.spell_allowed(s.target, spell.req_association)) {
            finish(refuse_loud(0x23d3));
            break;
        }
        // The body-type switch (@0x43bd5c).
        Spawn& t = *s.target;
        auto prop = [&](std::int32_t p) { return q.has_property(t, p, 0, 0); };
        auto body = [&](std::initializer_list<std::int32_t> props, std::uint32_t eqstr) {
            if (me->type != 0) return true;
            for (std::int32_t p : props)
                if (prop(p)) return true;
            if (me->type == 0 && loud) q.message(eqstr, kChatSpells, 0);
            return false;
        };
        auto body_any_caster = [&](std::int32_t p, std::uint32_t eqstr) {
            if (prop(p)) return true;
            if (me->type == 0 && loud) q.message(eqstr, kChatSpells, 0);
            return false;
        };
        auto not_self = [&]() {
            if (s.target != me) return true;
            if (loud) q.message(0xe0, kChatSpells, 0);
            return false;
        };
        bool pass = true;
        switch (tt) {
        case 11: case 22: pass = body({0x18, 0x1b, 8, 0x1c}, 0xd8); break;   // summoned
        case 14:
            s.target = q.pet_spawn();
            if (!s.target && loud) q.message(0xd9, kChatSpells, 0);
            pass = s.target != nullptr;
            break;
        case 38:
            s.target = q.pet_spawn();
            if (s.target && !(s.target->type == 1 && s.target->summoned)) s.target = nullptr;
            if (!s.target && loud) q.message(0xd9, kChatSpells, 0);
            pass = s.target != nullptr;
            break;
        case 12:
            pass = q.target_type_12_check(t);
            if (!pass) q.message(0xda, kChatSpells, 0);
            break;
        case 39:
            pass = t.type == 0;
            if (!pass) q.message(0x16e8, kChatSpells, 0);
            break;
        case 10: case 21: pass = body({3, 8, 0xc}, 0xdb); break;             // undead
        case 16: pass = body({0x19}, 0xdc); break;                             // plant
        case 17: pass = body_any_caster(9, 0xe1); break;                       // giant
        case 18: pass = body_any_caster(0x1e, 0xe2); break;                    // dragon
        case 19: pass = body_any_caster(0xa, 0xe3); break;
        case 35: pass = body_any_caster(0x22, 0x23ff); break;                  // Muramite
        case 15: // a corpse
            if (me->type != 0) break;
            pass = t.type >= 2;
            if (!pass && loud) q.message(0xdd, kChatSpells, 0);
            break;
        case 9: case 23: pass = body({0x15}, 0xde); break;                     // animal
        case 27: case 28: pass = body({0x16}, 0xdf); break;                    // insect
        case 34:
            if (me->type == 0 && !prop(0x21)) {
                if (loud) q.message(0x23ba, kChatSpells, 0);
                pass = false;
                break;
            }
            pass = not_self();
            break;
        case 13: case 20: pass = not_self(); break;
        case 43: { // the group
            if (me->type != 0) break;
            if (q.is_self_or_group_member(q.pc_zone(), &t)) break;
            if (t.type == 1 && t.master_id != 0) {
                Spawn* owner = q.spawn_by_id(t.master_id);
                if (owner && owner->type == 0 && q.is_self_or_group_member(q.pc_zone(), owner)) break;
            }
            if (loud) q.message(0xc4, kChatSpells, 0);
            pass = false;
            break;
        }
        default: break;
        }
        if (!pass) {
            finish(0);
            break;
        }
        break;
    }
    default: // no target needed
        break;
    }
    if (done) return refused;
    // +0x178 refused on yourself (@0x43c31c).
    if (spell.no_self != 0 && s.target == me) return refuse_loud(0xd2);

    // Committed (@0x43c372).
    Packet pkt;
    bool init_path = false; // LaunchSpellData_Init zeroed the packet's position
    bool quiet = s.quiet || s.demo;
    bool to_send = true;
    if (!s.skip_cost && !has_item && (me->type == 0 || me->mercenary)) {
        (void)spell_level_needed(spell, s.class_id); // the result is thrown away (an offline remnant)
        q.largest_cached_effect(0x109, 0);
    }
    if (!has_item) {
        std::uint32_t base = spell.cast_time;
        std::int32_t mod = q.casting_time_modifier(spell);
        // The original's 32-bit add wraps; do it in unsigned arithmetic so the wrap is defined here too.
        mod = static_cast<std::int32_t>(static_cast<std::uint32_t>(q.focus_casting_time_modifier(spell, 0)) +
                                        static_cast<std::uint32_t>(mod));
        std::uint32_t t = (mod <= 0 && static_cast<std::uint32_t>(-static_cast<std::int64_t>(mod)) >= base)
                              ? 0u
                              : base + static_cast<std::uint32_t>(mod);
        if (t < base / 2) t = base / 2;
        if (quiet) q.start_casting(id, static_cast<std::uint16_t>(me->spawn_id), t);
        if (t <= 100) {
            init_path = true;
        } else {
            q.launch_spell_data_reset();
            CastingData& c = me->casting;
            c.eta = s.now + t;
            c.spell_id = id;
            c.slot = a.slot;
            c.y = fpu_copy(me->y);
            c.x = fpu_copy(me->x);
            if (s.target) c.target_id = s.target->spawn_id;
            if (a.position) {
                for (std::size_t k = 0; k < 3; ++k) c.target_position[k] = fpu_copy((*a.position)[k]);
                c.free_target = 1;
            }
            s.last_beneficial = spell.spell_type;
        }
        if (quiet) to_send = false;
    } else {
        init_path = true;
        q.item_id();
        std::int32_t item_time = q.item_spell_cast_time(a.item_cast_type);
        if (item_time <= 100 || me->type == 1) {
            if (a.item_cast_type == 0 && q.item_spell_recast_time(0) > 0 && me->type == 0) {
                std::int32_t type = q.item_spell_recast_type(0);
                std::int32_t time = q.item_spell_recast_time(0);
                q.pc_zone();
                q.set_item_recast_timer(time, type);
            }
            if (quiet) to_send = false;
        } else {
            std::uint32_t t = static_cast<std::uint32_t>(q.item_spell_cast_time(a.item_cast_type));
            q.launch_spell_data_reset();
            CastingData& c = me->casting;
            c.item_location = a.item_location;
            c.eta = s.now + t;
            c.spell_id = id;
            c.slot = a.slot;
            c.item_id = static_cast<std::uint32_t>(q.item_id());
            c.item_cast_type = static_cast<std::uint32_t>(a.item_cast_type);
            c.y = fpu_copy(me->y);
            c.x = fpu_copy(me->x);
            if (s.target) c.target_id = s.target->spawn_id;
            if (a.position) {
                for (std::size_t k = 0; k < 3; ++k) c.target_position[k] = fpu_copy((*a.position)[k]);
                c.free_target = 1;
            }
            s.last_beneficial = spell.spell_type;
            if (quiet) {
                q.start_casting(id, static_cast<std::uint16_t>(me->spawn_id), t);
                to_send = false;
            }
        }
    }
    if (!to_send) return 1;

    // The gem timers (@0x43c750).
    if (a.slot <= 0xb) {
        me->gem_eta[a.slot] = s.now + 10000;
        me->cooldown_eta = s.now + 10000;
    }
    if (a.slot == 0xc) me->gem_eta[12] = s.now + 10000;
    if (!a.send_packet) return 1;

    // OP_CastSpell (@0x43c7c1).
    pkt.slot = a.slot;
    pkt.spell_id = id;
    pkt.item_location = a.item_location;
    pkt.target_id = s.target ? s.target->spawn_id : 0;
    pkt.has_position = a.position ? 1 : 0;
    if (a.position) {
        for (std::size_t k = 0; k < 3; ++k) pkt.position[k] = fpu_copy((*a.position)[k]);
        pkt.position_set = true;
    } else if (init_path) {
        pkt.position = {0, 0, 0};
        pkt.position_set = true;
    }
    pkt.crc = q.spell_crc(id);
    if (s.connected) {
        if (sent) *sent = pkt;
        q.hton(0x1287, 0);
        bool ok_send = q.send(4, pkt, 0x2e);
        q.operator_delete(0);
        --s.packet_count;
        if (!ok_send) q.close_connection();
    } else if (!s.quiet) {
        q.debug_log(0x1287);
    }
    if (!q.bard_cast_bard(sp, s.class_id)) {
        ++s.currently_interacting;
        ++s.item_pending;
        q.push_interact_reason(0);
        q.push_pending_reason(7);
    }
    return 1;
}

} // namespace hailstone::game::mechanics::cast
