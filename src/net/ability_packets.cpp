#include "net/ability_packets.h"

#include "net/packet_io.h"
#include "net/zone_packets.h"

namespace nmsc::net {

AppPacket encode_combat_ability(std::uint32_t target, std::uint32_t atk, std::uint32_t skill) {
    ByteWriter w;
    w.le(target);
    w.le(atk & 0xffu); // DAT_00dd00e6 = slot & 0xff
    w.le(skill);
    return make_packet(Op::CombatAbility, std::move(w));
}

AppPacket encode_bind_wound(std::uint32_t target) {
    ByteWriter w;
    w.le(target);
    w.le(std::uint32_t{0});
    return make_packet(Op::Bind_Wound, std::move(w));
}

AppPacket encode_disarm(std::uint32_t source, std::uint32_t target, std::uint32_t skill_value) {
    ByteWriter w;
    w.le(source);
    w.le(target);
    w.le(skill_value);
    w.le(std::uint32_t{0xff}); // the low byte of a zeroed local set to 0xff
    return make_packet(Op::Disarm, std::move(w));
}

namespace {
AppPacket steal_or_beg(Op op, std::uint32_t target, std::uint32_t me, std::uint32_t skill_value) {
    ByteWriter w;
    w.le(target);
    w.le(me);
    w.le(skill_value);
    w.le(std::uint32_t{0xff});
    w.le(std::uint16_t{0});
    return make_packet(op, std::move(w));
}
} // namespace

AppPacket encode_pick_pocket(std::uint32_t target, std::uint32_t me, std::uint32_t skill_value) {
    return steal_or_beg(Op::PickPocket, target, me, skill_value);
}

AppPacket encode_begging(std::uint32_t target, std::uint32_t me, std::uint32_t skill_value) {
    return steal_or_beg(Op::Begging, target, me, skill_value);
}

AppPacket encode_feign_death(std::uint32_t me, std::uint32_t unknown) {
    ByteWriter w;
    w.le(me);
    w.le(unknown);
    w.le(std::uint32_t{0});
    return make_packet(Op::FeignDeath, std::move(w));
}

AppPacket encode_taunt(std::uint32_t target) {
    ByteWriter w;
    w.le(target);
    return make_packet(Op::Taunt, std::move(w));
}

AppPacket encode_remove_trap(std::uint32_t target) {
    ByteWriter w;
    w.le(target);
    return make_packet(Op::RemoveTrap, std::move(w));
}

AppPacket encode_animation(std::uint16_t spawn_id, std::uint8_t action, std::uint8_t speed) {
    ByteWriter w;
    w.le(spawn_id);
    w.le(action);
    w.le(speed);
    return make_packet(Op::Animation, std::move(w));
}

AppPacket encode_hide_cancel() {
    ByteWriter w;
    w.le(std::uint32_t{0});
    return make_packet(Op::Hide, std::move(w));
}

std::optional<SkillUpdate> decode_skill_update(const AppPacket& p) {
    if (p.data.size() < 8) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        SkillUpdate v;
        v.skill = r.le<std::uint32_t>();
        v.value = r.le<std::uint32_t>();
        v.announce = r.remaining() > 0 ? r.le<std::uint8_t>() : 0;
        return v;
    });
}

AppPacket encode_skill_update(const SkillUpdate& v) {
    ByteWriter w;
    w.le(v.skill);
    w.le(v.value);
    w.le(v.announce);
    w.zeros(3);
    return make_packet(Op::SkillUpdate, std::move(w));
}

std::optional<AppPacket> encode_skill_send(const game::mechanics::SkillSend& s) {
    using K = game::mechanics::SkillSend::Kind;
    if (s.raw) {
        std::vector<std::byte> data(s.payload.size());
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>(s.payload[i]);
        return AppPacket(s.opcode, std::move(data));
    }
    switch (s.kind) {
    case K::CombatAbility: return encode_combat_ability(s.target, 100, s.skill);
    case K::RangedAttack: return encode_combat_ability(s.target, 11, s.skill);
    case K::BindWound: return encode_bind_wound(s.target);
    case K::Unhide: return encode(SpawnAppearance{static_cast<std::uint16_t>(s.target), 3, 0});
    case K::StopSneak: return encode(SpawnAppearance{static_cast<std::uint16_t>(s.target), 15, 0});
    case K::Disarm: return encode_disarm(s.source, s.target, s.value);
    case K::DisarmTraps: return make_empty(Op::DisarmTraps);
    case K::PickLock: return make_empty(Op::LDoNPickLock);
    case K::SenseTraps: return make_empty(Op::SenseTraps);
    case K::Forage: return make_empty(Op::Forage);
    case K::Mend: return make_empty(Op::Mend);
    case K::SenseHeading: return make_empty(Op::SenseHeading);
    case K::TrackUnknown: return make_empty(Op::TrackUnknown);
    case K::Fishing: return make_empty(Op::Fishing);
    case K::Hide: return make_empty(Op::Hide);
    case K::Sneak: return make_empty(Op::Sneak);
    case K::InstillDoubt: return make_empty(Op::InstillDoubt);
    case K::FeignDeath: return encode_feign_death(s.target);
    case K::PickPocket: return encode_pick_pocket(s.target, s.source, s.value);
    case K::Begging: return encode_begging(s.target, s.source, s.value);
    case K::Taunt: return encode_taunt(s.target);
    case K::RemoveTrap: return encode_remove_trap(s.target);
    case K::FishAnimation: return encode_animation(static_cast<std::uint16_t>(s.target), 5, 5);
    case K::LDoNInspect: return make_empty(Op::LDoNInspect);
    case K::LDoNOpen: return make_empty(Op::LDoNOpen);
    case K::Reveal: return AppPacket(0x15e7); // Cmd_reveal's opcode (net/player_packets.h kOpReveal)
    }
    return std::nullopt;
}

std::optional<std::uint32_t> decode_track_reply_header(const AppPacket& p) {
    if (p.data.size() < 4) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) { return r.le<std::uint32_t>(); });
}

} // namespace nmsc::net
