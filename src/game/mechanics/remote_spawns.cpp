#include "game/mechanics/remote_spawns.h"

#include "game/mechanics/movement.h"
#include "game/mechanics/physics.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace hailstone::game::mechanics {

namespace {

// Bits low..high of a dword, sign-extended.
std::int32_t signed_bits(std::uint32_t value, int high, int low) {
    return static_cast<std::int32_t>(value << (31 - high)) >> (31 - high + low);
}

// value * unit, stored as a float.
float scaled(std::int32_t value, float unit, const Fpu& f) {
    return static_cast<float>(f.mul(static_cast<double>(value), static_cast<double>(unit)));
}

constexpr float kEighth = 0.125f, kSixtyFourth = 0.015625f, kQuarter = 0.25f;
constexpr float kAnimationScale = 0.025f, kTurnScale = 0.05f, kZJump = 5.0f;
constexpr std::uint32_t kAnimationStop = 0x42;

} // namespace

void unpack_spawn_position(SpawnPhysics& p, const PackedPosition& w, const UnpackZ& zr, const Fpu& f) {
    float old_z = p.z;
    p.y = scaled(signed_bits(w[0], 30, 12), kEighth, f);
    p.speed_y = scaled(signed_bits(w[4], 22, 10), kSixtyFourth, f);
    p.x = scaled(signed_bits(w[2], 18, 0), kEighth, f);
    p.speed_x = scaled(signed_bits(w[1], 25, 13), kSixtyFourth, f);
    float z = scaled(signed_bits(w[3], 28, 10), kEighth, f);
    p.speed_z = scaled(signed_bits(w[1], 12, 0), kSixtyFourth, f);
    p.heading = scaled(signed_bits(w[2], 30, 19), kQuarter, f);
    p.pitch = scaled(signed_bits(w[0], 11, 0), kQuarter, f);
    p.speed_run = scaled(signed_bits(w[4], 9, 0), kAnimationScale, f);
    p.speed_heading = scaled(signed_bits(w[3], 9, 0), kTurnScale, f);
    if (!zr.spawn) return;
    if (zr.levitating && !zr.keep_level) {
        // |z - old| above 5 (an unordered compare keeps the old z).
        double moved = std::fabs(x87_sub(f, z, p.z));
        if (moved > static_cast<double>(kZJump)) p.z = z;
    } else {
        p.z = z;
    }
    if (!zr.keep_z_global) {
        if (zr.vf54) {
            p.z = through_fpu(old_z); // copied through the FPU: quiet
            p.speed_z = 0.0f;
        }
    } else if (zr.vf54) {
        p.speed_z = 0.0f;
    }
}

bool clear_stopped_motion(PackedPosition& packed) {
    // The record's dwords: packed[k] is record[k + 1].
    std::uint32_t animation = packed[4] & 0x3ff;
    if (animation != kAnimationStop || (packed[3] & 0x3ff) != animation) return false;
    packed[3] &= 0xfffffc00u;
    packed[4] &= 0xff800000u;
    packed[1] &= 0xfc000000u;
    return true;
}

namespace {

// BitStream (+4 size, +8 byte, +0xc bit in the byte).
struct BitStream {
    std::span<const std::uint8_t> data;
    std::size_t byte = 0;
    std::uint32_t bit = 0;

    // BitStream_ReadBits: bits that fit in the current byte.
    std::uint32_t read_bits(std::uint32_t n) {
        std::uint32_t v = (static_cast<std::uint32_t>(data[byte]) >> (8 - bit - n)) & ((1u << n) - 1);
        bit += n;
        if (bit == 8) {
            ++byte;
            bit = 0;
        }
        return v;
    }
    // BitStream::ReadUInt @0x80d380: 0 at the end; piecewise across bytes,
    // high bits first, 0 when the end is reached part way.
    std::uint32_t read_uint(std::uint32_t bits) {
        if (byte >= data.size()) return 0;
        std::int32_t left = static_cast<std::int32_t>(bits);
        std::int32_t room = 8 - static_cast<std::int32_t>(bit);
        if (room >= left) return read_bits(bits);
        std::uint32_t value = 0;
        while (left > 0) {
            std::int32_t part = std::min(left, room);
            value = read_bits(static_cast<std::uint32_t>(part)) | (value << part);
            left -= room;
            room = 8 - static_cast<std::int32_t>(bit);
            if (left <= 0) break;
            if (byte >= data.size()) return 0;
        }
        return value;
    }
    // BitStream::ReadInt @0x80d460: sign, then magnitude.
    std::uint32_t read_int(std::uint32_t bits) {
        std::uint32_t sign = read_uint(1);
        std::uint32_t magnitude = read_uint(bits - 1);
        return sign ? 0u - magnitude : magnitude;
    }
};

// The original's xor/and/xor into a field.
void set_bits(std::uint32_t& field, std::uint32_t value, std::uint32_t shift, std::uint32_t mask) {
    field ^= ((value << shift) ^ field) & mask;
}

} // namespace

