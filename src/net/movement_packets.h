#pragma once

// Movement packets the original client sends on its own (game/mechanics/movement.h):
//   OP_Jump         (0x31f4) no payload; DoMainLoop @0x53ecc0 after a jump.
//                   The server takes endurance (Handle_OP_Jump).
//   OP_EnvDamage    (0x51fd) EnvDamage2_Struct (rof2_structs.h, 39 bytes): the
//                   damage the client computed for a fall (type 0xfc),
//                   drowning (0xfb), lava (0xfa) or freezing water (0xf6);
//                   built by ApplyFallDamage @0x45ae2f / ProcessAirSupply and
//                   sent by CharacterZoneClient::ApplyDamage @0x457d60. The
//                   server subtracts it (Handle_OP_EnvDamage).
//   OP_SafeFallSuccess (0x2219) no payload; ApplyFallDamage when Safe Fall reduced a fall.
//   OP_SetRunMode   (0x009f) u32: 1 run, 0 walk (CharacterZoneClient::DoPassageOfTime, when __RunWalkState changes).

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "net/packet.h"

namespace nmsc::net {

struct EnvDamage {
    std::uint32_t spawn_id = 0; // the client writes its 16-bit spawn id and zeroes the rest
    std::uint32_t damage = 0;
    std::uint8_t type = 0;      // 0xfc falling, 0xfb drowning, 0xfa lava, 0xf6 freezing, 0xf4 fire
};

AppPacket encode(const EnvDamage& d);
std::optional<EnvDamage> decode_env_damage(const AppPacket& p);

AppPacket encode_jump();
AppPacket encode_safe_fall_success();
AppPacket encode_set_run_mode(bool running);

// The find path (game/player_path.h; EQEmu Handle_OP_FindPersonRequest, Client::SendPath). OP_FindPersonRequest
// (C→Z, 40 bytes, RoF2 FindPersonRequest_Struct, Send_OP_FindPersonRequest @0x59a420): u32 the find's kind, u16 the
// spawn (the original copies only the low half of +0x1c; the high half is left as zero here), u32 / u32 the real
// estate ids, the player's position, the point (f32 each in the packet's FindPerson_Point order). EQEmu reads only
// the spawn id. OP_FindPersonReply (Z→C): 12-byte points, the destination first, then the path from the player (EQEmu
// repeats its last point); empty when there is no path. The packet's point order is this client's world order
// (the server's y, x are its x, y), so the points are world coordinates as they come.
struct FindPersonRequest {
    std::uint32_t kind = 1;
    std::uint16_t id = 0;
    std::int32_t item_a = -1, item_b = -1;
    glm::vec3 at{0.0f};    // the player (world)
    glm::vec3 point{0.0f}; // the map location (world)
};
AppPacket encode(const FindPersonRequest& r);
// Every point of the reply in order (count = size / 12), as PlayerPath_HandleFindPersonReply takes them.
std::optional<std::vector<glm::vec3>> decode_find_person_reply(const AppPacket& p);
AppPacket encode_find_person_reply(const std::vector<glm::vec3>& path);             // server direction, for tests

} // namespace nmsc::net
