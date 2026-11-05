# Login server protocol (SoD/RoF2 port)

Implementation: `src/net/login.{h,cpp}` (`LoginClient`, `login_codec`), `src/net/des.{h,cpp}`. Tool: `netprobe [-v] login HOST[:PORT] USER PASS [SERVER_NAME]`. Tests: `tests/net_login_test.cpp`; the live check runs only with `NMSC_LIVE_LOGIN=host:port`.

Server side: EQEmu `loginserver/client.cpp`, `login_types.h`, `world_server.cpp`, `encryption.cpp`, `login_util/login_opcodes_sod.conf` (read as a spec, nothing copied).

EQEmu's login server listens on separate ports per client family; the RoF2 client uses the "SoD" port (`client_configuration.sod_port`, default 5999), whose opcodes come from `login_opcodes_sod.conf`. Encode passes are none, CRC 2 bytes (see `session.md`). The UDP session options (`LoginClient::Options::session`): max packet 512, no-data timeout 14.8 s, multi-buffer hold 0, no stat requests, resend adjust 2000 (T = min(1.25 rtt + 2000, 8000) ms), 100 reliable slots.

## Opcodes

| Opcode | Name | Direction |
|---|---|---|
| 0x0001 | OP_SessionReady | → |
| 0x0002 | OP_Login | → |
| 0x0003 | OP_Logout (unused here) | → |
| 0x0004 | OP_ServerListRequest | → |
| 0x000d | OP_PlayRequest | → |
| 0x0016 | kick request (remove the logged in character; not in EQEmu) | → |
| 0x0017 | OP_ChatMessage | ← |
| 0x0018 | OP_LoginAccepted | ← |
| 0x0019 | OP_ServerListResponse | ← |
| 0x0022 | OP_PlayResponse | ← |
| 0x002f | order-expansion reply (DES-encrypted; ignored) | ← |
| 0x0025–0x002d, 0x0030, 0x0031 | server pushes (chat, GM list, banner, poll, game features, OP_LoginExpansionPacketData; ignored) | ← |
| 0x0032 | the kick request's reply (base reply; not in EQEmu) | ← |

All messages go on the reliable channel.

## Message format

Every message is one application packet, little-endian: **opcode u16 | sequence u32 | compressed u8 | encrypt type u8 (2 = DES) | unused u32 | body** (a 12-byte header; in `AppPacket` the opcode is separate and the data starts with the sequence). Replies begin their body with **success u8 | error id u32 | error text\0** (eqlsstr_us.txt ids; 101 = no error).

- **Encryption.** Type 2 is DES-CBC with an all-zero key and an all-zero IV, fresh for every message; the body is zero-padded to a multiple of 8. The client encrypts only OP_Login. It decrypts only replies 0x18 and 0x2f, whatever their header says (the reply class sets the type), over the body rounded up to a multiple of 8. The all-zero key is a DES weak key, so encrypting twice gives the plaintext back.
- **Compression.** The client never compresses. A reply whose byte 6 (the compressed flag) is 1 is inflated with zlib before it is decrypted.

## Flow

