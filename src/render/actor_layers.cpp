#include "render/actor_layers.h"

#include <algorithm>
#include <cmath>

#include "game/anim_layers.h"
#include "game/mechanics/anim_select.h"

namespace hailstone::render {

namespace mech = game::mechanics;

bool GroupTrack::start(const assets::AnimationClip* c, std::uint8_t code, float speed, bool loops, bool reversed,
                       float blend_s, bool restart) {
    if (!c) return false;
    if (!std::isfinite(speed) || speed < 0.0f) speed = 0.0f;
    if (c == clip && reversed == reverse && !restart) {
        // BlendToAnimation leaves the animation it plays alone; the caller's speed still reaches it
        // (SetCurrentAnimationParameters, FUN_004b15b0's "keep" path / FUN_0058ac90's mode 3).
        rate = speed;
        loop = loops;
        anim = code;
        return false;
    }
    if (clip && blend_s > 0.0f) {
        previous = clip;
        previous_time = time;
        previous_reverse = reverse;
        blend = 0.0f;
        blend_seconds = blend_s;
    } else {
        previous = nullptr;
        blend = 1.0f;
    }
    clip = c;
    anim = code;
    rate = speed;
    loop = loops;
    reverse = reversed;
    time = 0.0f;
    elapsed = 0.0f;
    return true;
}

void GroupTrack::advance(float dt) {
    if (!clip) return;
    time += dt * rate;
    const float d = clip->duration_seconds();
    if (loop && d > 0.0f) time = std::fmod(time, d);
    elapsed += dt;
    if (blend < 1.0f) {
        blend = blend_seconds > 0.0f ? std::min(1.0f, blend + dt / blend_seconds) : 1.0f;
        previous_time += dt;
    }
    if (blend >= 1.0f) previous = nullptr;
}

bool GroupTrack::completed() const {
    if (!clip) return true;
    const float d = clip->duration_seconds();
    if (d <= 0.0f) return true;
    if (rate == 0.0f) return false;
    return elapsed >= d / std::fabs(rate);
}

void GroupTrack::sync_to(const GroupTrack& other) {
    if (clip && clip == other.clip) time = other.time;
}

namespace {
float shown(const assets::AnimationClip* c, float t, bool reversed) {
    if (!reversed || !c) return t;
    const float d = c->duration_seconds();
    return d > 0.0f ? std::max(0.0f, d - std::min(t, d)) : 0.0f;
}
} // namespace

float GroupTrack::shown_time() const { return shown(clip, time, reverse); }
float GroupTrack::shown_previous_time() const { return shown(previous, previous_time, previous_reverse); }

namespace {
// The cross-fade into `to` on a group that plays `from`: classic models always 100 ms (0x9c3988), new-style ones
// the mean of the old sequence's blend-out and the new one's blend-in (FUN_005890a0 / FUN_00589410, per group).
float blend_seconds(const LayerRequest& req, std::uint8_t from, std::uint8_t to, float classic_ms) {
    return (req.new_style ? mech::new_style_blend_ms(from, to) : classic_ms) / 1000.0f;
}
} // namespace

bool ActorLayers::start_action(const assets::AnimationClip* clip, std::uint8_t anim, float rate,
                               const LayerRequest& req) {
    if (!clip || req.corpse) return false;
    GroupTrack& up = group[game::kUpperBones];
    GroupTrack& lo = group[game::kLowerBones];
    bool upper_only = false;
    if (req.new_style) {
        // FUN_0058afa0: each group's mode (FUN_0058ac90; the style column is not tracked, so a sequence asked
        // again is the same one) decides. A rider plays only sequences of layer kind 1, and those on the upper
        // body (FUN_00588e20); a kind 1 sequence plays there alone too while the legs walk, run or turn (kind 2).
        const std::uint8_t kind = mech::new_style_layer_kind(anim);
        auto mode_of = [&](const GroupTrack& g) {
            game::LayerModeInput li;
            li.wanted = anim;
            li.playing = g.anim;
            li.completed = g.completed();
            return game::new_style_layer_mode(li);
        };
        const std::uint8_t upper_mode = mode_of(up);
        if (req.mounted) {
            if (kind != 1) return false;
            upper_only = true;
        } else {
            upper_only = kind == 1 && mech::new_style_layer_kind(lo.anim) == 2;
        }
        if (upper_only) {
            // FUN_00588e20: nothing while the upper body plays a sequence of layer kind 0 (and no mount answer).
            if (upper_mode != 2) return false;
            if (!req.mounted && mech::new_style_layer_kind(up.anim) == 0) return false;
        } else {
            const std::uint8_t lower_mode = mode_of(lo);
            if (kind == 2) {
                // FUN_005890a0: nothing without a lower mode, then each group by its own.
                if (lower_mode == 0 || (lower_mode != 2 && upper_mode != 2)) return false;
                upper_only = lower_mode != 2;
                if (upper_mode != 2) {
                    lo.start(clip, anim, rate, false, false, blend_seconds(req, lo.anim, anim, 100.0f));
                    return false;
                }
            } else {
                // FUN_00589410: nothing when either mode is 0, both 3 continue, neither 2 queues.
                if (lower_mode == 0 || upper_mode == 0) return false;
                if (lower_mode != 2 && upper_mode != 2) return false;
            }
        }
    } else {
        // FUN_004b15b0 with key 1: an action that may not interrupt waits for the upper body's cycle.
        if (!game::classic_may_interrupt(anim, up.anim) && up.clip && !up.completed()) {
            if (game::classic_queues(anim)) {
                follow_up = anim;
                follow_clip = clip;
                follow_rate = rate;
            } else if (game::classic_clears_queue(anim)) {
                follow_up = 0x20;
                follow_clip = nullptr;
            }
            return false;
        }
        upper_only = game::classic_action_upper_only(anim, req.mounted, req.humanoid, req.speed_run);
    }
    // BlendToAnimation: a group already playing the clip keeps it.
    up.start(clip, anim, rate, false, false, blend_seconds(req, up.anim, anim, 100.0f));
    if (!upper_only) {
        lo.start(clip, anim, rate, false, false, blend_seconds(req, lo.anim, anim, 100.0f));
        up.sync_to(lo);
    }
    action = clip;
    action_anim = anim;
    action_on_legs = !upper_only;
    legs_took_motion = false;
    return true;
}

bool ActorLayers::step(const LayerRequest& req, float dt) {
    GroupTrack& up = group[game::kUpperBones];
    GroupTrack& lo = group[game::kLowerBones];
    if (req.corpse) action = nullptr;
    // The action ends once the upper body has played it through (or something else took the upper body).
    if (action && (up.clip != action || up.completed())) action = nullptr;
    if (!action) {
        action_on_legs = false;
        legs_took_motion = false;
    }
    bool followed = false;

    if (!req.new_style && !req.corpse) {
        if (action && game::classic_clears_queue(req.base_anim)) {
            // A movement request while the action plays resets the follow-up.
            follow_up = 0x20;
            follow_clip = nullptr;
        } else if (!action && req.base_anim == 0x20 && follow_up != 0x20 && follow_clip) {
            // The stand request plays the follow-up (FUN_004b15b0's idle path: +0x10c4 becomes the request, and
            // then 0x20 or 0x47 by FUN_004b0860 on the animation playing).
            const assets::AnimationClip* c = follow_clip;
            const std::uint8_t a = follow_up;
            follow_up = req.follow_up_after;
            follow_clip = nullptr;
            followed = start_action(c, a, follow_rate, req);
        }
    }
    bool legs_free = !action;
    if (action && req.new_style) {
        const std::uint8_t kind = mech::new_style_layer_kind(req.base_anim);
        if (!action_on_legs && kind == 2) {
            // FUN_005890a0: the legs take the walk / run / turn (their mode 2 or 3); the upper body's unfinished
            // action queues it (mode 1).
            legs_free = true;
        } else if (!action_on_legs && !req.mounted) {
            // FUN_00589410: another sequence starts on both groups when either group's mode is 2, cutting the
            // action; otherwise nothing changes.
            game::LayerModeInput li;
            li.wanted = req.base_anim;
            li.playing = lo.anim;
            li.completed = lo.completed();
            const std::uint8_t lower_mode = game::new_style_layer_mode(li);
            li.playing = up.anim;
            li.completed = up.completed();
            const std::uint8_t upper_mode = game::new_style_layer_mode(li);
            if (lower_mode != 0 && upper_mode != 0 && (lower_mode == 2 || upper_mode == 2)) {
                action = nullptr;
                action_on_legs = false;
                legs_free = true;
            }
        }
    } else if (action && game::classic_legs_take_motion(action_anim, req.base_anim, req.humanoid)) {
        // FUN_004b15b0's busy path: a walk or run under an unfinished action of table 0xdced38 starts on the
        // legs alone; other requests wait for the action.
        legs_free = true;
        action_on_legs = false;
        legs_took_motion = true;
    }
    if (legs_free)
        lo.start(req.base, req.base_anim, req.rate, req.loop, req.reverse,
                 blend_seconds(req, lo.anim, req.base_anim, req.blend_ms), req.restart);
    if (!action) {
        // Back on the base clip, the upper body takes the legs' position when they play the same
        // (the lower group's SyncToBoneGroup call after both are started).
        if (up.start(req.base, req.base_anim, req.rate, req.loop, req.reverse,
                     blend_seconds(req, up.anim, req.base_anim, req.blend_ms), req.restart))
            up.sync_to(lo);
    }
    up.advance(dt);
    lo.advance(dt);
    return followed;
}

bool ActorLayers::split() const {
    const GroupTrack& up = group[game::kUpperBones];
    const GroupTrack& lo = group[game::kLowerBones];
    return up.clip != lo.clip || up.reverse != lo.reverse || up.time != lo.time || up.previous != lo.previous ||
           up.blend != lo.blend || (up.previous && up.previous_time != lo.previous_time);
}

} // namespace hailstone::render