std::array<std::uint32_t, 6> unpack_npc_move_update(std::span<const std::uint8_t> data) {
    BitStream in{data};
    std::array<std::uint32_t, 6> r{};
    std::uint32_t id = in.read_uint(16);
    std::uint32_t vehicle = in.read_uint(16);
    r[0] = (id & 0xffff) | (vehicle << 16);
    std::uint32_t flags = in.read_uint(6);
    set_bits(r[1], in.read_int(19), 12, 0x7ffff000);
    set_bits(r[3], in.read_int(19), 0, 0x7ffff);
    set_bits(r[4], in.read_int(19), 10, 0x1ffffc00);
    set_bits(r[3], in.read_int(12), 19, 0x7ff80000);
    if (flags & 1) set_bits(r[1], in.read_int(12), 0, 0xfff);
    if (flags & 2) set_bits(r[4], in.read_int(10), 0, 0x3ff);
    if (flags & 4) set_bits(r[5], in.read_int(10), 0, 0x3ff);
    if (flags & 8) set_bits(r[5], in.read_int(13), 10, 0x7ffc00);
    if (flags & 0x10) set_bits(r[2], in.read_int(13), 13, 0x3ffe000);
    if (flags & 0x20) set_bits(r[2], in.read_int(13), 0, 0x1fff);
    return r;
}

ClientUpdateResult handle_client_update(const ClientUpdateInput& in, PackedPosition& packed,
                                        std::uint32_t& last_request) {
    ClientUpdateResult r;
    if (!in.found) {
        if (in.in_game && in.local_player && in.now - last_request >= kSpawnRequestMs) {
            last_request = in.now;
            r.request_spawn = true;
        }
        return r;
    }
    if (in.has_mount) {
        r.unpack_rider = true;
    } else if (in.has_rider) {
        r.handled = true; // a mount with a rider ignores its own updates
        return r;
    }
    r.stop = clear_stopped_motion(packed);
    r.unpack = true;
    r.handled = true;
    return r;
}

bool advance_spawn(const AdvanceInput& in) {
    if (in.hidden || in.is_controlled || in.is_local || in.controlled_by_player) return false;
    if (in.ms_since_update <= kSpawnStaleMs) return true;
    if (in.corpse) {
        if (in.speed_run == 0.0f) return in.speed_z != 0.0f; // at rest stays, falling moves
    }
    return in.force_move;
}

void step_remote_spawn(RemoteBody& b, float elapsed_ms, bool npc) {
    float dt = elapsed_ms * kTicksPerMs;
    if (!(dt <= kMaxStepTicks)) dt = kMaxStepTicks;
    b.heading = integrate_heading(b.heading, b.speed_heading, dt);
    float c = kGroundFriction;
    if (npc && !(b.velocity.x == 0.0f && b.velocity.y == 0.0f)) c = c * 0.5f;
    glm::vec2 push(0.0f);
    if (b.speed_run != 0.0f) {
        push.x = heading_cos(b.heading) * b.speed_run * kGroundAcceleration;
        push.y = heading_sin(b.heading) * b.speed_run * kGroundAcceleration;
    }
    glm::vec3& v = b.velocity;
    if (v.x != 0.0f) v.x = c * v.x;
    if (v.y != 0.0f) v.y = c * v.y;
    if (std::fabs(v.x) < FLT_EPSILON) v.x = 0.0f;
    if (std::fabs(v.y) < FLT_EPSILON) v.y = 0.0f;
    v.x = push.x + v.x;
    v.y = push.y + v.y;
    b.position.x = integrate_axis(b.position.x, v.x, dt);
    b.position.y = integrate_axis(b.position.y, v.y, dt);
}

} // namespace hailstone::game::mechanics
