#pragma once

// Typed RoF2 world-server packets: authentication, character select,
// character creation and the hand-off to a zone. Layouts follow the NMS
// server's EQEmu RoF2 translator (common/patches/rof2.cpp, rof2_structs.h)
// and world/client.cpp; per-packet notes are in docs/protocol/rof2-packets.md.
//
// encode(x) builds the packet the client sends (or, for server packets that
// tests need, the bytes the server would send); decode_*() returns nullopt on
// a wrong opcode-independent size or a truncated payload. Decoders do not
// check the opcode; the caller dispatches on it.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "net/packet.h"

namespace hailstone::net {

// ---------------------------------------------------------------- client → world

// OP_SendLoginInfo. EQEmu identifies a RoF2 world stream by this being the
// first packet with exactly 464 bytes (LoginInfo_Struct).
inline constexpr std::size_t kLoginInfoSize = 464;
struct LoginInfo {
    std::string account_id;       // login-server account id, as decimal text
    std::string session_key;      // key from OP_PlayEverquestResponse
    bool zoning = false;          // 1 when re-authenticating during a zone change
    std::string custom_files_key; // NUL-terminated at +64; world checks it when the
                                  // World:CustomFilesKey rule is set (empty otherwise)
};
AppPacket encode(const LoginInfo& v);
std::optional<LoginInfo> decode_login_info(const AppPacket& p);

// OP_EnterWorld, client → world (72 bytes).
struct EnterWorld {
    std::string name;
    std::uint32_t tutorial = 0;
    std::uint32_t return_home = 0;
};
AppPacket encode(const EnterWorld& v);
std::optional<EnterWorld> decode_enter_world(const AppPacket& p);

// OP_World_Client_CRC1/2/3 (2056 bytes): checksum and size of a file, then 256
// random dword indices into it and the dwords found there.
struct FileChecksum {
    std::uint32_t checksum = 0; // eq_crc32 of the whole file
    std::uint32_t size = 0;
    std::array<std::uint32_t, 256> index{};
    std::array<std::uint32_t, 256> value{};

