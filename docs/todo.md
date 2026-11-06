# To do

What is left before the client is feature complete, in the order to work on it. The details of each gap are in `docs/unknowns.md`; this list only orders them. Tick an item when it is done and tested, and remove its row from `docs/unknowns.md` at the same time. Add new gaps to both.

Written 2026-10-28 from `docs/unknowns.md` and `docs/plan.md`.

## 1. Gameplay a player will hit

### Combat and spells (do first)
- [x] Missiles: a despawned source or target as DeleteMyMissiles handles it; flying through walls with a target is the original's behaviour (60b1934). Not tested live (no archer).
- [ ] Spell missiles (item type 9) with a valid spell id: the original drops them without one, and EQEmu never sends one, so this only matters for a server that does.
- [x] Environmental hits 0xf3 to 0xfd: animations, chat lines, sounds, and our own fall, drowning and lava damage reported like ApplyDamage (eb42f20; falls tested live).
- [x] ReportSuccessfulHit's shared tail: the spell book closes and a sitting or mounted player stands when damage lands; a spell heal from OP_Damage (ff39361; the stand checked live).
- [ ] The blood spurt: the trigger is known, the BSPURT_BCD / I_BLOODSPRITE definition and point 6 are not.
- [x] Linked spell timers and discipline timers from the player profile.
- [x] Item recast on cast items (already in 789134d).
- [x] The distance modifier of effect values, and the extra long-buff slots from level 72 and 75.
- [x] Buff tick timers held by SPA 0x1b4 and suspendable spells.
- [x] DoPassageOfTime's 6 s tick runs whole in the client (ProcessAffects' buff HP, mana and endurance, local expiry at 0, the mesmerize break; HP, endurance and mana regen) and its 1 s endurance upkeep; unit and connection tests, not tried live.
- [ ] The passage-of-time gaps in unknowns.md: CharacterBase::Status for the bleeding line, the running penalty of the endurance regen, StopCurrentSpellCast(1), the drunkenness update, the Eye of summons.
- [x] Combat line filters 13 to 16.
- [x] Hand-to-hand delay and fist damage from the profile (EQEmu sends 0); the player's own BodyWet in the attack checks.
- [x] `AltAttack` and the attack lock match the original; the rank swap's limit and GM check ported (never swaps on EQEmu's gold membership).
- [x] Ranged attacks do not turn the player in the original either.
- [x] Held buffs seen live (a SPA 436 buff freezes Courage's countdown). Melee checked by hand.
- [ ] Distance-scaled spell effects live (needs a spell with a distance modifier cast at range). Buff caster ids and temporary pets in the line filters are in unknowns.md.
- [x] Commands: `/discipline`, `/doability`, `/stopdisc`, `/memspellset`, `/spellscribe`, `/targetgroupbuff` / `/tgb`, `/autofire`, `/melody`, `/dismount`, `/aggrometer`, `/buyer`, `/claim` (f3c5c5d).
- [ ] `/gems`: the Gems mini-game (CGemsGameWnd, 0x6705f0 to 0x6734e0), a port of its own.
- [x] (358bf04, checked live: /discipline now runs the NMS reuse timers; the empty list after login matches the original) A second `/discipline` right after `/stopdisc` is not refused by the client (NMS disc timer map or a missing server timer packet); the reuse list is empty after zone-in until the server sends timers.

### Movement
- [x] The game rules on a 60 Hz game clock (`game.fixed_step`, docs/mechanics.md "Frame rate"): movement, other spawns, missiles, weather and fog steps, the frame-gated timers; the camera drawn between steps (unit tests at 15 to 1000 simulated frames a second).
- [ ] Collision radius: the static reading gives the model's bounding radius (6.2 for a human), too wide for doorways; settle it with a run-time read of the actor's vf44 in the real client (rof2re's radius_log, needs a new rof2re.dll in the game folder and a user-run session). The stand-in stays until then.
- [ ] Other spawns: bodies never block in the original (no per-bone meshes); only bone attachments such as held weapons could, and that test is not ported.
- [x] NPC collision (Hailstone's own, switchable by the server's hailstone.cfg `[movement] npc_collision`): live NPCs stop the player; players, pets, mercenaries, corpses and mounts do not (game/npc_collision.h; unit tests).
- [x] The jump's crouch window from the duck clip's length (453db92); the ceiling query and auto-duck match the original apart from the radius.
- [x] New-style models' duck clip for the crouch window (CRCH_BA_1, FUN_00406740's row 0x7a - 0x4f); the jump animation from the playing animation (+0x10c0, vf24) rather than 500 ms after a jump (anim-fixes; unit tests).
- [x] Lifts carry riders (already worked; checked live on the Paineel lift).
- [x] Teleport doors (a Bazaar IT78 door, the PoK books' type, checked live; NMS sends PoK visitors to the Bazaar).
- [x] Touch-activated switches: PlayerClient::TouchingSwitch ported, traps checked live (f500930).
- [x] Boats, as our own design (docs/mechanics.md "Boats"): riding with hull floors and walls, smoothed boat motion, other riders, a zone-in onto a deck, climbing aboard, sailing a small boat at the helm (dee94fe..873439e; all checked live in Butcherblock and OOT).
- [x] Boats with a live asset folder: not the client's job. The classic boat models (LAUNCH etc.) are only in RoF2's per-zone `_chr` archives, which live dropped; a server that wants boats on live assets supplies those models in its `client_files/` (user's decision, 2026-10-29).
- [x] Boats: overlapping decks (the nearest, the one stood on in a tie), the boat's own animation in its collision (its triangles taken again every 200 ms), the helm against docks and other boats (hull probes; the step is refused and the boat stops, the turn alone may still swing it), rails on a moving deck (the LAUNCH skiff's flat one included); unit tests only (tests/boats_test.cpp).
- [ ] Boats, check live: walking from a dock onto a boat; the helm against a real dock and another boat; the rails on the LAUNCH skiff; the landing on the Timorous shuttle (NMS sent the character to the Bazaar: where a boat script puts the player is the server's, so that is for the NMS server's quest script, not the client). Seen live on the NMS dev server (2026-10-31, qeytoqrg's row boat): a zone-in onto the deck held the player until the hull loaded and stood it on the boat, A on the boat asked for and got the helm (OP_ControlBoat), the boat sailed and turned; walking off a still boat let go of it. Still open: the helm's stop against a dock or another boat (not seen to happen in the screenshots, night and no log line), boarding a moving boat from a dock, the LAUNCH skiff's rails (Butcherblock's Captains_Skiff, grids 232 / 233, not tried).
- [x] Zone points that request a teleport by index: sent already; the player is now held while waiting (untested, EQEmu never sets them).
- [x] Reachable zone lines in toxxulia, mistythicket and oldbloodfield: a client fallback fires buried EQG zone-line boxes from the ground over them (81d4a59; 39 boxes across the EQG zones; all 7 lines in the three zones checked live). oldbloodfield's ATP_3 has no zone point 30 in the server's table.
- [x] Position history (PositionTracker, OP_FloatListThing), the retried and the 30-minute saves, closing on loading screens (60069a0). Missing samples are listed in unknowns.md.

### Items and commerce
- [x] Partial-stack looting asks the quantity (EQEmu ignores it); Link All; the loot music (entry 5 has no asset).
- [x] Ctrl + right-click bag swapping; Ctrl+click bag contents' slot order and refusal text (game/bag_move).
- [x] The tradeskill window: favourites, request waits, combine checks, trivial colours (experiment and auto-combine were already in).
- [x] Hailstone's tradeskill window: each component's carried count (green / red), how many can be made, Combine / Combine N / Combine All with each result put away and a stop when the inventory is full (docs/mechanics.md; unit and mock tests, not seen live).
- [x] Trades: the item request counter in IsOkToTransact; a busy answer cancels the trade.
- [x] Bazaar purchase: EQEmu's TradeRequestFailed reads as a failure (5cf3f63; mock test only).
- [x] Seen live (5ba3a3b and before): a stock-loot corpse with a partial stack, Link All (a crash without a loot session fixed) and Loot All, a two-client trade with coin and the busy refusals, Ctrl bag moves and the bag swap, the tradeskill search and favourites (the modern window's selected recipe fixed), the bandolier's Make Hot Button and Auto Swap, the merchant's Qty and Lvl.
- [x] Link All in the gamepad loot window (unit tests); the inventory doll drop in the modern pack (checked live). Open: the bandolier button actually swapping weapons.
- [x] rof2decomp `notes/items-and-inventory.md` §2.7: the swap list's room mode is 3 for every entry, not "3 + i".
- [x] Bazaar: Find Trader, Hide Traders, the result row's Buy button, the stall check, the price load of a selected slot (f7401f3; Hide Traders by unit tests only).
- [x] Barter: the feature check, offline mode, Hide Buyers (f7401f3; Hide Buyers and the feature check by unit tests only).
- [x] Mail: the ignore list (kept and edited only, as in the original); the composition window's From (351d492).
- [x] Mail's From seen live after a mail login: "SOE.EQ.nmsdev.Nmscmistwo" (2026-10-29; the chat server answers logins again).
- [x] Bazaar: the PC and NPC target keys skip hidden traders and buyers (unit tests). The NMS bazaar's porch stalls (type 153) lie outside the zone geometry, so only the FRUGS302 rugs work as stalls (server data).
- [x] Bazaar: a right click on a trader or buyer the server sends as an NPC (Project Triune's offline traders) opens its shop or barter window: RightClickedOnPlayer tests the trader / buyer flag after the service classes for any spawn, not only players (mock test; the right click on the Triune bazaar traders not retried live).
- [x] Bandolier: Make Hot Button, Auto Swap saved, the slots' held right button (the original has no slot drag handler).
- [x] Merchant: the Qty and Lvl columns (checked live).
- [x] The marketplace window (CMarketplaceWnd) in the modern and gamepad packs: the original's store packets (catalog, states, wallet, purchase and its answer, the window's preview), the category tree, find, sort, the landing page and item of the week, quantity, the zip and confirmation pages, `/marketplace`, its key and the EQ button (unit and mock-server tests; live on Project Triune 2026-11-01: the catalog's 523 rows shown after Ready). Open: buying, and the rows docs/unknowns.md lists.
- [x] Merchant: the inventory money buttons' hook on the Parcels page (FUN_00698290; the page itself was built 2026-10-28, 5a71bbc / dd8f339): the bag item let go, the coin outlined, the quantity answer picks the money to send (unit and mock-server tests). Checked live on the NMS dev server (2026-10-31): 5 pp picked through the coin-button path and sent by parcel left the purse once (500 to 495, one OP_MoneyUpdate), EQEmu stored it in `character_parcels` (5000 copper, the note) and the recipient claimed it (0 to 5 pp); in the gamepad pack's send page the purse row shows the coins and A on platinum, then the quantity, fills Send money ("495pp") with the purse unchanged (playprobe `--parcel-money`, `--parcel-claim`; screenshots).

### Network and session
- [x] Declining a group invite: the client handles 0x2a50; stock EQEmu maps it to nothing, so the server script relays it (`group_declines`, 5cf3f63).
- [x] The updated server script deployed on the NMS test server (`install.py`, `#reload quest global`); the group decline checked live: the inviter sees "Nmscmistwo rejects your offer to join the group." (2026-10-29).
- [ ] The server script's limits (files_rescan, files_chunks_per_second with the requests over it held on a timer, eight files kept open, a file changed since its hashing, 5 group declines a sender in 10 s; Lua tests against a stand-in for EQEmu's API, tests/server_script_test.cpp): check live on an EQEmu server, a first download of a large client_files/ with two players at once, and with a low files_chunks_per_second.
- [x] Server files: a piece that does not come is asked for again (at once when a later one comes, after 3 s of silence otherwise; 5 tries), so one dropped request no longer fails the download (unit tests against the fake script).
- [x] A refused duel is reported to the challenger (EQEmu's 12-byte OP_DuelDecline; checked live).
- [x] The duel partner leaving or dying keeps the original's 8-byte OP_DuelDecline: EQEmu's handler would dereference the departed partner's missing entity, and ends the duel itself (mock test).
- [x] Strings the client sends fit stock EQEmu's fields: a tell's recipient (63) and text (2047) as SendNewText cuts them (EQEmu's DECODE strcpy's the names into `char[64]` on its stack), the bandolier name (31), our own name in the world and zone requests, the name-change answer, the barter lines' names (mock and unit tests; the audit's list is in the commit).
- [x] /guildstatus sends the original's 140-byte GuildStatus_Struct, which EQEmu accepts (it dropped the 128-byte one) (mock test).
- [x] The trade partner leaving the zone cancels and closes the trade, as msgEQRmPlayer does (mock test).
- [x] Keep-alive packets: `00 06` after 15 s idle (14.8 s login, 44 s chat) (515e7ef; checked live, EQEmu dropped an idle client at 60 s without them).
- [x] Login: the kick request after 302/114 (eqmain's 0x16 / 0x32) with a Yes/No box (5c3a3cc; EQEmu never sends 302/114, mock tests only).
- [ ] Login: the reply's two leading bytes (unused by OnPlayerLogin), pushes 0x25 to 0x30 (EQEmu sends only 0x31).
- [x] The world and zone link-death timeout is the original's 180 s of silence (Params 0x12, UdpConnection +0x15c), not 15 s; 15 s is the keepalive delay (Params 0xe) and /offlinemode's exit window (0x3a98 @0x53ddc3). Login (14.8 s) and chat (44 s) keep the client's limits (docs/unknowns.md). Checked live on the NMS dev server (2026-10-31): a character idle in South Qeynos for 270 s stayed connected and camped out normally.
- [x] rof2decomp notes: Params 0xe is the keepalive delay, not the no-data timeout (checked at 0x8c4197 / 0x8c61c4).
- [x] Session limits against a hostile or broken server: early packets bounded in bytes (4 MiB) as well as count, the delivered-but-unread queue bounded (64 MiB, then the session ends), an ack for a packet not yet sent ignored, a too-short first fragment ends the session instead of reassembling garbage or silently losing what follows, datagrams failing the CRC no longer keep the link alive, oversized datagrams dropped on every platform (unit and mock tests).
- [ ] What the original checks in OP_OutOfSession, and whether its last-receive time is set before its CRC check (unknowns.md).

### Real estate (player housing, guild halls)
- [x] The packets, the manager (catalog, records, access, item rows, owned lists, areas, placed objects, occupancy), result messages, upkeep time left; unit and mock-server tests.
- [x] Buy / sell / transfer checks, the offer dialog, escrow deposit from and withdrawal to the cursor, /realestate, /leaverealestate, the windows' keys.
- [x] The guild bank in every guild hall zone; an instanced hall only for the guild whose plot holds it.
- [x] The plot, manage, items, neighborhood and plot search windows and the item context menu, in both packs.
- [x] Placed objects drawn in the world (collision, right-click menu), placement mode with its keys, the area check and the 0x711a confirm.
- [ ] Placement: the ghost's wall and surface fit, free mode's mouse moves, the flatness test (unknowns.md).
- [ ] Play it live against a server with housing (Project Triune) and settle the rows of the real estate section in unknowns.md.
- [ ] The item-move pre-checks and button greying, alternate-currency deposits, the NPC-item binding, the item menu's unbuilt entries, the neighborhood polls and vote window (unknowns.md).

## 2. Looks and sound

- [x] Animation layering: the original's upper/lower bone groups, actions over the legs, per-group cross-fades and SyncToBoneGroup, rider actions on the arms, other spawns' water (d8c9fc2; model viewer and unit tests, live split not seen in a screenshot).
- [x] Animation: a busy upper body queues (classic, +0x10c4) or drops an action as the original does; the same action never replays over itself (84046fb; unit tests).
- [x] Animation: classic P08 / O02 after an animation FUN_004b0860 does not name (the run, jumps, socials; not after an action, which keeps P01), the follow-up's +0x10c4 = 0x47; the local player's climbing animation on a ladder (anim-fixes; unit tests). Checked live on the NMS dev server (2026-10-31) on another player's Luclin human (HUM from globalhum_chr, an old-style model with P01 and P08) seen from the front by a second client: standing after zoning in and after a walk stop it plays P01 (elbows out, fists at the hips), after a run stop and after `/wave` P08 (arms hanging straight), matching the model viewer's P01 / P08. The local player's own stand was not seen from the front (the camera could not be turned round in a scripted run). The ladder climb could not be tried: this server has no ground object named LADDER*, the only kind the client climbs; its two OBJ_LADDER doors (xorbb door 8, pillarsalra door 7) are switches here (docs/unknowns.md).
- [ ] Animation: other spawns' climbing (their +0x24 from DoCollisionMovement's ladder hits), new-style riders' column-5 sequences, the new-style style column.
- [x] Ladders placed in the zone (objects.wld LADDER* actors, type 6): walking into one climbs it (CollidingType 6 from the sweep's hit), a click from the top mounts it (zone-ladders; unit tests). Not tried live.
- [x] Passable ladders (Cabilis's LADDERCB101, every polygon passable): clickable from the top as in the original, and climbed by walking or swimming into them by Hailstone's own rule, switched by `hailstone.cfg` `[movement] passable_ladders` (canal-ladder; unit tests, and the real Cabilis East zone with `EQ_GAME_DIR`). Not tried live.
- [x] rof2decomp: the upper/lower naming in player_animation_{start,layers,play,rows}.cpp fixed, differential tests pass (uncommitted in rof2decomp).
- [x] Luclin eye colours and `SwapFace` (e7a5849; model viewer). Open: one-texture Luclin materials' tint (female dwarf faces 2 and 3).
- [ ] Hero's forge pieces without bones on Luclin models: narrowed (bracers' UVs land on the mail fill; gloves beside the hands); needs the DLL's rigid attach traced (ChangeBoneAttachment @0x10042c60).
- [x] Head items: the head's armour colour on helms (110650f); "keep head" has no effect in the original; the Velious helms beyond the table don't exist in the RoF2 files.
Fog, weather, sky, EQG surfaces, lighting and spell particles are a look-and-feel pass, not ports (user, 2026-10-29: "make it look good"); branches `look-atmosphere` and `look-surfaces`.

- [x] Fog and haze: eased between places, the sky's colour, a storm band, lightning (e069ae2). The original's area fog has no data in either install.
- [x] Weather transitions over tens of seconds (e069ae2).
- [x] Sky: a tilted sun path, ozone, ambient from the sky model, a colour grade (09624d4). Rough: dense forest shade, red dusk haze in lopingplains/commons, everfrost flat, befallen blue.
- [x] EQG: water reflects the drawn sky and the noswap environment maps, modern lava (crust, LavaH walls), blood/coloured falls, foam only on rising floors, animated texture sidecars, the one unpainted heightmap tile (scorchedwoods) (look-surfaces). Open: heightmap lighting colour, glow maps in the classic look, planar reflections, TAA.
- [x] Upscaling: a render scale (`display.render_scale`, 50 .. 100 %) with AMD FSR 1 (EASU + RCAS, `display.sharpness`) (render-scale; zone viewer with validation, not seen in game or with /viewport).
- [ ] Upscaling, temporal groundwork (docs/graphics.md Roadmap 6): projection jitter; a motion-vector target from every scene pipeline (last frame's camera, bones, wind time, door matrices); history reset on zone-in, teleport and resize; texture LOD bias.
- [ ] Upscaling: TAA/TAAU on that groundwork, then FSR 2/3.1 (MIT) as the temporal upscaler; reactive mask for particles, spells, water and lava.
- [x] Doors: orientation already matched SetEulerRotation512 (655 of 665 hinged doors in 21 zones fit their doorways); invisible switches now block movement as in the original; harbingers' BCN loads from its .mds (c34ca9e, d59971c; walked live in halas and felwithea). Open: BCN's actordef name, piece and animation; the door-angle sign.
- [x] Lighting: characters and doors take the light of their place (ambient probe), tinted baked light balanced (192ec47).
- [x] Spell particles: the EDD billboard modes (flat and axis), animation-started actor emitters (look-surfaces; billboard meanings read from names, not traced).
- [x] Audio: the local player has no loop sound in the original; `/reversesound` has no effect in RoF2, here it swaps the whole output's channels (our choice) (f7b19af).

### Updates (docs/updater.md)
- [x] The updater: the signed manifest from the release feed, the download (core, browser and the installed plugins as their own packages, an unchanged one skipped), the install at quit with Restart now, the rollback of a cut-off install or of a version that does not start, and the notice in both packs' login screens and the client's own. Unit tests; a real run on Linux against a local HTTP server (`hailstone_sign try`: packages from `make_update.py`, signed, downloaded with the system's libcurl, installed over a copy of an install).
- [x] The login screen's corner: this build, whether it is up to date, What's new (this version's part of the shipped CHANGELOG.md) and the GitHub page, in both packs and the client's own (tests/changelog_test.cpp, tests/lua_eq_pregame_test.cpp, tests/ui_pack_windows_test.cpp, tests/dev_ui_test.cpp). Open: seen with a real update check (only test runs and unit tests so far).
- [ ] The repository (or a public one for releases) has to be public: GitHub answers 404 to the client for a private one's releases.
- [ ] A release key (`hailstone_sign keygen`, the public half in `update-key.pub`); until then builds have no updates.
- [ ] The updater on Windows: WinHTTP and the install's renames of the running `hailstone.exe` and its DLLs (compiles; not run).
- [ ] A release job in CI (packages, manifest, signature, upload), and macOS (an app bundle's own install and code signing).
- [ ] Downloads that resume (HTTP range requests) instead of starting over.

### Add-ons (docs/addons.md)
- [x] The manifest and GitHub link parsers, the library header reader; unit and fuzz tests; `tools/make_addon.py`, and `hailstone_sign` for add-on manifests.
- [x] Install and uninstall from a GitHub link on the login screen, scripts and plugins (content rules, the Lua and library checks, `addons/installed.txt`, a journaled install undone when cut off); the server's own listed and not removable; the Modern pack's card, the Gamepad pack's page and the client's own window.
- [x] The add-on store: every add-on of the signed add-on index, with its manifest (held to the index's name and key), Install through its install card; the Modern pack's add-ons card beside the characters card (tests/addon_index_test.cpp, tests/lua_eq_pregame_test.cpp, tests/ui_pack_windows_test.cpp, tests/dev_ui_test.cpp). Open: listing the real index live, and lines in the index for the add-ons beyond Project Triune's.
- [x] Lua Scripts window (`ScriptsWnd`, both packs, the EQ menu's Lua Scripts row, Gamepad More > Lua Scripts): every script in the folders with its state, Run / Stop / Pause, the reason a script may not run, and a per-character Autorun list (`scripts.ini`) started at the login's zone-in, not again at a zone change. Unit tests (the scan, the autorun file, the rules through a zone change) and both packs drawn headless with a script host. Open: tried in a live session in both packs (a script autorun at login and kept running across a zone line).
- [x] The manifest's `caps` as a ceiling on what an installed add-on gets (`plugin::Policy::set_ceilings`).
- [x] Plugin and add-on windows confined in the UI's Lua state: their own globals, `eq` / `ui` / the libraries read-only, their code (and every `eq` function they took) run under their capabilities, text-only loads, `dofile` kept to the plugin's and the pack's folders (tests/lua_folders_test.cpp, tests/lua_eq_pregame_test.cpp).
- [x] An update's approval holds only for the capabilities and version the player saw (tests/addon_manager_test.cpp).
- [x] The player's word wins over a check under way: an add-on removed meanwhile is not installed again, auto-update switched off meanwhile is not overridden, and an update keeps the switch as it is at install time (tests/addon_manager_test.cpp).
- [x] Scripts started by a script or plugin get no more than their starter; `scripts/config/` never runs as code (tests/mq_lua_test.cpp).
- [x] A server's compiled plugins load only once the player allowed their bytes for that server (`servers/<profile>/plugin_approvals.txt`, the prompt in both packs and the client's own, changed from the add-on list; tests/native_plugins_test.cpp, tests/plugin_consent_test.cpp, tests/ui_pack_windows_test.cpp).
- [x] A module runs only in a script it may do as much as: an add-on's module is refused in a script that may do more (tests/mq_lua_test.cpp).
- [x] Binding a command needs 'commands'; scripts and plugins cannot shadow the game's or the client's own commands (`Hooks::new_guest`, `reserved_command`; tests/plugin_hooks_test.cpp, tests/mq_lua_test.cpp, tests/native_plugins_test.cpp).
- [x] The clipboard (`ui.GetClipboardText` / `SetClipboardText`) only for scripts with every capability (tests/mq_lua_test.cpp).
- [x] A script's or plugin's `/plugin <name> load` never shows the consent question; only the player's own asks again (tests/native_plugins_test.cpp).
- [x] Values between Lua and plugins: cycles refused, a budget of values and bytes (`plugin/lua_value.h`; tests/lua_eq_args_test.cpp).
- [x] Script and module names stay inside the script folders (no absolute names, no links out); the config check canonical and case-insensitive (tests/mq_lua_test.cpp).
- [x] `mq.pickle`: a script without every capability gets `scripts/config/<script>/` (tests/mq_lua_test.cpp).
- [x] Host functions only on the client's thread: off-thread calls do nothing (`MainOnly`), the caller's capabilities are none there (tests/native_plugins_test.cpp, tests/plugins/HostProbe.cpp).
- [x] C++ exceptions out of a plugin caught at every entry point (`guarded`); a failed `InitializePlugin` leaves nothing registered (tests/native_plugins_test.cpp).
- [x] `web_state` fills what a shorter `NmscWebState` holds; its `size_t` size kept for ABI 3 (tests/native_plugins_test.cpp).
- [ ] The server plugins' prompt live: a first visit to a server that ships a plugin (NMS), Allow mid-zone and Don't allow, in both packs and with a gamepad; whether a server extension allowed mid-zone needs the next zone-in.
- [x] Plugin windows' functions left in the tables the pack keeps in its globals (`GPSTATE`, `modern`, `ui_palette`) ran as the player when the pack called them: the plugin now sees those tables through views that wrap what it writes to run as the plugin (and to stop when it unloads), and refuse setmetatable, rawset and `__gc` (ui/lua/lua_host.cpp).
- [ ] A function or table a plugin's code passes to a pack function as an argument is not wrapped: a pack function that keeps it in its own tables and calls it later runs the plugin's code as the player. Wrapping every value that crosses into pack code (a full membrane) would close it, at a cost on every call; the packs' shared libraries that keep callbacks should keep them per window instead.
- [x] Auto-update: the check at start (`addons.check_hours`), locally modified files, files moved only at the login screen, approval when the caps grow.
- [x] Signatures: the key pinned at the first install, an update with another key (or unsigned) refused.
- [x] Suggested add-ons: the signed add-on index (projecthailstone/hailstone-addon-index) and a server's `[addons] suggest`; a line on the server select, Look at it / Not now / Don't ask again on the login screen in both packs and the client's own; a suggestion's card held to the index's name and key (tests/addon_index_test.cpp, tests/lua_eq_pregame_test.cpp, tests/ui_pack_windows_test.cpp).
- [ ] Suggested add-ons live: a first login to Project Triune from a fresh install, with the real index on GitHub.
- [ ] Add-ons against the real GitHub: a public repository's release and branch, through GitHub's redirects to its asset hosts (unit tests and an in-memory GitHub only).
- [ ] Add-ons on Windows (the WinHTTP final-address check compiles; not run) and a plugin add-on on macOS.
- [ ] A Hailstone release whose `core` package ships a plugin folder an add-on already installed would write over it: the core install does not yet skip add-ons' folders (only the optional `plugin-<name>` packages are skipped).

## 3. UI

- [x] Labels: combat timer, breath, respawn gauge, aggro meter, title, ATK, regeneration, and more EQTypes, in the modern and gamepad packs (ui-gaps, merged up to 90aa43f; checked live). ATK leaves out the leadership offense bonus and profile +0x3468; the label and gauge types still not tracked are listed in unknowns.md.
- [x] Buff icon fading below 19 s; gauge colours for special values (unit tests, not seen live).
- [x] Hot buttons: the AA and gem right clicks, vertical bars (modern pack, checked live), the key-map text corner; the ranged attack was already in.
- [x] Hot buttons: vertical bars in the gamepad pack (checked live). Open: the AA and gem right clicks seen live.
- [x] Hot buttons: an inventory or bag slot held (or dragged, modern pack; LB in the gamepad pack) gives an item link or inventory slot button; item links find their item, show its icon and use it (unit tests and mock server). Open: seen live, the Krono slot's hold (type 13), the bank's slots.
- [x] The login, server select and character select screens drawn by the modern and gamepad packs (`app/lua_eq_pregame.cpp`, `ui/<pack>/login.lua`, `serverselect.lua`, `charselect.lua`); a redone login (Continue as, the server's settings and More options folded away). Unit tests, and the server and character select seen live against the local test server in both packs. Open: played through live: a world played, a character entered, created and deleted, a box login, a server saved, deleted and its interface switched from these screens.
- [x] Return Home on the character select (CLW_Home_Button: enabled by the list's go-home flag, asked first with eqstr 6137, then OP_EnterWorld with the home flag), in the ImGui screen and both packs (`eq.CharacterSelectDo("home")`, `canGoHome`, `homeQuestion`). Unit and mock-server tests. Open: tried live against a server with `World:EnableReturnHomeButton` on; the tutorial button (CLW_Tutorial_Button, the same packet's tutorial flag).
- [x] The character creation screen drawn by the modern and gamepad packs (`eq.CharacterCreate`, `ui/<pack>/charcreate.lua`): name with a random one, gender, race, class, deity, start city, the stats, the game's descriptions, Appearance. Unit tests and the packs drawn headless. Open: played through live in both packs (a character created, the random name, Appearance and back).
- [x] The spell book: memorize only into an empty gem; Mem. This Page does nothing in RoF2 either.
- [x] The compass's heading error below Sense Heading 200, reset by a successful Sense Heading (checked live); sense heading's raw heading and the South band match the original.
- [x] The item display's placeable, collectible, No Destroy, No Storage and trophy lines (stock EQEmu sends none of this data; unit tests).
- [x] Context tips for experience, invites, consent, red-con target, world containers and more; the tip table's ids fixed (two missing entries shifted every tip from 0x38); OP_ConsentResponse / OP_DenyResponse handled (unit tests). Open: the tips listed in unknowns.md.
- [x] The chat server's per-window channel state is saved (it already was; a test added).
- [x] Tracking window: the controls run the /track commands, auto update saved (modern checked live).
- [x] Tracking in the gamepad pack's More window for characters with the skill (checked live).
- [x] `/copylayout`, `/offlinemode`, `/bugreport` (f3c5c5d; `/offlinemode`'s yes path not seen live).
- [x] Ability Book (Hailstone's own, `game/ability_book.h`, `ui/modern/abilitybook.lua`): activated AAs, combat abilities and activated skills in one searchable, sortable list with descriptions and timer groups; double-click / drag to a bar / right-click menu; the EQ menu row after Combat Abilities and `/abilitybook`; client-side autoskill (ordered list; while fighting, any time or out of combat for beneficial abilities; only when its buff has worn off; endurance, mana, own health and target health above / below conditions; failure backoff; the switch kept per character across logins; `hailstone.cfg` `[abilities] autoskill = off`). Checked live on a cleric (autoskill fired Bash on a #spawn dummy). A class filter shows that class's abilities and the shared ones; the class dropdown sits after the filter chips. Open: the gamepad pack's page; the class filter and sort seen with a multiclass character (NMS sends every AA as every class's, docs/unknowns.md); a key binding for it.
- [x] Modern UI item marks (junk, locked slots, bag filters; `ui/modern/lib/itemtools.lua`) are kept per server and character: `items.` keys are the character's settings (`LuaUi::character_setting`), in `servers/<profile>/.../ui-modern.txt`; marks from the shared file are read until the character changes them, and a layout copy leaves them.
- [ ] The in-game browser (docs/browser.md "Milestones"; the messages, pipes, tab host, the Linux helper, the window in both packs and the scripts' and plugins' tabs are done): the helper on macOS (app bundles) and checked on Windows and Linux arm64 (CI), Chromium's sandbox on Windows and macOS.
- [x] Scripts' and plugins' pages held tighter (docs/browser.md "Rules", "Profiles", "Sizes"): a cookie store per add-on, frames and requests on the allowed sites, never this computer or a private network, console lines and page messages bounded in bytes (unit tests; the CEF helper's frame and request refusals in its test).
- [ ] Scripts' and plugins' pages: a public host name whose DNS answer is a private address is not refused (it would need the helper to check the address it connects to); whether CEF hands WebSocket handshakes to `OnBeforeResourceLoad` was not checked.

## Navigation (docs/navigation.md)
- [x] Navigation meshes built from the zone's collision on worker threads, cached per server and zone; areas from the regions (water, lava, zone lines, PvP).
- [x] `/nav` (MQ2Nav's commands), `/moveto`, the `Navigation` TLO; the walk steers by the heading and the movement keys, gets unstuck, opens doors in its way, and asks the server's `/find` path while the mesh is building.
- [x] Tried live on NMS Dev (ecommons): long walks, /moveto, a door, the TLO, the overlay, pause, cancelling with a key, an unreachable point.
- [ ] Live: a moving spawn, a closed door clicked on the way, the server-path fallback, a stock EQEmu server.
- [x] /follow helped by the mesh (round corners and walls, unstuck, a 400 leash), switchable with `nav.follow`.
- [ ] /follow's help live (following a player round corners and through a doorway).
- [ ] Lifts, jumps and safe drops as off-mesh links (drops checked against the fall-damage rules).
- [ ] Walks across zones (`/travel <zone>`) over the zone lines and zone points.
- [x] `/stick` (distance, behind, id; the `Stick` TLO), the `LineOfSight` TLO and `Spawn.LineOfSight` (the abilities' own check). Unit tests on a built mesh; not tried live.
- [ ] Tactical queries for bots (a kite point; `/stick`'s pin, `!front`, `moveback`, `uw`, and following a changed target without `hold`).
- [ ] Auto Combat (a script with its own window; `scripts/autocombat/`): Manual (full and partial), Hunter (roam and camp) and Assist modes, Spells and Abilities pages of rows with conditions. Its client side is done: `Me.AbilityReady` / `CombatAbilityReady` / `AltAbilityReady` and timers, `Target.Buff` and the rest from OP_TargetBuffs, `/stick`, line of sight, `hailstone.server_rule`, `hailstone.hold_autoskill` and `autoskill_list`.
- [ ] Auto AA (an add-on of its own, projecthailstone/hailstone-autoaaspend): its client side is done (`hailstone.alt_abilities`, `hailstone.train_aa`, the Modern pack's `lib/aa.lua` with a part on each row, in the details and under the header). Open: the Gamepad pack's AA window as a library a script can extend the same way; a purchase checked live against EQEmu.
- [ ] Waypoints and an in-client mesh editor (areas and links in a shareable per-zone file).
- [x] Swimming: water surfaces in the mesh, the look pitch steered while swimming (breath, diving, climbing out).
- [x] Zone lines marked exactly into the mesh; a walk stops before stepping into one (seen live at Oasis / North Ro).
- [ ] Swimming live end to end (a long swim, diving, climbing out); levitation paths.

## 4. Check live (implemented, not confirmed)

- [x] Shift-click / Ctrl-click quantity flags (checked live).
- [x] Equip slot availability (the profile's slot mask) and auto-inventory's use check, ported.
- [x] Guilds tried live with a GM-made guild (2026-10-29): invites across zones, chat, MOTD, ranks and permissions, notes, alts, leader change, the guild bank; the join, leave and rank lines and the nameplate's show-guild flag fixed. Open: a second member's view of the guild bank; /guildwar and /guildpeace are only logged by EQEmu.
- [x] The guild window's Tribute page (each member's tribute and trophy states, donations, last donation; the opt-out buttons) and Banner page (as the original shows it without banner messages, which EQEmu never sends). Checked live on the NMS dev server (2026-10-31) with a GM-made guild of two: the leader's OptOut for the member (`guild_tribute_opt_out`) came back as OP_GuildOptInOut to both and updated the member's row (tribute on to off, `guild_members.tribute_enable` 0), and the Tribute Benefit window's button (`guild_tribute_opt`) opted the member in, out and in again (playprobe `--tribute-opt-out`, `--tribute-cycle`). The Banner page was not tried live. Open: the banner requests and editing (docs/unknowns.md).
- [x] A raid of 18 and the raid MOTD button: checked live on the NMS dev server (2026-10-31): 17 headless characters invited by `/raidinvite`, every client's list reached 18, and the leader's MOTD reached all 17 members ("Raid MOTD: ...") (playprobe `--raid-size`, `--raid-motd`, `--raid-accept`). Open: zoning in, the leader printed "<name> joined the raid." for each of the 17 members (docs/unknowns.md).
- [x] Gamepad pack: after B closes a Tip window opened over the merchant, the pad dropped to game mode and the merchant window no longer took it: the merchant and the Tip asked for the pad in the same frame and only the last request was kept. Every request is kept now, and the window under a closed one takes the focus back; checked live on the NMS dev server (2026-10-31): after the Tip, RB reached the merchant's Parcels page.
- [x] Melee sounds (the cues fire with the right waves; not listened to).
- [x] Augment appearance-slot gating: checked live on the NMS dev server (2026-10-31): with game feature 0x15 on (the client's setting for EQEmu accounts), a Tricorne Hat Ornament (aug type 21) into a Cloth Cap's type-21 socket was not refused (no 5485), went out as OP_AugmentItem and EQEmu stored it (`inventory.augment_six`); the client showed it in socket 6. The refusal with the feature off is unit-tested only (docs/unknowns.md).
- [ ] OP_Weblink from a quest's `SendWebLink`, `/url` from a script, and the browser window (`/web`), in a live session (unit and window tests only).

- [ ] Idle appearances freed during a zone (render/look_eviction.h, five minutes unworn): watch a busy hub for an hour (the log's "freed N idle appearances", the GPU memory), and a spawn coming back after its look went (dressed again on the worker). The rule is unit-tested; the renderer side was only built.
- [ ] Free the texture slots of freed appearances too: a freed look keeps its textures (slots are shared between looks), so unique armour and illusion textures still build up over a long stay in a hub. Reference-count each slot by the GpuModels that read it and free one no live model reads, after kFramesInFlight.
- [ ] A GPU without a sampleable D32_SFLOAT depth attachment (the D32_SFLOAT_S8_UINT / D24_UNORM_S8_UINT fallback, now with `separateDepthStencilLayouts`) under the validation layers; and ObjectRenderer's instance flush on a driver with non-coherent host memory. Neither was run on such hardware.

- [ ] The 60 Hz game clock in a live session: jumps, falls and fall damage, /follow and the camera at 30, 60, 144 and uncapped; turning and strafing feel; a zone's load (a dropped stretch).
- [ ] The cameras 1..5 (docs/mechanics.md "Cameras") in a live session: each one's place, the sweep against walls and doors, mouse look and the camera keys in each, riding and sitting (the tether's bone 0). Unit tests only so far.
- [ ] The cameras the original switches to itself: the overhead view while knocked out (KnockedOut @0x5a3d1b, back with RestoreSavedCameraType on GetConscious), first person when drunk (70 and over), the missile camera (8) for the player's own arrow with its option (0xe635a9), and RestoreSavedCameraType after zoning.

## Not planned

Voice macros and audio triggers, the commercial-service windows (the marketplace's funding pages among them), the zone guide, zone path and tribute trophies (no data on EQEmu), Miles' reverb DSP and DLS synthesis, the client's own graphics settings.
