// Per-frame clip choice for character models (game/spawn_animation.h).
// Sources: rof2decomp batch 248 (movement_animation.cpp) and batch 213
// (player_animation_update.cpp FUN_004afe60, player_animation_start.cpp
// FUN_004b15b0's stand-state substitutions, player_animation_pick.cpp
// FUN_004b1210's replacements); the old-style object's slot 0x38
// (0x4b07e0: may an idle start from this animation) read from the exe;
// idle_animation.cpp (PlayerAnimation_ChooseIdleAnimation) and
// player_animation_rows.cpp / player_animation_play.cpp (FUN_00589770,
// FUN_00588730, FUN_005885e0, FUN_00588470, FUN_0058afa0) for the new-style
// tracks.
#include "game/spawn_animation.h"

#include <optional>

#include "assets/eqg_character.h"
#include "game/anim_layers.h"
#include "game/mechanics/anim_select.h"

namespace nmsc::game {

namespace mech = nmsc::game::mechanics;

namespace {

// The old-style object's slot 0x38 (0x4b07e0): true for 0x20, 0x26, 0x47, 0x4a.
bool old_can_idle(std::int32_t anim) { return anim == 0x20 || anim == 0x26 || anim == 0x47 || anim == 0x4a; }

// FUN_004b1210's fixed replacement for an old-style animation the model lacks.
std::uint8_t old_replacement(std::uint8_t a) {
    if ((a >= 1 && a <= 4) || (a >= 6 && a <= 9)) return 5;
    switch (a) {
    case 0xa: case 0x15: case 0x19: return 0x25;
    case 0xb: return 1;
    case 0xc: case 0xe: return 0xd;
    case 0xd: return 0xc;
    case 0x11: case 0x4c: return 0x12;
    case 0x12: case 0x4b: return 0x11;
    case 0x1a: return 0x20;
    case 0x25: return 0x11;
    case 0x27: return 0x28;
    case 0x2a: case 0x2c: return 0x2b;
    case 0x48: return 0x1a;
    case 0x4e: return 0x4a;
    default: return 0x20;
    }
}

constexpr std::uint32_t kAggressive = 0x4, kForcedAggressive = 0x8, kPrimaryEquipped = 0x40,
                        kSecondaryEquipped = 0x80; // 0x9d77a4 / 0x9d77a8 / 0x9d77b4 / 0x9d77b8

// A sequence's track name in a column ("SLPR" + "_1H").
std::string track_of(std::uint8_t code, int column) {
    return std::string(mech::new_style_tag(code)) + "_" + std::string(assets::eqg_character::kTrackStyles[column]);
}

// The player's animation table as FUN_0058afa0 reads it: a cell is variant k (0..3) of a column.
struct Rows {
    const std::function<bool(std::string_view)>& has;
    bool cell(std::uint8_t code, int column, int k) const {
        return has(track_of(code, column) + "_" + std::to_string(k + 1));
    }
    bool row(std::uint8_t code) const {
        for (int c = 0; c < 6; ++c)
            for (int k = 0; k < 4; ++k)
                if (cell(code, c, k)) return true;
        return false;
    }
};

// FUN_00588730 @0x588730: the column for an animation (no forced column, no mount: slot 0x34's 0x200 state and
// GetMount are not modelled).
int column_for(std::uint8_t code, const NewStyleTrackInput& in) {
    if (!(in.player_state & kPrimaryEquipped)) return 0;
    if (in.player_state & kSecondaryEquipped) return 3;
    int column = code == 0x56 || code == 0x57 ? 2 : code == 0x58 ? 4 : 1;
    return in.forced_column != -1 ? in.forced_column : column;
}

// FUN_005885e0 @0x5885e0: STDA / IDLA for STND / IDLE while aggressive or after an attack (the playing sequence's
// +0x19 with CEverQuest +0x5c8, the game state, in game: not 1 or 2). CDisplay +0x12c's FUN_007f4100 on the
// player's +0xfd0 / +0xfbc is taken as false.
std::uint8_t alternate_row(std::uint8_t code, int column, const NewStyleTrackInput& in, const Rows& rows) {
    const bool flagged = mech::new_style_attack_flag(mech::old_to_new_anim(in.current));
    const bool in_state = (in.player_state & kAggressive) || (in.player_state & kForcedAggressive);
    if (!in_state && !flagged) return code;
    std::uint8_t alt;
    if (code == mech::kAnimStand) alt = 0x70;
    else if (code == mech::kAnimIdle) alt = 0x71;
    else return code;
    if (!rows.row(alt)) return code;
    return rows.cell(alt, column, 0) || rows.cell(alt, 0, 0) ? alt : code;
}

} // namespace

NewStyleTrack new_style_track(std::uint8_t code, const NewStyleTrackInput& in,
                              const std::function<bool(std::string_view)>& has_clip,
                              const std::function<int()>& rand) {
    // FUN_00589770 @0x589770: the row and column, following the sequence fallbacks.
    const Rows rows{has_clip};
    std::uint8_t track = code;
    int column = 0;
    bool found = false;
    for (int guard = 0; guard < 0x68 && !found; ++guard) {
        if (track <= 0x4f || track > 0xb6) return {};
        if (rows.row(track)) {
            int col = column_for(track, in);
            std::uint8_t alt = alternate_row(track, col, in, rows);
            if (rows.row(alt) && (alt == track || rows.cell(alt, col, 0))) track = alt;
            if (rows.cell(track, col, 0)) {
                column = col;
                found = true;
                break;
            }
            bool try_zero = true;
            if (col == 5) {
                try_zero = mech::new_style_mount_fallback_flag(track);
            } else if (col != 0 && rows.cell(track, 1, 0)) {
                column = 1;
                found = true;
                break;
            }
            if (try_zero && rows.cell(track, 0, 0)) {
                column = 0;
                found = true;
                break;
            }
        }
        track = mech::new_style_fallback(track);
    }
    if (!found) return {};
    // FUN_00588470 @0x588470 with extra 0xff: rand() % (1 + the column's variants 2..4 present).
    int count = 1;
    for (int k = 1; k <= 3; ++k)
        if (rows.cell(track, column, k)) ++count;
    int variant = count == 1 ? 0 : (rand ? rand() : 0) % count;
    NewStyleTrack out;
    out.code = track;
    out.track = track_of(track, column);
    out.clip = out.track + "_" + std::to_string(variant + 1);
    if (!has_clip(out.clip)) out.clip = out.track + "_1"; // the variants are taken as numbered from 1 without gaps
    return out;
}

namespace {

} // namespace

SpawnAnimChoice choose_spawn_animation(SpawnAnimState& st, const SpawnAnimInput& in,
                                       const std::function<bool(std::string_view)>& has_clip, bool clip_done,
                                       const std::function<int()>& roll, const std::function<int()>& rand) {
    const mech::Fpu fpu(mech::FpuPrecision::Single24);
    SpawnAnimChoice keep{st.clip, st.reverse, st.rate, st.loop, false, 100.0f, st.anim, in.follow_up};
    auto start = [&](const std::string& clip, std::uint8_t anim, bool reverse, float rate, bool loop,
                     float blend_ms) {
        // The same animation asked again does not restart it (FUN_004b0900); a
        // one-shot clip then holds its last frame.
        bool same = st.started && clip == st.clip && reverse == st.reverse;
        SpawnAnimChoice c{clip, reverse, rate, loop, !same, blend_ms, anim, in.follow_up};
        st.started = true;
        st.track.clear();
        st.anim = anim;
        st.clip = clip;
        st.reverse = reverse;
        st.rate = rate;
        st.loop = loop;
        return c;
    };

    // A new-style code on its track (new_style_track); the same sequence and column asked again keeps its
    // variant (FUN_0058ac90's mode 3: nothing changes).
    NewStyleTrackInput track_in{in.player_state, st.anim, -1};
    auto start_new = [&](std::uint8_t code, bool reverse, float rate, bool loop) -> std::optional<SpawnAnimChoice> {
        NewStyleTrack t = new_style_track(code, track_in, has_clip, rand);
        if (t.clip.empty()) return std::nullopt;
        // FUN_0058afa0: BWLK found on WALK's row plays it backward.
        if (code == mech::kAnimBackWalk && t.code == mech::kAnimWalk) reverse = true;
        // The layers store the sequence found (Base + row) as the animation playing (+0x10c0).
        const float blend = mech::new_style_blend_ms(st.anim, t.code);
        std::string clip = st.started && t.track == st.track ? st.clip : t.clip;
        SpawnAnimChoice c = start(clip, t.code, reverse, rate, loop, blend);
        st.track = t.track;
        return c;
    };

    // Posture changes: sitting down / standing up.
    if (st.started && in.stand_state != st.stand_state) {
        const std::uint8_t from = st.stand_state;
        st.stand_state = in.stand_state;
        if (in.stand_state == 110 && from == 100) {
            if (in.new_style) {
                if (auto c = start_new(mech::kAnimSitDown, false, 1.0f, false)) {
                    st.posture = true;
                    return *c;
                }
            } else if (has_clip("P02")) {
                st.posture = true;
                return start("P02", mech::kOldSitStand, false, 1.0f, false, 100.0f);
            }
        } else if (in.stand_state == 100 && from == 110) {
            // FUN_0058afa0: STNG asked reversed plays STDG forward.
            if (in.new_style) {
                if (auto c = start_new(mech::kAnimStandUp, false, 1.0f, false)) {
                    st.posture = true;
                    return *c;
                }
            } else if (has_clip("P02")) {
                st.posture = true;
                return start("P02", mech::kOldSitStand, true, 1.0f, false, 100.0f);
            }
        } else if (in.stand_state == 111 || (in.stand_state == 100 && from == 111)) {
            // PlayerZoneClient::ChangePosition @0x5a0bc0: crouching plays the duck (0x18) once, unless it or the
            // duck walk (0x16) is playing; standing up from a crouch plays it backward. A plain Play, not a held
            // posture: the choice below takes over when there is something else to play (a walk, a jump), and
            // otherwise leaves it (a still ducking spawn asks nothing, nor does a still one after the duck).
            const bool down = in.stand_state == 111;
            if (in.new_style) {
                const std::uint8_t duck = mech::old_to_new_anim(mech::kOldDuck);
                const std::uint8_t walk = mech::old_to_new_anim(mech::kOldDuckWalk);
                if (!down || (st.anim != duck && st.anim != walk))
                    if (auto c = start_new(duck, !down, 1.0f, false)) return *c;
            } else if (has_clip(mech::old_anim_clip(mech::kOldDuck)) &&
                       (!down || (st.anim != mech::kOldDuck && st.anim != mech::kOldDuckWalk))) {
                return start(std::string(mech::old_anim_clip(mech::kOldDuck)), mech::kOldDuck, !down, 1.0f, false,
                             100.0f);
            }
        }
    }
    st.stand_state = in.stand_state;
    if (st.posture) {
        if (!clip_done) return keep;
        st.posture = false;
    }

    // The original's speeds: +0x70 SpeedY, +0x74 SpeedX (only their squares
    // and the horizontal stillness matter here), +0x78 SpeedZ.
    const float vy = in.velocity.y, vx = in.velocity.x, vz = in.velocity.z;

    if (in.new_style) {
        // FUN_0058af10: a still player (and no mount) goes to ChooseIdleAnimation, a moving one to
        // SelectMovementAnim (which clears +0x360).
        mech::AnimStart r;
        if (mech::movement_still(vy, vx, vz, fpu)) {
            mech::IdleAnimInput idle;
            idle.turning = in.turning;
            idle.player_state = in.player_state;
            idle.display_time = in.now_ms;
            idle.still_since = st.still_since;
            idle.stand_state = static_cast<std::uint8_t>(in.stand_state == 111 ? 0x6f : in.stand_state);
            idle.body_wet = in.body_wet;
            idle.animation = st.anim;
            idle.speed_heading = in.speed_heading;
            idle.owner_38c = in.owner_id;
            idle.attacking = in.attacking;
            idle.local_first_person = in.local_player && in.first_person;
            idle.random_percent = roll;
            mech::IdleAnimResult ir = mech::choose_idle_animation(idle);
            if (ir.set_still_since) st.still_since = in.now_ms;
            r = ir.start;
        } else {
            mech::MovementAnimInput m;
            m.speed_y = vy;
            m.speed_x = vx;
            m.speed_z = vz;
            m.speed_run = in.speed_run;
            m.colliding_type = in.colliding_type;
            m.feet_wet = in.feet_wet;
            m.body_wet = in.body_wet;
            m.spawn_type = in.npc ? 1 : 0;
            m.stand_state = static_cast<std::uint8_t>(in.stand_state == 111 ? 0x6f : in.stand_state);
            m.animation = st.anim;
            m.walk_speed = in.walk_speed;
            m.run_speed = in.run_speed;
            m.model_scale = in.model_scale;
            m.anim_scale = in.anim_scale;
            m.local_player = in.local_player;
            m.mount_race = in.mount_race;
            m.new_style_race = in.new_style_race;
            r = mech::select_movement_anim(m, fpu).start;
            st.still_since = 0;
        }
        if (!r.played) return keep;
        // The play (FUN_0058afa0) does nothing for a sequence the model has no track of (nor its fallbacks).
        if (auto c = start_new(r.anim, r.reverse, r.speed, !r.play_once)) return *c;
        return keep;
    }

    // Old-style (classic) models.
    mech::OldStyleAnimInput o;
    o.speed_y = vy;
    o.speed_x = vx;
    o.speed_z = vz;
    o.speed_run = in.speed_run;
    o.colliding_type = in.colliding_type;
    o.display_time = in.now_ms;
    o.still_since = st.still_since;
    o.stand_state = static_cast<std::uint8_t>(in.stand_state == 111 ? 0x6f : in.stand_state);
    o.animation = st.anim;
    o.feet_wet = in.feet_wet;
    o.body_wet = in.body_wet;
    o.race = in.race;
    o.walk_speed = in.walk_speed;
    o.spawn_type = in.npc ? 1 : 0;
    o.local_player = in.local_player;
    o.random_percent = roll;
    o.can_idle = old_can_idle;
    mech::OldStyleAnimResult r = mech::select_old_style_anim(o, fpu);
    if (r.set_still_since) st.still_since = r.still_since;
    if (!r.start.played) return keep;
    std::uint8_t anim = r.start.anim;
    // FUN_004b15b0: while sitting, standing becomes sitting and the idle the
    // seated fidget; other stances drop idle requests.
    if (in.stand_state == 110) {
        if (anim == 0x20 || anim == 0x47) anim = mech::kOldSit;
        else if (anim == 0x1a || anim == 0x48) anim = mech::kOldSitIdle;
        else if (anim >= 0x27 && anim <= 0x29) return keep;
    }
    if (in.stand_state != 100) {
        bool idle_request = r.start.anim == 0x20 || r.start.anim == 0x22 || r.start.anim == 0x47 ||
                            r.start.anim == 0x1a || r.start.anim == 0x48 ||
                            static_cast<std::uint16_t>(r.start.anim - 0xc) <= 3;
        if (idle_request && anim != mech::kOldSit && anim != mech::kOldSitIdle) return keep;
    }
    auto has_anim = [&](std::uint8_t a) {
        std::string_view c = mech::old_anim_clip(a);
        return !c.empty() && has_clip(c);
    };
    // FUN_004b15b0: an animation the model lacks takes FUN_004b1210's replacement (the seated ones play nothing).
    if (!has_anim(anim)) {
        if (anim == mech::kOldSit || anim == mech::kOldSitIdle) return keep; // nothing starts
        anim = old_replacement(anim);
    }
    // Its idle block: what is still missing is a stand request. A stand request plays the follow-up (+0x10c4)
    // when there is one, else P01, or P08 when the animation playing (+0x10c0) is not one FUN_004b0860 names; an
    // idle (O01) after such an animation is O02 (0x48), FUN_004b1210's O01 for a model without it.
    std::uint8_t follow_up = in.follow_up;
    const bool plain = classic_plain_stand_after(st.anim);
    if (!has_anim(anim) || anim == mech::kOldStand) {
        anim = mech::kOldStand;
        if (in.follow_up == 0x20) {
            if (!plain) anim = mech::kOldStandAfter;
        } else {
            // An action as the follow-up is the layers' to play (render/actor_layers.h); the base stays P01.
            if (in.follow_up == mech::kOldStandAfter) anim = mech::kOldStandAfter;
            follow_up = plain ? 0x20 : mech::kOldStandAfter;
        }
    } else if (anim == mech::kOldIdle && !plain) {
        anim = mech::kOldIdleAfter;
        if (!has_anim(anim)) anim = old_replacement(anim);
    }
    // Still missing (P08 on most models): the stand; a model without P01 plays nothing.
    if (!has_anim(anim)) {
        anim = mech::kOldStand;
        if (!has_anim(anim)) return keep;
    }
    const std::string clip(mech::old_anim_clip(anim));
    double rate = mech::old_model_anim_speed(anim, r.start.speed, in.race, in.gender, false, false, fpu);
    // FUN_004b15b0 starts the layers with a 100 ms blend (0x9c3988) whatever the animation; the tag table's
    // blend times are the new-style object's.
    SpawnAnimChoice c = start(clip, anim, r.start.reverse, static_cast<float>(rate), !r.start.play_once, 100.0f);
    c.follow_up = follow_up;
    return c;
}

std::string action_clip(std::uint8_t action, const std::function<bool(std::string_view)>& has_clip, bool new_style,
                        const NewStyleTrackInput& track_in, const std::function<int()>& rand,
                        std::uint8_t* playing) {
    if (playing) *playing = action;
    if (new_style) {
        // The new-style object's slot 0x14: an old-style number through the map at 0xaaf5c8.
        std::uint8_t code = mech::old_to_new_anim(action);
        if (code == 0x4f || code == mech::kNoAnimation) return {};
        NewStyleTrack t = new_style_track(code, track_in, has_clip, rand);
        if (playing) *playing = t.code;
        return t.clip;
    }
    // FUN_004b15b0: a new-style code becomes its old-style number (0xaaf428); 0x4f and above play nothing.
    if (action >= 0x4f && action <= 0xb6) action = mech::new_to_old_anim(action);
    if (playing) *playing = action;
    if (action == 0 || action >= 0x4f) return {};
    std::string code(mech::old_anim_clip(action));
    if (has_clip(code)) return code;
    std::uint8_t r = old_replacement(action);
    std::string alt = r == mech::kOldStand ? std::string() : std::string(mech::old_anim_clip(r));
    return !alt.empty() && has_clip(alt) ? alt : std::string{};
}

} // namespace nmsc::game