    // EQEmu stores (and, when World:EnableChecksumVerification is on, compares)
    // checksum | size << 32.
    std::uint64_t server_value() const { return checksum | (static_cast<std::uint64_t>(size) << 32); }
};
enum class ChecksumKind : std::uint8_t {
    Exe,       // CRC1: eqgame.exe
    SkillCaps, // CRC2: Resources/SkillCaps.txt
    BaseData,  // CRC3: Resources/BaseData.txt
};
// Builds the record the original client sends for this file; `seed` drives
// the sample positions (the original uses its own RNG; only the first 8
// bytes are checked by the server).
FileChecksum make_file_checksum(std::span<const std::byte> file, std::uint32_t seed);
AppPacket encode(ChecksumKind kind, const FileChecksum& v);
std::optional<FileChecksum> decode_file_checksum(const AppPacket& p);

// OP_ApproveName, client → world (80 bytes). World reads name/race/class only.
struct NameApproval {
    std::string name;
    std::uint32_t race = 0;
    std::uint32_t class_ = 0;
    std::uint32_t deity = 0;
    std::uint32_t flag = 0; // the creation window's tutorial flag
};
AppPacket encode(const NameApproval& v);
std::optional<NameApproval> decode_name_approval(const AppPacket& p);

// OP_ApproveName, world → client (1 byte): 1 = name reserved; 0 = rejected
// (also sent when OP_CharacterCreate fails). The client handles 2..5 as
// specific errors, which EQEmu never sends.
std::optional<std::uint8_t> decode_approve_name_reply(const AppPacket& p);

// OP_CharacterCreate, client → world (96 bytes, CharCreate_Struct). Sent after
// a successful OP_ApproveName; the name is the one approved. On success world
// answers with a fresh OP_SendCharInfo, on failure with OP_ApproveName = 0.
struct CharCreate {
    std::uint32_t gender = 0, race = 0, class_ = 0, deity = 0, start_zone = 0;
    std::uint32_t hair_color = 0, beard = 0, beard_color = 0, hair_style = 0, face = 0;
    std::uint32_t eye_color1 = 0, eye_color2 = 0;
    std::uint32_t drakkin_heritage = 0, drakkin_tattoo = 0, drakkin_details = 0;
    std::uint32_t str = 0, sta = 0, agi = 0, dex = 0, wis = 0, int_ = 0, cha = 0;
    std::uint32_t tutorial = 0;
};
AppPacket encode(const CharCreate& v);
std::optional<CharCreate> decode_char_create(const AppPacket& p);

// OP_CharacterCreateRequest: the client sends it empty (make_empty); world
// answers with the combination and stat tables it validates OP_CharacterCreate
// against (world/client.cpp HandleCharacterCreateRequestPacket; DB tables
// char_create_point_allocations and char_create_combinations).
struct CharCreateAllocation {
    std::uint32_t index = 0;
    // Order STR, DEX, AGI, STA, INT, WIS, CHA (as CheckCharCreateInfoSoF uses them).
    std::array<std::uint32_t, 7> base{};
    std::array<std::uint32_t, 7> points{}; // default allocation; their sum is the free-point budget
};
struct CharCreateCombo {
    std::uint32_t expansion_required = 0, race = 0, class_ = 0, deity = 0, allocation_index = 0, zone = 0;
};
// FUN_007f2d00: a combination is offered when the account owns every expansion it requires (world's
// OP_ExpansionInfo bits, GameConnection::world_expansions).
constexpr bool char_create_allowed(const CharCreateCombo& c, std::uint32_t owned) {
    return (c.expansion_required & owned) == c.expansion_required;
}
struct CharCreateData {
    std::vector<CharCreateAllocation> allocations;
    std::vector<CharCreateCombo> combos;
};
AppPacket encode(const CharCreateData& v); // server direction, for tests
std::optional<CharCreateData> decode_char_create_data(const AppPacket& p);

// World's validation of OP_CharacterCreate (CheckCharCreateInfoSoF): the
// (class, race, deity, start_zone) combination must be listed, and each stat
// must lie within [base, base + budget] with the total spent ≤ budget.
// Returns the matching allocation, or nullopt when world would refuse it.
std::optional<CharCreateAllocation> validate_char_create(const CharCreateData& data, const CharCreate& cc);
// A CharCreate for this combo with base stats and the default point allocation.
std::optional<CharCreate> default_char_create(const CharCreateData& data, const CharCreateCombo& combo,
                                              std::uint32_t gender);

// OP_DeleteCharacter: the name with its terminator.
struct DeleteCharacter {
    std::string name;
};
AppPacket encode(const DeleteCharacter& v);

// OP_RandomNameGenerator (72 bytes both ways; world fills in name).
struct RandomName {
    std::uint32_t race = 0;
    std::uint32_t gender = 0;
    std::string name;
};
AppPacket encode(const RandomName& v);
std::optional<RandomName> decode_random_name(const AppPacket& p);

// ---------------------------------------------------------------- world → client

// OP_LogServer (560 bytes).
struct LogServer {
    std::int32_t pvp = 0;       // +4 dword, World:PVPSettings: 0xdcee70 and 0xddfd2c (0 none, 1 Rallos Zek,
                                // 2 Tallon / Vallon Zek, 4 Sullon Zek, 6 Discord)
    std::uint8_t enable_fv = 0; // +8 byte: 0xdcee74
    // +0x18 dword: 0xddfd3c (WorldAuthenticationHandler::OnRoutePacket @0x562f63), the switch Cmd_spellscribe and
    // Cmd_betabuff test (EQEmu leaves it 0).
    std::int32_t spell_scribe = 0;
    std::string world_short_name; // +36, char[32]
};
std::optional<LogServer> decode_log_server(const AppPacket& p);

// OP_ExpansionInfo (68 bytes): expansion bit mask at +64.
std::optional<std::uint32_t> decode_expansion_info(const AppPacket& p);

// OP_SendMaxCharacters (12 bytes): max_chars at +0.
std::optional<std::uint32_t> decode_max_characters(const AppPacket& p);

// OP_SendMembership (RoF2: 116 bytes; rof2_structs.h Membership_Struct). The original reads it into
// FreeToPlayClient::Instance() (GameFeatureRestrictions_ReadEntry @0x7dbaa0): the level at +0, races, classes, then a
// counted array of which it keeps at most 22 entries (@0x57b600 stops at its capacity); the rest is not read.
// Handled at character select and in game (WorldAuthenticationHandler, HandleWorldMessage).
inline constexpr std::size_t kMembershipEntryCap = 22;
struct Membership {
    std::uint32_t level = 0;   // 0 free, 1 silver, 2 gold (EQEmu: always 2)
    std::uint32_t races = 0;   // available race bits
    std::uint32_t classes = 0; // available class bits
    std::vector<std::int32_t> entries; // at most kMembershipEntryCap (EQEmu sends 25)
};
AppPacket encode(const Membership& v); // for tests (the server's bytes)
std::optional<Membership> decode_membership(const AppPacket& p);

// OP_SendMembershipDetails (rof2_structs.h Membership_Details_Struct), GameFeatureRestrictions_UnSerialize @0x7dbe90:
// u32 count, count x {u32 level (0..2), u32 feature, i32 value}; then two counted lists of {u32 purchase id,
// u32 bits} (races, classes; @0x7dbb90), then two strings (u32 length + text: the upgrade URLs).
struct MembershipSetting {
    std::uint32_t level = 0;
    std::uint32_t feature = 0;
    std::int32_t value = 0;
};
struct MembershipDetails {
    std::vector<MembershipSetting> settings;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> races, classes; // {purchase id, bits}
    std::string url, url2;
};
AppPacket encode(const MembershipDetails& v); // for tests
std::optional<MembershipDetails> decode_membership_details(const AppPacket& p);

// OP_GuildsList: 64 zero bytes, u32 count, then {u32 id, cstring name}.
struct GuildEntry {
    std::uint32_t id = 0;
    std::string name;
};
std::optional<std::vector<GuildEntry>> decode_guilds_list(const AppPacket& p);

// OP_SendCharInfo: u32 count, then per character a NUL-terminated name and
// a 274-byte fixed tail (rof2_structs.h CharacterSelectEntry_Struct).
struct CharSelectEquip {
    std::uint32_t material = 0, unknown1 = 0, elite_material = 0, hero_forge_model = 0, material2 = 0;
    std::uint32_t color = 0; // Tint_Struct: B, G, R, use-tint
};
struct CharacterSelectEntry {
    std::string name;
    std::uint8_t class_ = 0;
    std::uint32_t race = 0;
    std::uint8_t level = 0;
    std::uint8_t shroud_class = 0;
    std::uint32_t shroud_race = 0;
    std::uint16_t zone = 0;
    std::uint16_t instance = 0;
    std::uint8_t gender = 0;
    std::uint8_t face = 0;
    std::array<CharSelectEquip, 9> equip{}; // head, chest, arms, wrist, hands, legs, feet, primary, secondary
    std::uint8_t unknown15 = 0xff, unknown19 = 0xff;
    std::uint32_t drakkin_tattoo = 0, drakkin_details = 0;
    std::uint32_t deity = 0;
    std::uint32_t primary_id_file = 0, secondary_id_file = 0;
    std::uint8_t hair_color = 0, beard_color = 0, eye_color1 = 0, eye_color2 = 0, hair_style = 0, beard = 0;
    std::uint8_t go_home = 0, tutorial = 0;
    std::uint32_t drakkin_heritage = 0;
    std::uint8_t unknown1 = 0;
    std::uint8_t enabled = 0;
    std::uint32_t last_login = 0;
    std::uint8_t unknown2 = 0;
};
inline constexpr std::size_t kCharacterSelectTail = 274;
AppPacket encode(const std::vector<CharacterSelectEntry>& v); // server direction, for tests
std::optional<std::vector<CharacterSelectEntry>> decode_character_select(const AppPacket& p);

// OP_EnterWorld, world → client: the character to enter as a NUL-terminated
// name (empty unless auto-login or zoning; while zoning it is the live
// character, which the client then sends back in its own OP_EnterWorld).
std::optional<std::string> decode_enter_world_name(const AppPacket& p);

// OP_MOTD: NUL-terminated text.
std::optional<std::string> decode_motd(const AppPacket& p);

// OP_SetChatServer / OP_SetChatServer2: "host,port,server.Character,<type><key>".
struct ChatServerInfo {
    std::string host;
    std::uint16_t port = 0;
    std::string server_short_name;
    std::string character;
    char connection_type = 0; // EQ::versions::ucs* letter
    std::string mail_key;     // 8 hex digits
};
std::optional<ChatServerInfo> decode_chat_server(const AppPacket& p);

// OP_ZoneServerInfo (130 bytes): ip[128] text, u16 port (little-endian).
struct ZoneServerInfo {
    std::string host;
    std::uint16_t port = 0;
};
AppPacket encode(const ZoneServerInfo& v); // server direction, for tests
std::optional<ZoneServerInfo> decode_zone_server_info(const AppPacket& p);

// OP_ZoneUnavail: zone short name in the first 16 bytes.
std::optional<std::string> decode_zone_unavail(const AppPacket& p);


} // namespace hailstone::net
