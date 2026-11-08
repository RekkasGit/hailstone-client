#include "game/boats.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "game/mechanics/remote_spawns.h"
#include "game/picking.h"
#include "game/sine_table.h"
#include "game/world_state.h"

namespace hailstone::game {

namespace {

// The shortest turn from `from` to `to`, -256..256.
float heading_diff(float to, float from) {
    float d = std::fmod(to - from, 512.0f);
    if (d > 256.0f) d -= 512.0f;
    if (d < -256.0f) d += 512.0f;
    return d;
}

} // namespace

float wrap_heading(float heading) {
    float h = std::fmod(heading, 512.0f);
    return h < 0.0f ? h + 512.0f : h;
}

BoatPose drawn_pose(const Spawn& s) {
    return s.display_valid ? BoatPose{s.display_position, s.display_heading} : BoatPose{s.position, s.heading};
}

BoatPose stepped_pose(const Spawn& s) { return {s.position, s.heading}; }

glm::vec3 boat_to_local(const BoatPose& boat, const glm::vec3& world) {
    const float sh = cmath_sin_interp(boat.heading), ch = cmath_cos_interp(boat.heading);
    const glm::vec3 d = world - boat.position;
    return {ch * d.x + sh * d.y, -sh * d.x + ch * d.y, d.z};
}

glm::vec3 boat_to_world(const BoatPose& boat, const glm::vec3& local) {
    const float sh = cmath_sin_interp(boat.heading), ch = cmath_cos_interp(boat.heading);
    return boat.position + glm::vec3(ch * local.x - sh * local.y, sh * local.x + ch * local.y, local.z);
}

void vehicle_note_update(Spawn& s, const glm::vec3& position, float heading) {
    if (!s.display_valid) return;
    s.display_error = s.display_position - position;
    s.display_heading_error = heading_diff(s.display_heading, heading);
    if (glm::length(s.display_error) > kBoatSnapDistance) {
        s.display_error = glm::vec3(0.0f);
        s.display_heading_error = 0.0f;
    }
}

void step_vehicle(Spawn& s, float dt, bool fresh) {
    const float one = dt * 1000.0f;
    step_vehicle(s, dt, fresh, std::span<const float>(&one, 1));
}

void step_vehicle(Spawn& s, float dt, bool fresh, std::span<const float> steps_ms) {
    if (fresh) {
        // Boats float (EQEmu never fixes a boat's z): no floor, no gravity.
        mechanics::RemoteBody b{s.position, s.velocity, s.heading, s.speed_heading, s.speed_run};
        for (float ms : steps_ms) mechanics::step_remote_spawn(b, ms, true);
        s.position = b.position;
        s.velocity = b.velocity;
        s.heading = b.heading;
    }
    if (!s.display_valid) {
        s.display_error = glm::vec3(0.0f);
        s.display_heading_error = 0.0f;
    }
    const float fade = std::exp(-dt * kBoatSettleRate);
    s.display_error *= fade;
    s.display_heading_error *= fade;
    s.display_position = s.position + s.display_error;
    s.display_heading = wrap_heading(s.heading + s.display_heading_error);
    s.display_valid = true;
}

void attach_rider(Spawn& rider, std::uint32_t vehicle_id, const Spawn& vehicle) {
    const BoatPose stepped = stepped_pose(vehicle);
    rider.vehicle_local = boat_to_local(stepped, rider.position);
    rider.vehicle_local_heading = heading_diff(rider.heading, stepped.heading);
    if (rider.on_vehicle != vehicle_id) {
        const BoatPose drawn = drawn_pose(vehicle);
        if (rider.display_valid) {
            rider.vehicle_display_local = boat_to_local(drawn, rider.display_position);
            rider.vehicle_display_local_heading = heading_diff(rider.display_heading, drawn.heading);
        } else {
            rider.vehicle_display_local = rider.vehicle_local;
            rider.vehicle_display_local_heading = rider.vehicle_local_heading;
        }
    }
    rider.on_vehicle = vehicle_id;
}

void step_rider(Spawn& rider, const Spawn& vehicle, float dt, bool fresh) {
    const float one = dt * 1000.0f;
    step_rider(rider, vehicle, dt, fresh, std::span<const float>(&one, 1));
}

void step_rider(Spawn& rider, const Spawn& vehicle, float dt, bool fresh, std::span<const float> steps_ms) {
    if (fresh) {
        mechanics::RemoteBody b{rider.vehicle_local, rider.velocity, rider.vehicle_local_heading, rider.speed_heading,
                                rider.speed_run};
        for (float ms : steps_ms) mechanics::step_remote_spawn(b, ms, rider.is_npc);
        rider.vehicle_local = b.position;
        rider.velocity = b.velocity;
        rider.vehicle_local_heading = b.heading;
    }
    const float k = 1.0f - std::exp(-dt * 12.0f);
    rider.vehicle_display_local += (rider.vehicle_local - rider.vehicle_display_local) * k;
    rider.vehicle_display_local_heading +=
        heading_diff(rider.vehicle_local_heading, rider.vehicle_display_local_heading) * k;
    const BoatPose stepped = stepped_pose(vehicle), drawn = drawn_pose(vehicle);
    rider.position = boat_to_world(stepped, rider.vehicle_local);
    rider.heading = wrap_heading(stepped.heading + rider.vehicle_local_heading);
    rider.display_position = boat_to_world(drawn, rider.vehicle_display_local);
    rider.display_heading = wrap_heading(drawn.heading + rider.vehicle_display_local_heading);
    rider.display_valid = true;
}

BoatPose steer_boat(Helm& helm, const BoatPose& pose, bool forward, bool back, bool left, bool right, float dt) {
    float& v = helm.speed;
    if (forward && !back) v = std::min(v + kHelmAcceleration * dt, kHelmTopSpeed);
    else if (back && !forward) v = std::max(v - kHelmAcceleration * dt, -kHelmAsternSpeed);
    else v = v > 0.0f ? std::max(v - kHelmDrag * dt, 0.0f) : std::min(v + kHelmDrag * dt, 0.0f);
    helm.turn = left == right ? 0.0f : (left ? kHelmTurnRate : -kHelmTurnRate);
    BoatPose out = pose;
    out.heading = wrap_heading(pose.heading + helm.turn * dt);
    out.position = boat_to_world(out, glm::vec3(v * dt, 0.0f, 0.0f));
    return out;
}

BoatPose helm_resolve(Helm& helm, const BoatPose& from, const BoatPose& to, const HelmClearFn& clear) {
    if (!clear || clear(from, to)) return to;
    // The move without the turn (backing off a dock while the wheel is over), then the turn without the move.
    const BoatPose moved{to.position, from.heading};
    if (to.heading != from.heading && to.position != from.position && clear(from, moved)) return moved;
    helm.speed = 0.0f;
    const BoatPose turned{from.position, to.heading};
    if (to.heading != from.heading && clear(from, turned)) return turned;
    return from;
}

std::vector<glm::vec3> hull_probes(std::span<const glm::vec3> points, float probe_radius) {
    std::vector<glm::vec3> out;
    if (points.empty()) return out;
    glm::vec2 lo(points[0]), hi(points[0]);
    for (const auto& p : points) {
        lo = glm::min(lo, glm::vec2(p));
        hi = glm::max(hi, glm::vec2(p));
    }
    const glm::vec2 centre = (lo + hi) * 0.5f;
    for (int k = 0; k < kHullProbeDirections; ++k) {
        const float a = 6.28318531f * static_cast<float>(k) / static_cast<float>(kHullProbeDirections);
        const glm::vec2 dir(std::cos(a), std::sin(a));
        const glm::vec3* best = &points[0];
        float best_d = glm::dot(glm::vec2(*best), dir);
        for (const auto& p : points)
            if (const float d = glm::dot(glm::vec2(p), dir); d > best_d) {
                best_d = d;
                best = &p;
            }
        // Pulled in toward the middle of the outline, so a corner's probe is inside both its sides.
        const glm::vec2 in = glm::vec2(*best) - centre;
        const float in_length = glm::length(in);
        const glm::vec2 pull = in_length > probe_radius ? in / in_length * probe_radius : in;
        const glm::vec3 probe = *best - glm::vec3(pull, 0.0f);
        if (std::none_of(out.begin(), out.end(), [&](const glm::vec3& q) { return glm::distance(q, probe) < 0.01f; }))
            out.push_back(probe);
    }
    return out;
}

bool hull_clear(const BoatPose& from, const BoatPose& to, std::span<const glm::vec3> probes, const HullSweepFn& blocked,
                float probe_radius) {
    if (!blocked) return true;
    for (const auto& p : probes)
        if (blocked(boat_to_world(from, p), boat_to_world(to, p), probe_radius)) return false;
    return true;
}

std::uint32_t sweep_hulls(SphereSweep& s, std::span<const VehicleHull> hulls, float radius, std::uint32_t prefer,
                          std::uint32_t skip) {
    std::uint32_t stopped = 0;
    std::optional<float> prefer_t;
    auto sweep = [&](const VehicleHull& v) {
        if (v.id == skip) return;
        if (point_segment_distance_squared(PickSegment{s.start, s.delta}, v.center) >
            (v.radius + radius) * (v.radius + radius))
            return;
        bool hit = false;
        for (const auto& t : v.triangles) hit = collide_triangle(s, t) || hit;
        if (!hit) return;
        stopped = v.id;
        if (v.id == prefer) prefer_t = s.t;
    };
    // The vehicle stood on first: a hull hit only as near as it (or nearer) is taken after it, so the last one to
    // stop the sweep is the nearest, and a tie with it is settled below.
    for (const auto& v : hulls)
        if (prefer && v.id == prefer) sweep(v);
    for (const auto& v : hulls)
        if (!prefer || v.id != prefer) sweep(v);
    if (prefer_t && (*prefer_t - s.t) * glm::length(s.delta) <= kHullTieDistance) return prefer;
    return stopped;
}

} // namespace hailstone::game
