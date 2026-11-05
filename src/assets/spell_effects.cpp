#include "assets/spell_effects.h"

#include <cstring>
#include <stdexcept>

#include "core/byte_io.h"

namespace nmsc::assets {

namespace {

std::string cstring(ByteReader& r, std::size_t n) {
    std::string s = r.fixed_string(n);
    if (auto nul = s.find('\0'); nul != std::string::npos) s.resize(nul); // stale bytes follow the first NUL
    return s;
}

glm::vec3 vec3(ByteReader& r) {
    float x = r.f32(), y = r.f32();
    return {x, y, r.f32()};
}

} // namespace

EmitterDefinition parse_edd_record(std::span<const std::byte> record) {
    if (record.size() < kEddRecordSize) throw std::runtime_error("EDD record too short");
    ByteReader r(record.subspan(0, kEddRecordSize));
    EmitterDefinition e;
    e.name = cstring(r, 64);
    e.texture = cstring(r, 32);
    e.bone_orientation = r.le<std::uint32_t>();
    e.no_zwrite = r.le<std::uint32_t>();
    e.additive = r.le<std::uint32_t>();
    e.actor_scales_size = r.le<std::uint32_t>();
    e.billboard = r.le<std::uint32_t>();
    e.follow_attachment = r.le<std::uint32_t>();
    e.duration = r.f32();
    e.lifetime = r.f32();
    e.initial_count = r.le<std::uint32_t>();
    e.per_tick = r.le<std::uint32_t>();
    e.ticks_per_second = r.f32();
    e.delay = r.f32();
    e.fade_in = r.f32();
    e.fade_out = r.f32();
    e.grow = r.f32();
    e.shrink = r.f32();
    e.reduce_distance = r.f32();
    e.max_opacity = r.f32();
    e.shape = r.le<std::uint32_t>();
    e.shape_size = vec3(r);
    e.offset = vec3(r);
    e.angle1 = r.f32();
    e.angle2 = r.f32();
    e.width = r.f32();
    e.depth_offset = r.f32();
    for (int i = 0; i < 3; ++i) e.color_start[i] = r.le<std::uint32_t>();
    for (int i = 0; i < 3; ++i) e.color_end[i] = r.le<std::uint32_t>();
    for (auto& m : e.motion) m = vec3(r);
    e.gravity = r.f32();
    e.wind_x = r.f32();
    e.frames = r.le<std::uint32_t>();
    e.frames_per_second = r.f32();
    e.spin = r.f32();
    e.unknown_13c = r.le<std::uint32_t>();
    e.unknown_140 = r.le<std::uint32_t>();
    e.capacity = r.le<std::uint32_t>();
    for (auto& g : e.globals) g = r.le<std::uint32_t>();
    e.scale1 = r.f32();
    e.scale2 = r.f32();
    e.random_rotation = r.le<std::uint32_t>();
    e.unknown_178 = r.le<std::uint32_t>();
    e.height_start = r.f32();
    e.height_end = r.f32();
    e.width_end = r.f32();
    e.spin2 = r.f32();
    e.same_random_size = r.le<std::uint32_t>();
    e.shrink_x = r.f32();
    e.shrink_y = r.f32();
    e.negative_radial = r.le<std::uint32_t>();
    e.actor_scales_offsets = r.le<std::uint32_t>();
    return e;
}

std::vector<EmitterDefinition> parse_edd(std::span<const std::byte> data) {
    if (data.size() < 8 || std::memcmp(data.data(), "EDD\0", 4) != 0)
        throw std::runtime_error("not an EDD file (the legacy headerless format is not supported)");
    std::string version(reinterpret_cast<const char*>(data.data()) + 4, 4);
    if (auto nul = version.find('\0'); nul != std::string::npos) version.resize(nul);
    if (version != "110") throw std::runtime_error("EDD version " + version + " is not supported (only 110)");
    if ((data.size() - 8) % kEddRecordSize != 0) throw std::runtime_error("EDD size is not 8 + n x 0x1a0");
    std::vector<EmitterDefinition> out;
    out.reserve((data.size() - 8) / kEddRecordSize);
    for (std::size_t at = 8; at < data.size(); at += kEddRecordSize)
        out.push_back(parse_edd_record(data.subspan(at, kEddRecordSize)));
    return out;
}

std::vector<SpellEffect> parse_spell_effects(std::span<const std::byte> data) {
    constexpr std::size_t kRecord = 268;
    std::vector<SpellEffect> out;
    for (std::size_t at = 0; at + kRecord <= data.size(); at += kRecord) {
        ByteReader r(data.subspan(at, kRecord));
        SpellEffect e;
        e.name = cstring(r, 64);
        for (auto& stage : e.stages) {
            stage.sound = r.le<std::int32_t>();
            for (auto& slot : stage.slots) {
                slot.emitter = r.le<std::int32_t>();
                slot.min_level = r.le<std::int32_t>();
                slot.location = r.le<std::int32_t>();
                slot.attach = r.le<std::int32_t>();
            }
        }
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace nmsc::assets
