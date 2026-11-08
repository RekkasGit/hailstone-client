#pragma once

// The guild bank in EQEmu's layouts (zone/guild_mgr.cpp SendGuildBank, zone/client_packet.cpp Handle_OP_GuildBank,
// common/patches/rof2.cpp ENCODE / DECODE OP_GuildBank: RoF2's actions are EQEmu's less one); the client's requests
// as CGuildBankWnd sends them (Send_OP_GuildBank_0067cc00 / _0067ea40 / _0067ec10 and WndNotification),
// the server's messages as Handle_OP_GuildBank @0x67e470 reads them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/packet.h"

namespace hailstone::net {

inline constexpr int kGuildBankDepositSlots = 40; // RoF2 GUILD_BANK_DEPOSIT_SIZE
inline constexpr int kGuildBankMainSlots = 200;   // GUILD_BANK_MAIN_SIZE
enum class GuildBankArea : std::uint16_t { Deposit = 0, Main = 1 };
// The window's item key: the slot in the low word, the area in the high one (-1: none).
inline constexpr std::int32_t guild_bank_key(GuildBankArea area, int slot) {
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(area) << 16) | (static_cast<std::uint32_t>(slot) & 0xffff));
}
// Who may withdraw a main-area item (EQEmu's GuildBankPermissions).
enum class GuildBankPermission : std::uint32_t { BankerOnly = 0, SingleMember = 1, PublicIfUsable = 2, Public = 3 };

struct GuildBankItem {
    GuildBankPermission permission = GuildBankPermission::BankerOnly;
    std::string who_for, donator; // who_for: the member a single-member item is for
    std::uint32_t item_id = 0, icon = 0, quantity = 0;
    bool allow_merge = false, usable = false;
    std::string name;
};

// OP_GuildBankItemList (Z→C, while zoning into the guild hall): 40 deposit then 200 main entries, each u8 present
// then, when present: u32 permission, who for, donator, u32 item id, u32 icon, u32 quantity, u8 can merge, u8
// usable, name (NUL-terminated strings). Fewer entries leave the rest empty.
struct GuildBankList {
    std::vector<std::optional<GuildBankItem>> deposit, main;
};
std::optional<GuildBankList> decode_guild_bank_list(const AppPacket& p);
AppPacket encode(const GuildBankList& v); // server direction, for tests

// OP_GuildBank (Z→C): action 0 one slot's update (230 bytes: u32 0, u32, u32, u16 slot, u16 area, u32 shown (0
// empties the slot), u32 item id, u32 icon, u32 quantity, u32 permission, u8 can merge, u8 usable, char[64] name,
// char[64] donator, char[64] who for); 9 the acknowledgement (12 bytes); 4 the deposit's (16 bytes) and 2 the
// promotion's, whose u16 slot, u16 area at +12 the window selects (EQEmu puts its failed flag there).
struct GuildBankMessage {
    enum Kind { Update, Ack, DepositAck, PromoteAck, Other } kind = Other;
    std::uint16_t slot = 0;
    GuildBankArea area = GuildBankArea::Deposit;
    std::optional<GuildBankItem> item; // Update: none when the slot empties
    std::int32_t key() const { return static_cast<std::int32_t>((static_cast<std::uint32_t>(area) << 16) | slot); }
};
std::optional<GuildBankMessage> decode_guild_bank(const AppPacket& p);
AppPacket encode(const GuildBankMessage& v); // server direction, for tests

// OP_GuildBank (C→Z), RoF2's actions; each starts u32 action, u32 the banker's spawn id, u32 (left uninitialised by
// the original; 0 here):
//   4 deposit the cursor item (16 bytes; one more uninitialised u32);
//   2 promote a deposit, 3 view, 6 withdraw, 7 split, 8 merge (24 bytes): u32 key (u16 slot, u16 area), u32 item id,
//     u32 value (view: 1 unless Shift is held; withdraw and split: the quantity; promote and merge: 0);
//   5 permissions (88 bytes): u32 key, u32 item id, u32 permission, char[64] member (zeroed first).
enum class GuildBankAction : std::uint32_t { Promote = 2, View = 3, Deposit = 4, Permissions = 5, Withdraw = 6, Split = 7, Merge = 8 };
AppPacket encode_guild_bank_deposit(std::uint32_t banker);
AppPacket encode_guild_bank_item(GuildBankAction action, std::uint32_t banker, std::int32_t key, std::uint32_t item_id,
                                 std::uint32_t value);
AppPacket encode_guild_bank_permissions(std::uint32_t banker, std::int32_t key, std::uint32_t item_id,
                                        GuildBankPermission permission, std::string_view member);

} // namespace hailstone::net