1. **Session** handshake (`session.md`). No session within 10 s, or a failed one: the next host (eqhost.txt `Host`, `Host1`, …; `LoginClient::start` takes a list); none left: **6011**.
2. **OP_SessionReady**: header + u16 **11** (14 bytes). Reply **OP_ChatMessage**: base reply and one more string (`SCHandshakeReply` +0x20; EQEmu sends it empty); only the success byte is checked. EQEmu hard-codes sequence 2 in it.
3. **OP_Login** (encrypt type 2): DES of **user\0 password\0 token\0 flag**. A password login is `user 00 password 00 00 00` (the token empty, flag 0); a token login is `00 00 token 00 01`. The client uses the password form. Those two extra zero bytes can add an 8-byte block compared with a plain `user\0password\0`.
4. Reply **OP_LoginAccepted** (decrypted): success u8 | error id u32 | text\0 | u8 | u8 | **account id** u32 | **session key**\0 (read to its NUL) | failed attempts u32 | show player count u8 | 7 × u32 offer fields | token string | commerce string. EQEmu writes the key as a fixed 11-byte field (10 characters and a NUL), which reads the same. Failure: the reply's error text (EQEmu sends **105**); the password box is cleared. Failed attempts > 0 after a success: **6018** "There have been %1 failed login attempts…" (`LoginClient::notice()`).
5. **OP_ServerListRequest**: header only. Reply **OP_ServerListResponse**: base reply | count u32 | per server: address\0, type u32 (1 standard, 8 preferred, 16 legends), id u32, name\0, country\0, string\0 (EQEmu: language), status u32 (0 up, 1 down, 4 locked; flags), players u32. While the list is shown it is requested again once more than **60 s** have passed since the last request, and the answer replaces it.
6. **OP_PlayRequest**: header + server id u32 (16 bytes). Refused locally with **6028** (nothing sent) when the server's country, mapped through the locale table (first two characters: us kr tw jp de fr cn, anything else us), is not the client's locale (us), unless the login reply set show-player-count. Reply **OP_PlayResponse**: base reply + server id u32; the id is looked up in the current list for the world address. Errors: EQEmu's 111 (already online), 326 (unavailable), 337/338 (suspended/banned), 339 (full); **302/114** mean a character is still logged in and the server offers to remove it (`PlayResult::offer_kick()`), below.
7. **The kick** (eqmain: ServerSelectWnd::OnJoinServer @0x10037350, the request at @0x100388dc, its reply's handler @0x10037740). After 302 or 114 the login stays up and the player is asked the reply's text (Yes / No; `LoginState::KickOffered`, the client's own box over the server select). No: back to the list. Yes: the kick request **0x16**, header only (its class at 0x100209b0 writes no body), pending **60 s** with the server's id; "Please wait while the currently logged in player is removed from the world." (**6075**) shows meanwhile (`LoginState::Kicking`). Its reply **0x32** is a base reply (LoginClient callback +0x78): success sends the play request for the same server again (10 s); failure shows the reply's error. A timeout is the canned reply: success 0, **103**. EQEmu's login server sends neither 302 nor 114 (its "already online" is 111, a plain refusal, sent only with the rule `World:EnforceCharacterLimitAtLogin`) and has no handler for 0x16, so on EQEmu this path is never taken; a kick request it got would time out with 103.

## Sequence numbers and pending requests

One counter per login connection (a new one for each connect attempt) starts at 1 and is incremented **before** each request is built, wrapping to 1 at 0xffffff: handshake 2, login 3, list 4, play 5; each list refresh takes the next number, so a play request after one refresh carries 6. Every request is pending with a timeout: handshake 10 s (nothing happens when it runs out), login 20 s and list 120 s (**103** "A timeout occurred"), play 10 s (**324**). A reply whose sequence is below 0xffffff must match a pending request, which it then removes, or it is dropped, unless its opcode is a push (0x25–0x2d, 0x30, 0x31); the reply's opcode is not compared with the request's. EQEmu echoes the request's sequence (and hard-codes 2 in the handshake reply, which a fresh connection always uses). A connection the server drops after the login: **112**.

Error texts come from eqlsstr_us.txt (`load_login_strings`, loaded by the client at start-up), with Hailstone's own English wording (not the stock file's text) for the ids the client uses when the file is missing; an id with no text reads "Login message <id> (no text for it).".

The `LoginClient` state machine: `start()` → Connecting → Handshake → Authenticating → **LoggedIn**; `request_server_list()` → RequestingList → **ServerList**; `request_play(id)` → RequestingPlay → **PlayAccepted**; any error → Failed with `error()` and `error_id()`.

## Hand-off to the world server

`LoginClient::ticket()` returns a `WorldTicket` once the play request is accepted:

- `world_host`: the IP string of the chosen server from the server list;
- `world_port`: 9000 (the RoF2 client always connects to world on 9000);
- `account_id`: the login account id from OP_LoginAccepted;
- `session_key`: the 10-character key from OP_LoginAccepted.

The world connection's first packet is OP_SendLoginInfo (0x7a09, 464 bytes): the account id as **decimal text**, `00`, the session key, `00`, …. EQEmu's world parses the name with `Strings::ToInt` and checks the pair with `ClientList::CheckAuth(id, key)`. The login session can be closed once the ticket is in hand.

## Live check (2026-10-21, EQEmu login server on 127.0.0.1:5997)

`NMSC_LIVE_LOGIN=127.0.0.1:5997 NMSC_LIVE_REFRESH=1 nmsc_tests -tc="live login*"` (account nmscgrp1): login (sequence 3), list (4), the list again after 60 s (5), play (6). The login server logged `Play received from client [nmscgrp1] server number [2] sequence [6]` and answered with sequence 6; the client then entered the world and zoned in as before.

## Live check (2026-10-20)

```
$ netprobe login 192.0.2.10:5997 nmsctest1 nmsctest1 "Dev"
connecting to 192.0.2.10 port 5997 as nmsctest1
login accepted: account id 2, session key ##########, failed attempts 0, show player count no
1 server(s):
  id   name                           address          status         players type
  2    Dev                            192.0.2.10   up             76      1
requesting play on 2 (Dev)
play accepted (server 2)
world connection: 192.0.2.10:9000, OP_SendLoginInfo login "2" password "##########"
```

The login server logged `New SoD+ client connection`, created the account, `Successful login for user id [2]`, the play request with sequence 5, and `Sending play response`.
