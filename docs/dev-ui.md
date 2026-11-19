# Developer UI

Lua interfaces use the versioned, modular `hs.UI` contract for game data and actions.
Modern and Gamepad both use it. See [UI API](ui-api.md) for feature discovery,
pack inheritance and the author workflow, and [UI API Reference](ui-api-reference.md)
for every canonical method. Flat host bindings are also available under `hs`.

The drawing API exposes all 23 public window flags from the pinned Dear ImGui version through
`ui.WindowFlags`, including `HorizontalScrollbar`, `AlwaysVerticalScrollbar` and
`AlwaysHorizontalScrollbar`. Their values match Dear ImGui in ordinary windows and child windows.
Internal window-kind flags and obsolete flags are not exported; use the corresponding window, popup
or child functions to establish those scopes.

The client uses [Dear ImGui](https://github.com/ocornut/imgui) (v1.91.9, fetched by `cmake/Dependencies.cmake`) for a minimal developer UI. With it you can log in, pick a server and a character, and see basic in-game information. The Lua windows below replace it for play.

## Pieces

- `src/ui/imgui_layer.{h,cpp}`: owns the ImGui context and the SDL3 and Vulkan backends. The Vulkan backend uses our volk loader and dynamic rendering, and records into the frame's command buffer inside the rendering that `VkContext::begin_frame` started (colour + depth). Fonts and sizes follow the display's content scale. The layer looks for Liberation Sans or another common system font and falls back to ImGui's built-in font.
- `src/ui/dev_screens.{h,cpp}`: the screens. Each one takes a view model and returns the player's action, and none of them does networking:
  - `login_screen`
  - `server_select_screen`
  - `character_select_screen`
  - `loading_screen`
  - `in_game_overlay`, made of the info panel, chat and spawn list
- `src/ui/spell_windows.{h,cpp}`: the spell gem bar, cast bar, spell book, buff and song windows and the target's buffs, with spell icons from the game's `spells0N.tga` sheets (A_SpellIcons: 40 × 40 cells, 6 per row, 36 per sheet). `src/app/spell_ui.{h,cpp}` builds their view models from `net::GameConnection` each frame and passes clicks back.
- `src/game/eq_names.{h,cpp}`: race and class names for display. Multiclass characters are shown as `SHD/CLR/WIZ`.
- `src/core/config.{h,cpp}` and `src/platform/paths.{h,cpp}`: the `key=value` settings file, `hailstone.ini` in the Hailstone folder beside the binary (or in `~/.config/hailstone`, `%APPDATA%\hailstone` or `~/Library/Application Support/hailstone` when that folder is not writable; `docs/layout.md`). It names the last server used (`login.profile`).
- `src/core/server_profile.{h,cpp}`: the saved servers the login screen picks from. Each is a folder `servers/<id>/` with a `server.ini` (name, address, port, username).
  - New, Save and Delete edit them; Login saves the picked one.
  - The username is kept when "Remember username" is ticked. The password never is.
  - The Assets row shows the server's own files folder (`servers/<id>/assets`) with an Open folder button. Files there replace the asset folder's files of the same name for that server (`GameDir::set_overlay`, applied at Login).
  - The old `login.host`, `login.port` and `login.username` keys become the first saved server.
  - Test logins (`--user` with `--host`/`--port`) use a transient server that is never saved. `--password` is on the command line, which every user of the computer can read in the process list, so use it only for a test account. `--server <id or name>` picks a saved one.
- `src/app/ui_demo.{h,cpp}`: the demo below.

## Demo

```sh
build/hailstone/hailstone --ui-demo login           # start on the login screen
build/hailstone/hailstone --ui-demo game --zone qeynos2 # in-game overlay over the zone viewer
build/hailstone/hailstone --ui-demo chars --frames 20 --screenshot chars.png   # to screenshots/chars.png
build/hailstone/hailstone --ui-demo login --ui-scale 1.5 # override the display scale
```

The screens are `login`, `servers`, `chars`, `nochars`, `loading`, `game` and `items` (a fake inventory and corpse). In the demo:

- F1 to F7 switch between the screens.
- The buttons walk through a fake flow: any username and password log in, then server, character, loading and in game.
- Chat lines you type are echoed back.

While the UI has keyboard or mouse focus, the camera ignores that input. Holding the right mouse button still turns the camera.

The UI has the keyboard only while an edit box is being typed in, a modal dialog is open, or a window asks for it (`ui.CaptureKeyboard`). A click or drag on a button, slider, title bar or window background does not take it (`ui::keyboard_for_ui`), so movement keys held through the click stay held, as in the original.

## Using it from an app loop

```cpp
ui::ImGuiLayer ui(window, vk);           // after VkContext
while (window.poll_events()) {
    ui.new_frame();
    if (auto a = ui::login_screen(login_view); a == ui::LoginAction::Submit) { /* start login */ }
    auto f = vk.begin_frame(clear);
    if (!f) { ui.end_frame_without_render(); continue; }
    /* draw the world */
    ui.render(*f);                       // last, before end_frame
    vk.end_frame(*f);
}
```

### Modern chat tabs

Right-click any tab or the chat output to open the same chat menu. From there you can create,
rename, and close tabs and windows, detach or move a tab, reorder tabs, and combine the windows
into the one you clicked. **Switch Tab** lists the tabs in that window, and **Chat Windows**
shows the window IDs and tabs across all windows. The menu also has the tab's scrollbar and
highlight settings, the chat routing controls, and the **Window** layout menu. The overflow
button lists all tabs when the strip fills.

Moving a tab keeps its filters, output and scroll position. Background tabs flash in their chosen
colour every half second for five seconds, then stay highlighted until selected. Highlighting
defaults to on in red; with it off, a tab with unseen lines shows a dot after its label instead. Each tab can also hide its scrollbar. Chat groups, names, order, selection
and tab settings are saved per character. The native model is shared by the Modern controls,
[`hs.UI.chat`](ui-api.md#chat-tabs-and-windows) and [`/chattabs`](commands.md).

The menu's first rows are the original's (CChatWindowManager::InitContextMenu @0x6513c0, in its order: Window,
Scrollbar, New Chat Window, Rename Window, Always Chat Here, the tell menus, Filters, Channel, Language, Hit Modes);
the tab rows, the highlight settings, Font Size, Use Tell Windows and Timestamps are Hailstone's and come after them.
Rename Window renames the frame, Rename Tab the chat window it shows (RoF2's window name); both dialogs take 15
characters, as the original's does.

Tabs are kept in the character's `UI.ini` under `[ChatManager]`, beside the original's keys: `ChatWindow<n>_Group`
(the frame, 1..32), `_TabOrder`, `_Highlight` and `_HighlightColor` per chat window, and `ChatGroup<g>_Selected`
and `_Name` per frame. A file written before them (`ChatWindow<n>_Dock` and `_Up`) loads as it was: a docked window
becomes a tab of its host's frame, with the tab that was up selected, and the new keys are written at the next
save. Tell tabs are not saved, as in the original.

### Extended chat channels

The chat windows' channels 57..107 and user colours 355..408 (docs/mechanics.md "Extended chat routing") are
Hailstone's, used only when a server sends those colours. The gamepad pack has no filter menu.

- `hs.ChatFilterGroups()` gives the Filters menu's submenus as `{id, parent, name}` (parent 0 is the Filters menu):
  Chat Channel, Melee and Spells, then Extended (server) and its submenus.
- `hs.ChatFilters(slot)` keeps `id`, `group`, `name` and `on`, and adds `menu` and `parent` (the item's menu and the
  menu that holds it).
- `hs.ChatSetFilter` takes a channel or a menu's name.
- Each filter menu ends in the original's **ALL** and Hailstone's **Hide All**, which apply only to that menu's own
  channels.

The channels are kept as `ChannelMap0..107` in the character's UI ini and the colours as `User_1..153` in its
`chat.ini`. A file written before them (ChannelMap0..56, User_1..99) loads as it was, with the new ones on the main
window and at their defaults.

## Quest Journal and Quest Tracker

#### The journal

The modern journal (`tasks.lua`) has Active and History pages. Active lists every current task, shared tasks included, with tracked quests first.

The selected quest has Objectives, Description and Rewards sections:

- The objective table keeps the native group/slot order, including every hidden `? ? ?` row and the blank separators the task model supplies. It does not invent numbered steps or parent/child dependencies.
- Completed quests keep the native history request.
- Description and reward links use the existing `TASK_Description` STML handler. Abandon keeps its native confirmation.
- Preview Reward opens the selected task's server-owned reward preview when the native task model enables it. The Rewards section keeps the supplied reward text; unavailable previews stay disabled.

Both packs read the active-task preview's enabled state through the registered `hs.ChildState` API.

#### Task offers

TaskSelectWnd also has Preview Reward in both packs, using `TSEL_PreviewRewardButton` and the selected offer's received preview flag.

- Opening or drawing offers does not request rewards. A preview leaves the offers open.
- Its Description includes the native starting-zone hint from element slot 0. This is eqstr 6029, not a reward summary: reward details come from the server's separate preview response.
- Selecting an offer without a preview hides a preview already shown, as the native selection handler does. A replaced offer list starts again at its first row.
- Accept and Preview Reward report disabled through `hs.ChildState` until the offers have been listed (`hs.ListInfo`), since only a listed row can be acted on.

#### Shared tasks

The header's Shared members control shows the roster count and opens a dismissible shared-task management popup. Its Member and Role columns read the native `STASK_MemberList`. Selecting a member drives Remove member and Make leader.

Add target, Remove member, Make leader, Invite raid and Invite expedition use the existing native controls and their permission checks. Shared tasks stay in Active while the roster is open.

#### The tracker

Track puts a quest in `questtracker.lua`, a UI-owned `hud.Window` independent of `TaskWnd`. It is a Hailstone presentation feature, with no extra server requests or rules.

- Each tracked quest shows up to three revealed, unfinished objectives in their native order, with counts and locations. Hidden rows stay in the full journal.
- Its title and All objectives open that exact quest in the journal.
- It has the ordinary HUD move, edge/corner resize, lock, fade and roll-up behaviour.
- Hiding it leaves the tracking flags alone; the journal's Show tracker control brings it back.
- With no tracked quests it starts hidden, unless the player explicitly shows it.

#### Shared state and settings

Both windows share `lib/task_journal.lua` and `hs.Tasks()`, which reads all active task state independently of journal visibility and selection.

Building it is not cheap (every quest's text and objectives). The shared module reads it again only when `hs.TasksRevision()` changes (the task list's revision and the selected task, cheap), after a pick, and twice a second for the countdowns. A hidden tracker reads nothing.

A quest that leaves the list (completed, abandoned) forgets its tracking, collapsed state and section. A key set back to its default is dropped from the character's file. `hs.OpenTask(id)` opens and selects an existing active task.

These are character settings:

- tracked IDs (`tasks.tracked.*`);
- tracker visibility and collapsed quests (`tasks.tracker.*`);
- category (`tasks.category`);
- selected section (`tasks.section.*`).

The frame's geometry and lock stay ordinary `hud.*` character layout settings. Copy layout copies frame placement, not the tracked quest IDs or sections.

#### Journal tests

Packet-driven bridge tests and headless Dear ImGui/Lua tests cover the data, hidden rows, sections and independent tracker. Live acceptance of quest updates across zoning, shared roster updates and membership actions, and an in-game mouse resize remain separate checks.

#### Syncing Lua-only edits

The client target copies the UI packs after linking. For a Lua-only edit that does not relink the executable, invoke `cmake/SyncUiPacks.cmake` explicitly with absolute `SRC` (the source UI folder) and `DST` (the built client's UI folder) paths. This copies changed scripts and leaves pack settings alone.

## The Lua windows

### UI packs

The windows are Lua scripts drawn with ImGui, grouped into user interfaces. Every folder of `ui/` with a `ui.ini` is one (`src/ui/ui_packs.h`).

- The build copies them next to the executable, and they reload when their scripts change. It copies only the scripts whose contents changed and deletes those gone from the source (`cmake/SyncUiPacks.cmake`), so a rebuild reloads only what it changed.
- Each saved server keeps its own (`servers/<id>/server.ini` `[Server] ui`). It is picked on the login screen's Interface list, which every pack's login screen shows on its main card, or in Options > Display > Interface > User Interface. The login screen switches to it at once, and a saved server keeps it at once.
- `hailstone.ini` `ui.name` is the one for a new server or a test login (`modern` when it is missing).
- Each keeps its scripts' settings in `ui/settings/<name>.txt` of the Hailstone folder, not inside the pack, which an update replaces.

A pack's `ui.ini` holds:

- `name` and `description`, shown in the Options window;
- `input`: what it is built for (`mouse`, `gamepad`, `touch`);
- `game_art`: with `icons`, only the item, spell and gem icon sheets of the game's `uifiles/default` load (`UiTextures::set_all_art`); with `all`, every file;
- `base`: a pack whose windows and helpers fill in what this one lacks (docs/ui-api.md).

Scripts `dofile` paths relative to their UI's folder (`"lib/theme.lua"`). A new UI is a folder with a `ui.ini` and its window scripts over the same `ui.*` and `hs.*` API. It may take `lib/` from modern or gamepad by copying it.

The Classic UI (`ui/classic`: the game's windows drawn from their EQUI_*.xml layouts through `lib/sidl.lua`) is gone (2026-10-28). Modern and Gamepad draw every window themselves. A server or `hailstone.ini` that still names `classic` gets Modern (`ui::find_ui_pack`).

### Modern look and palettes

`ui/modern` is a modern HUD over the same `hs.*` answers and clicks, with no layouts and no game UI art (`game_art=icons`). It has translucent rounded panels, eased bars with a trail for lost health, and icon tiles.

- Colours and drawing pieces are in `lib/theme.lua`.
- `lib/style.lua` restyles ImGui's own pieces (title bars, buttons, tabs, scroll bars, popups, tooltips). It uses the `ui.Col` / `ui.StyleVar` names and skips any the client does not have.

#### Colour palettes

Colours come from the palette the player picks: Options > Display > Interface > Colour Palette, the setting `palette` in `ui/settings/modern.txt`, applied at once.

- `lib/palettes.lua` lists Midnight (the default), Charcoal, Daylight (light), High Contrast and the accent palettes Ember, Verdant, Amethyst and Rose. Each is a set of named tokens (window, frame, frameBorder, text, muted, heading, accent, onAccent, ink for faint overlays, chat and bar text, the coins, ...).
- `lib/theme.lua` fills `T.color` from it in place, so every window and every `lib/style.lua` push follows a change.
- Each palette keeps the WCAG contrast checks listed in the file (text 4.5:1, control outlines 3:1; `tests/lua_scripts_test.cpp`).
- The chat draws its lines on a page of the palette's `chatBg`, checked alone over a white or black world. The chat frame's panel fades while idle, so it cannot carry the contrast.
- Dialogs and the Options window outline their fields, check boxes, combo boxes and buttons in `frameBorder`. The Options window's section headings are in `heading`.
- Testing: `test.palette.cycle=<seconds>` shows each palette in turn (logged), for screenshots.

### Modern frames and dialogs

#### Default layout

Frames (`lib/hud.lua` `hud.Window`) are borderless. A frame shows when the game shows the window it replaces (`hs.ReplaceWindow`).

Until moved, a frame sits on the modern UI's own default layout (`lib/layout.lua`). The layout depends on the screen's size, the UI and HUD scale and the frames shown now. It is worked out again when one of those changes: a frame shows, changes size, or has not been drawn for more than two frames. The places:

- from the bottom middle up: the hot bars, spell gems, player and target side by side, and the air bar;
- the chat at the bottom left, as wide as the room beside them allows (the middle stack moves right when the screen is too narrow for both);
- the compass at the top middle;
- the minimap in the top right corner, with the buffs and songs to its left (the trays wrap to the room up to the compass, under it where fewer than eight icons fit) and the auras and extended targets under them;
- the group at the left, and the pet beside the player;
- the menu button in the bottom right corner.

Each frame takes the first of its places that is clear of the ones before it, else the clear place beside the frame nearest it. The casting bar, a target, the first buff and a full group keep their room while not shown, so one appearing moves nothing. A frame the layout does not know (a plugin's) keeps its script's `opts.place`.

`tests/lua_scripts_test.cpp` checks the layout at 17 screen sizes and 7 scales: nothing off screen, and nothing overlapping from 1000 x 600 design pixels for a new character and from 1280 x 720 with every frame up.

#### Moving and sizing frames

- A frame moves by dragging any spot that is not a button, slot or bar, or anywhere with Alt held. A drag that starts on a piece covering its frame (a group card, the menu button, the minimap) moves the frame (`hud.DragItem`).
- It is scaled from the right-click menu's Scale.
- The chat's right and bottom edges and its bottom-right corner change its size.

The right-click menu (Alt+right-click where the game has a menu of its own) has:

- Lock this frame / Lock all frames;
- Layout mode (the frames that hide while empty, outlined);
- each frame's scale and opacity, and reset position and size;
- Roll up or Expand (a frame that folds into its title bar, the chat), and hide;
- All frames (HUD scale, the minimap, reset every position);
- Snap to grid (below).

Places, sizes and scales (`hud.pos.<window>`, `hud.size.<window>`, `hud.scale.<window>`, every `hud.*` key) are the character's. They are kept in `ui-modern.txt` in the character's folder, not its UI ini. A key it lacks comes from the shared `ui/settings/modern.txt`, so a new character starts from that layout. The layout copy window copies another character's.

#### Modern dialogs

Dialogs (`hud.Dialog`) have a title bar and close box and are always movable. Their background is opaque; the Opacity setting fades the whole window.

- **Scale**: a dialog's scale (`hud.scale.<window>`, text and all) is set from its right-click menu's Scale slider or Ctrl+mouse wheel over it.
- **Resize**: a dialog with `opts.resizable` changes size from its right and bottom edges and its bottom-right corner. Those handles are the script's own (`ui.EdgeButton`): ImGui's resize is off, since its grip would take the corner. The size is kept in `hud.size.<window>` in design pixels and the contents reflow, so the dialog's Scale (the menu's slider, Ctrl+mouse wheel, the UI scale) zooms the whole window. A dialog sized to its contents has no corner grip.
- **Roll up**: a dialog with a close box has a roll-up button beside it, ImGui's collapse button put at the title bar's right by `ui.Begin`'s `collapseRight`. A double click on the title bar does the same, and so does Roll up in its right-click menu. Rolled up, only the title bar is drawn. The state is kept in `hud.rolled.<window>` until it is unrolled, even across closing and opening it again.
- **No roll-up**: questions to answer (the confirmation, quantity, text entry and bazaar purchase prompts, the server plugins' question) say `rollup = false` and have none, nor does a dialog with `closable = false`.

The HUD frames (hot bars, spell gems, player, target, group, buffs, chat, ...) have no title bar and do not roll up, unless their script gives them one. A frame with `opts.rollup` folds into the title bar it draws itself (`opts.titleHeight`, its height in design pixels), the state kept in `hud.rolled.<window>` like a dialog's.

#### Close boxes

Every window that can be put away has a close box. It goes through the window's own close (`hs.ShowWindow(name, false)`): the merchant, bank, loot, trade and tradeskill windows end their sessions with the server. A window only the server closes (the respawn choices) has none (`hud.Dialog`'s `closable = false`).

The modern frames are HUD pieces with no close box, except those that can be put away (`hud.Window` `opts.onClose`). The gamepad pack's windows have a round X next to B. A modern dialog's right-click menu has its scale, opacity, Fades, Snap to grid, reset position and size, Roll up and Close.

#### Grid snapping

Grid snapping (`lib/snap.lua`) is off by default. Snap to grid (`hud.snap`) is in every frame's and dialog's right-click menu and in Options > Display > Windows.

- The grid (`hud.snap.size`, 4 to 64, default 10) is in design pixels, times the UI scale on screen. A faint grid is drawn over the screen while a window is dragged. Shift held places it freely.
- **Moving**: a dragged frame or dialog lands with its left or right edge, and its top or bottom edge, on the grid line nearest where the mouse put it, or flush with the screen's edge when that is nearer (`snap.Axis`, tested in `tests/lua_scripts_test.cpp`).
- **Resizing**: the dragged right or bottom edge of a resizable frame or dialog (both, from the corner) lands on the nearest grid line, or flush with the screen's right or bottom edge when that is nearer, never below the window's smallest size (`snap.Extent`). The grid shows for the drag. The window's left and top edges stay where they are, on the grid or not.
- A frame that lays itself out from the drag (`opts.onResize`: the hot bars, whose shape goes in whole buttons) is resized as the mouse puts it and shows no grid, since its own rounding would leave it off the lines.
- A frame's drag keeps the unsnapped place for the length of the drag, since each frame's mouse move alone would never reach the next line.
- A dialog is moved by ImGui's title bar drag. It is snapped from where that drag put it before its `Begin` (`ui.FindWindowRect(name)`: a window's place and size by name, nothing for one never drawn) and held there with `ImGuiCond_Always`.
- A window already in place does not move when the grid is turned on, only when next dragged.

#### Transparency and fading

A window's transparency (frames and dialogs alike) is its own Opacity (its right-click menu) times the Options window's Window Transparency (the UI ini's `[Main] GlobalAlpha`).

A window with Fades ticked in its right-click menu (`hud.fades.<window>`, off by default; the original's per-window Fades) fades as CXWnd::OnProcessFrame fades a window:

- to the Fade-to Transparency (`GlobalFadeToAlpha`) once the mouse has been away for the Fade Delay (`GlobalFadeDelay`);
- back while the mouse is over it;
- each over the Fade Duration (`GlobalFadeDuration`).

#### Close with Esc

Close with Esc (`lib/escape.lua`) works for frames that can be put away, dialogs and the Options window alike. It is ticked in a window's right-click menu (`hud.esc.<window>`).

- It is on until unticked for the windows in its `DEFAULT_ON`: the inventory, AA window, ability and spell books, bags, map, item and spell displays, and the reference and social windows (inspect, skills, tasks, guild, friends, bank, scripts, help, books and notes, tribute, leadership, titles, tracking, mail, group, bazaar and barter search).
- It is off for the rest, such as prompts and the merchant, loot, trade, give and tradeskill windows, whose closing ends or cancels something.

The key bound to CLOSE_TOP_WINDOW (Escape unless rebound) closes a ticked window. One press closes one window: of the ticked windows shown, the one opened or clicked last.

The client's own key windows go first (`ActionsUi::close_top_window`). A press nothing of the client's took is the UI's for that frame (`hs.TakeCloseTopWindow()`, true once; dropped after the frame). The key's other default command, CLEAR_TARGET, still runs.

#### Controls

Controls (`lib/widgets.lua`) are buttons, check boxes, button rows, edit and combo boxes, sliders, gauges, lists, STML text, item slots and slot grids, tabs and coins. A list is a virtual table over `hs.ListInfo` / `hs.ListRow`, sortable by a header click and filterable.

Each control is bound to a child of the game window by its ScreenID (the layout's `id`, not its `name`). It reads what the child shows and hands clicks to it (`hs.ClickChild`, `hs.ListClick`, `hs.SetWindowText`, `hs.ComboSelect`, `hs.SetSlider`, `hs.SetTabPage`, `hs.PickUp`), so the window's own handler runs.

The ImGui bindings they use beyond the first set: `InputTextWithHint`, `PushTextWrapPos` / `PopTextWrapPos`, `SetCursorScreenPos`, `TableSetupScrollFreeze`, `TableGetSortSpecs` and the `Sortable`, `BordersInner*`, `Hideable`, `Reorderable` table flags.

### Redone windows

#### HUD windows

- **PlayerWindow**: level badge, health / mana / endurance, experience and AA experience, and the spell being cast (the game's CastingWindow, drawn here instead of a frame of its own). While auto attack is on: a pulsing red ring with a halo (`T.Panel`'s `glowWidth`), a red wash and a red level badge. The pet's health is the Pet window's.

- **TargetWindow**: con colour, health, the target's target, the aggro meter, and its buffs, each with Spell Info on a right click. The aggro meter is AggroMeterWnd's gauge 149 and labels 302..307. `aggro.lua` keeps the game's window from showing, and the EQ menu has no Aggro Meter row.

- **GroupWindow**: a solid panel with a title strip to drag, member cards, and the invite / disband / accept / decline buttons. Solo it shows only with an invitation or a player to invite.

- **BuffWindow** and **ShortDurationBuffWindow**: packed icon trays and time left; the last ten seconds pulse. A right click offers Spell Info, Add to Blocked Buffs (the Blocked Buffs window's own Add) or Remove.

- **CastSpellWnd**: a row or column of gem tiles, the refresh shading, the spell book.

- **HotButtonWnd**..10, **CastingWindow**, **EQMainWnd**; **CompassWindow** (a heading band).

- **Chat windows**: tabs, the channel chip, the chat menu.
  - A button at the tab strip's right end folds the window into the strip (the output and the input go, the tabs stay), as the combat ability window folds into its title bar.
  - Always Chat Here (`hs.ChatSetAlwaysHere`, `hs.ChatInfo`'s fifth value) makes a window the one Enter and '/' start a line in. The main window is the one by default.
  - Timestamps (`chat.timestamps`, every window) put each line's local time first, `[hh:mm:ss]`, from the time it came, which the chat history keeps.
  - Each window and tab keeps its own last 1000 lines (Hailstone's limit), so a busy one does not push out a quiet one's.
  - A tell window flashes once, a fading ring, when a tell from another player comes in (`hs.ChatWindows`' `tellsIn`).
  - A window asks `hs.ChatText(slot, version, names, stamps, true)` for the lines that came since its version, adds them and drops its oldest down to the count given. It gets everything again (`reset`) when its lines changed otherwise: cleared, another tab, the stamps. Its version changes only with its own lines.
  - Each line's wrap and height are kept until the width or font change.

- **InventoryWindow**: the worn grid, the numbers by EQType, carried slots, coins. Testing: `test.inv.page=character|stats|bandolier|currency`.
  - The worn grid and weapons stand for the paper doll. An item or coin held on the cursor and dropped between their slots goes to `IW_CharacterView`: CInventoryWnd::HandleLButtonUp's auto-equip (eqstr 5290 when it stays), or the coin back to the purse.
  - A Stats page: every attribute and resist as total, base, heroic and cap, the item bonuses, and `hs.CharacterStats`'s regeneration, pools without buffs and defense (`app/lua_ui_stats.cpp`).
  - A Bandolier page (Hailstone's: the stock window has none): the sets beside the selected set's weapons and buttons. It is drawn from `lib/bandolier.lua` as the Bandolier window is, and drives the same BandolierWnd, which stays hidden.
  - The Alt. Currency page: "Show all" for the ones not held (kept in step with `AltCurr_DisplayMissingButton`). A card click selects that row of `AltCurr_PointList`. Claim and Reclaim are `AltCurr_CreateItemButton` and `AltCurr_ReclaimButton`.

- **MapViewWnd** (`map.lua`, over `hs.DrawZoneMap`):
  - resizable; wheel zoom around the cursor (around the player while following, which zooming keeps); drag to pan;
  - Fit / Follow / Labels / Auto Z, and markers of your own per zone in `map.markers.<zone>`;
  - a right click's Find a path here (`hs.FindMapLocation`), the cursor's /loc and distance; Edit opens the map toolbar;
  - the side panel's Find page is Hailstone's Find window (`hs.FindNpcs`, `FindCategories`, `FindNpcTarget`, `FindNpcPath`, `FindNpcWalk`, `FindOpen`; `app/lua_ui_find.cpp`, docs/navigation.md "Find"). Its NPCs button marks the listed NPCs on the map.

- **Bag1..36** (`bags.lua`): rows of up to five. Resizable (Hailstone's): a size the player gave it (`hud.size.Bag<n>`) puts as many slots to a row as its width holds, and keeps its height at least what the rows need.

- **ExtendedTargetWnd**, drawn by `lib/xtarget.lua`, which takes a plugin's part on each row (docs/plugins.md):
  - a solid panel with a title strip and the filled / available slots, shown while the game shows the window; "No extended targets" while empty;
  - a row for every slot the server gives, an empty one faint, each with its role unless an auto hater;
  - six rows tall until the player resizes it, the rest scrolling;
  - right-click: the slot's menu, with Hailstone's Auto expand (`hud.expand.ExtendedTargetWnd`, also in the frame's own menu), which grows the frame down to the last filled slot, within the screen.

- **PetInfoWindow**: its buffs' right click offers Spell Info and Add to Blocked Pet Spells.

- **SpellBookWnd** (`spellbook.lua`): the whole book over `hs.SpellBook`.
  - Search, sort, filters, categories, list or grid, and the spell display text (`hs.SpellDetails`).
  - The spell sets: load, edit, delete; New set or From gems. The editor has twelve slots. It takes spells dragged from the book, a double-click or the spell's menu, moves them between slots, and saves without memorizing. A spell dropped on a set's card goes into that set.
  - A gem strip to drag spells onto. Only an empty gem takes a spell, as StartSpellMemorization without replace: `hs.SpellBookMemorize` refuses a full one.
  - The spell bar takes those drags too, and its book button's right click lists the sets.

- A frame of its own, the **minimap** (`minimap.lua`, from the gamepad UI's): `hs.DrawMinimap`, the zone name and the clock, and the Find window's NPC marks as the map shows them. A click opens the map.

#### Item slots

Slots act as classic's (`lib/items.lua`): the same left and right-button phases, and drags to hot buttons.

- The inventory's and the bags' own slots take the left click on its release (CInvSlot::HandleLButtonUp).
- The left button held on one for half a second, or a drag off it, puts a hotbutton for it on the cursor, to let go of over a hot bar (`hs.ClickSlot(slot, bag, "leftheld")`, `hs.BagSlotClick(window, i, "leftheld")`; CInvSlotWnd::HandleLButtonHeld, `game::inventory_hot_button`).
- That hotbutton is an item link (type 12) that follows the item. With Shift or Ctrl (or on an empty slot) it is the slot itself (type 5, top-level slots only).
- A link finds its item by serial, else by item id (`game::hot_button_item_slot`).

#### Group roles

In a group, right-click a member's name in the Group window, or your own name in the Player window, for Main Tank, Main Assist and Puller as checked toggles. This works in both packs (`hs.ClickChild("GroupWindow", "Gauge<n>", "right")` / `hs.ClickChild("PlayerWindow", "PlayerHP", "right")`, true when it took the click: the menu opened, or context menus are off).

- Small role icons beside the names show every assigned role, with the full role names on hover.
- Only the group leader may change them; the others see the rows greyed. A mercenary's Main Assist and Puller rows are greyed.
- Icons and menu checks follow the server's replies.
- The menu has no "Window" row. In the modern pack the frame's layout menu is a right click anywhere else on the frame, an Alt-right-click on the name, or a right click on the name with no role menu to open (solo), since the name takes the click only when the role menu opened.
- In the gamepad pack, Y on a row of the Group window opens it with the pad (docs/gamepad.md).

The group-role rules are in docs/mechanics.md "Group roles". These controls use the shared GroupWindow / PlayerWindow models (`app/lua_ui_hud.cpp`) and the existing `OP_GroupRoles` sender, as `/grouproles` does. `ui.StyleVar.WindowMinSize` is bound for windows smaller than ImGui's 32 px default (the gamepad HUD's name row).

#### Group invite and Auto-Inventory

- **Group invite** (`groupinvite.lua`, Hailstone): a popup in the upper middle of the screen while an invitation to a group waits. It shows who sent it (GroupWindow's `Hailstone_Inviter`), with Accept and Decline (the group window's own FollowButton and DeclineButton). Its X puts it away for that invitation.
- **Auto-Inventory** (Hailstone, not the original's): a button in the inventory window's and the bank's button row, the InventoryWindow's and BankWnd / BigBankWnd's `Hailstone_AutoInventory` child. It is enabled while the cursor holds an item. A click runs `/autoinventory`: a worn slot that takes it, a free general slot, else a bag; "no open slots" when there is none.

The groups below are also redone, each window a resizable dialog (or a frame) over its game window's children, with a script-side helper library per group.

#### Items and trading

Helper: `lib/trade_common.lua`.

- **MerchantWnd**: the goods as a sortable table with search, "Usable only" (the game's MW_UsableButton) and "Affordable only"; prices as coins, dimmed when the purse cannot cover them; the considered item with Buy or Sell and Inspect; the Recover and Parcels pages.
- **TradeWnd**: both offers side by side, stacked when narrow. An accepted side is framed and tagged, and a status line says who has accepted, from `hs.TradeState()`.
- **GiveWnd**.
- **BankWnd** and **BigBankWnd** (`bank.lua`): slots that rewrap, a search that dims what does not match, the shared bank, banked coin.
- **GuildBankWnd**: bank and deposit tables with the permission filter.
- **QuantityWnd**: the number box, 1 / Half / All, opened where the game puts it.
- **ItemDisplayWindow**..#6: one scrolling card. The flags as chips (Collectible and No Destroy too), captioned class / race / deity / slot lines, the stat grid folding into fewer columns as the window narrows, heroic bonuses, the description.
- **LootWnd**: the filled slots as tiles, Loot All.
- **SkillsWindow**: a sortable, searchable list.

Testing: `HAILSTONE_COMMERCE_TEST=merchant|bank|give:<item id>` near the NPC.

#### Dialog windows

Helper: `lib/dialogs_kit.lua`.

- **ConfirmationDialogBox**: Yes as the primary button; Enter does not accept.
- **LargeDialogWindow**: the server's text, scrolled.
- **TextEntryWnd**, **EditLabelWnd**, **TipWindow** (earlier tips).
- **NoteWindow** and **BookWindow**: paper-toned pages, the spread side by side or stacked.
- **AlarmWnd**, **BreathWindow** (a frame: the air bar pulses in its last quarter), **RespawnWnd**.
- **ColorPickerWnd**: preview, hex, red / green / blue, presets.
- **BodyTintWnd**: now and dye per slot, reagents needed and available.
- **BugReportWindow**, **ClaimWnd**.

#### Progress windows

Helper: `lib/progress.lua`. `P.Rows` draws a list child as custom rows, reading `hs.ListRow` only for the rows in view.

**AAWindow** is drawn by `lib/aa.lua`, which takes a script's part on each row, in the details and under the header (docs/plugins.md).

- One list of all four tabs' abilities, AAWindow's `ModernList`. This is a deliberate difference: the stock window shows a tab at a time.
- A search and tab, category and owned filters over all of it.
- Columns for the tab, the era (the expansion), active or passive, the rank and the cost. The era sorts by `ModernList`'s eighth column, the expansion's number. The text is a font size up from other windows'.
- A details pane with rank, next cost, required level, prerequisites and reuse from AAWindow's `Modern*` children (`app/lua_ui_progress.cpp`), and Train / Train all / Hotkey. Stacked when narrow.

The AA list's columns:

- The list is sorted by name to start with. A header's click sorts by its column, a second click the other way, and its drag moves the column.
- Drag a header's right edge to resize its column against the next column (the last column's right edge is the list's), with minimum widths.
- The ability column has no width of its own. It takes what the others leave, so a wider window widens it, and dragging its edge resizes the column beside it.
- Narrower windows shrink the columns to fit as drawn without changing what is kept.
- Order, sort and the resized columns' preferred widths are kept as `aa.columns`, `aa.sort` and `aa.widths`. Widths are in design pixels, never the ability column's; an old saved one is ignored.
- **Reset columns** restores the default widths, order and name sort.
- Headers and rows use the dialog's effective scale and the same current-frame row bounds, including the scroll bar and a script's reserved area. Each cell and header clips its own text.

The other progress windows:

- **LeadershipWindow**, **TitleWnd**.
- **TrainWindow**: search, sort, value bars, prices.
- **TradeskillWnd**: recipe search, favourites, container, components with what you carry of each in green or red and how many you can make, Combine / Combine N / Combine All / Stop (Hailstone's `Combine*`, `ComponentList` and `MakeableLabel` children), Auto-Inventory / Experiment.
- **TrackingWnd**: con toggles, search. The toggles and the sort are a ranger's, disabled for anyone else. The window's controls run `/trackfilter`, `/tracksort`, `/trackplayers`, `/trackpets` and `/trackmercs` as CTrackingWnd::WndNotification does, so they print and save. Auto update is kept as the client option `track_auto_update`, `[Defaults] TrackAutoUpdate` of the character's options.ini.
- **SkillsSelectWindow**, **CombatSkillSelectWnd** and **SpellDisplayWindow**..#6.
- **AchievementsWnd** and **AchievementWnd** use both packs' own `achievements.lua`: received category hierarchies, objectives with unknown counts kept distinct, linked details, and explicit refresh, reward-view and chat-link actions (see `lua_ui_achievements.cpp` below).

Testing: `test.open.<screen>=<seconds>`, `test.select.<window>.<list>=<row>`.

#### Social windows

Helper: `lib/social.lua`.

- **FriendsWindow**: friends and ignored, add, Who / Remove.
- **RaidWindow**: twelve group cards that reflow to the width, the ungrouped players above, roles, a search, the invitation banner.
  - A member dragged onto a card, or "Move here", is selected in the game's list and that group's button pressed.
  - The game enables the group buttons only in a locked raid, so the leader of an unlocked raid gets the raid locked first and the move made once the lock is back.
  - A full group refuses the drop.
- **RaidOptionsWindow**, **LFGuildWnd**.
- **GuildManagementWnd**: roster, notes, message of the day, ranks.
- **InspectWnd**: the worn grid and the gear by name.
- **MailWindow** (mailbox beside the message, or above it when narrow), **MailAddressBookWindow** and **MailCompositionWindow**.
- **GroupSearchWnd**: what you post, as a player or a group, beside the players or groups found as sortable, searchable tables; a group's members and wanted classes under it.
- **GroupSearchFiltersWnd**, **GroupMentorWindow**.
- **FellowshipWnd**: members and campsite pages. Its tab box is the script's own, since the model keeps no page: `test.tab.FellowshipWnd.Subwindows=n`.
- **SocialEditWnd**:
  - a preview of the button, and the colours as swatches;
  - each line's command with a badge for what it is, a wait box for its `/pause`, and move / empty buttons;
  - a cooldown box for the `/timer`, and a timeline of when each line runs;
  - examples, Revert / Cancel, Ctrl+Enter to accept, and a Commands / Lua switch;
  - Tab completes the game's slash commands, with the matches listed under the box;
  - a toolbar with Test, Put on bar (dragging the preview does it too), Copy and Paste share text, and "running" with Stop;
  - a pasted Lua social goes to the Lua editor unsaved, through the global `hailstone_social_draft`;
  - the lines' parts come through `lib/social_lines.lua`. The gamepad pack has a copy of it, kept the same by a test.
- The **Lua Social editor** (`luasocial.lua`): a dialog of its own, `hud.pos.LuaSocialWnd`. Its Commands / Lua switch turns a press of `mq.cmd` and `mq.delay` lines back into command lines.
- **IconSelectionWnd**.
- **CombatAbilityWnd**: the active discipline and eight ability tiles with their reuse shading; right-click for the ability menu; drag to a bar. `test.draw.CombatAbilityWnd=1` draws it for a class the game refuses it to.

Testing: `test.click.<window>=child,list:row,tabs=page,edit~text`, `test.lfguild.page`, `test.mail.tab`.

#### Quest windows

Helpers: `lib/quests_kit.lua`, `lib/quests_board.lua`, `lib/task_journal.lua`.

- **TaskWnd** and the separate **Quest Tracker** (see "Quest Journal and Quest Tracker" above).
- **TaskSelectWnd**: the offers as cards, the description's `[n,text]` steps numbered.
- **DynamicZoneWnd** and its lockouts, **DynamicZoneSwitchListWnd**.
- Adventures: Alt+V opens the current status independently of a recruiter. Request/offer controls, a separate confirmed Leave, seven-column theme stats and a category/risk leaderboard with tied ranks.
- PvP stats and leaderboard.
- **TributeBenefitWnd** and **TributeMasterWnd**.

Testing: `test.tab.TaskWnd.TaskSubwindows=2`, `test.tab.TributeBenefitWnd=guild`.

#### Market windows

Helpers: `lib/markets.lua` (coin chips; `M.Table`, a list child as a sortable, filterable table with script-drawn cells over a copy of its rows) and `lib/markets_points.lua`.

- **BazaarSearchWnd**: filters in a side column; results sortable by item, quantity, price, trader or the picked stat; double-click buys, right-click inspects.
- **BazaarWnd**, **BazaarConfirmationWnd**, **BarterWnd**, **BarterMerchantWnd**, **BarterSearchWnd**.
- **PointMerchantWnd** and **NewPointMerchantWnd**: right-click inspects a good, Shift or Ctrl+right-click previews it.
- **MarketplaceWnd**: the category tree, the shelf as a grid, a list or the landing page, the selected item with its quantity, buy pages and 3D preview (`hs.ItemPreviewImage(w, h, "marketplace")`).
- The mercenary windows: MMTW_MerchantWnd, MMGW_ManageWnd, MIW_InfoWnd.

Testing: `test.open.<screen>=1`, `test.draw.<screen>=1`.

#### Information windows

- **JournalNPCWnd** and **JournalCatWnd** (`journal.lua`): the NPCs beside their lines, the line's text, its category and the category filter, search, keep a journal, Save; the category editor. Both lists select several rows with Shift / Ctrl and sort by a header click (`W.List` `opts.multi` / `opts.headerClick`).
- **HelpWindow** (`help.lua`): Back / Contents / Reset Tips / Customer Service over the page. The pages' href links are followed by the window (`lib/stml.lua`).
- **StoryWnd**: the stories beside the page, "show new stories". The pages' href links are followed as the help window's are, with no Back.
- **AlertHistoryWnd**: the alerts key, Shift+H by default, shows and hides it.
- **ZonePathWnd** (`zoneguide.lua`). Find is the map's page (below).
- **TributeTrophyWnd**: personal or guild view, the numbers as tiles, both benefit lists, Upgrade / Downgrade / Modify.

Testing: `test.open.<screen>=1` (JournalNPCWnd, JournalCatWnd, AlertHistoryWnd, TributeTrophyWnd); `test.draw.<screen>=1` (ZonePathWnd, which the game opens only with a zone guide path; TributeTrophyWnd).

#### HUD extras

Helpers: `lib/hudx_tiles.lua`, `lib/hudx_blocked.lua`.

- **ActionsWindow**: tabs, tiles that reflow; a tile dragged onto a bar gives its hot button.
- **BandolierWnd**, **PotionBeltWnd** and **AuraWindow** (frames).
- **BlockedBuffWnd** and **BlockedPetBuffWnd**.
- **MapToolbarWnd**: the tools grouped.
- TargetOfTargetWindow is kept from showing (the target frame shows the target's target).

Testing: `test.tab.ActionsWindow.ACTW_ActionsSubwindows=n`, `test.show.MapToolbarWnd=1`.

#### Cursor and menus

`cursor.lua` and `menus.lua` redraw the cursor's item and the game's context menus in this UI's look, over the same data and clicks.

The main button's cascade menu keeps the game's rows and commands but is laid out by `menus.lua`:

- It opens from the menu button wherever that was moved (`game_menu.lua` keeps its rectangle in `modern.menuAnchor`), toward the middle of the screen, each submenu beside its row.
- Its line icons are this UI's own (`lib/menuicons.lua`, by row text). Every row the client can offer has its own, other skins' and later clients' names for the same windows included. The gamepad pack's All Windows list draws the same icons from its copy of the file.

#### The Options window

`options.lua` is this client's own tabbed Options window, in this UI's style on an opaque background. Every setting is a client option by name (`hs.UI.settings.Option` / `SetOption`).

A search field above its tabs filters the open tab:

- A setting stays when every word of the query is in its label, its tooltip or its section's title (on the Plugins tab, the plugin's name).
- Words may be typed loosely (`lib/fuzzy.lua`, tested in `tests/lua_scripts_test.cpp`): as the initials of words that follow each other ("fov" for Field of View); as a word's letters in order from its first, four letters or more; or one letter off, five letters or more.
- The key bindings, chat filters and colours match by their names. The key list reads its command names once for a search, and without one reads only the rows in view.
- A section with nothing left is left out whole, a note goes with its row, and the buttons under a section go while a search is typed. Clearing the field brings everything back (tested in `tests/ui_pack_windows_test.cpp`).

#### System windows

- **LoadskinWnd**: this client's user interfaces, Keep your layout.
- **LayoutCopyWindow**, **VideoModesWindow**.
- **NameChangeWnd** / **NameChangePetWnd** / **NameChangeMercWnd**: one script, `namechange.lua`.

They are opened by `/loadskin`, `/copylayout`, the EQ menu (VideoModesWindow; the modern pack's Options has no Video Modes button, its Resolution list does the same), `/changename`, `/changepetname`, `/changemercname`.

### Other shared windows

#### Gamepad panel bounds

Gamepad's shared `lib/window.lua` bounds each panel's physical size and position to the current viewport. This keeps titles and close controls reachable at narrow resolutions without changing the global font or UI scale.

Its scaled design dimensions leave a small screen margin, even when the theme's minimum readable scale would otherwise make a panel wider or taller than the display. Valid explicit positions and dimensions that already fit are kept; centred panels use their bounded dimensions. Each window still owns its body layout and scrolling when its content needs more room.

#### Item experience transfer

`ItemExpTransferWnd` is drawn by `itemexptransfer.lua` in both packs. An owned evolving item's display offers Transfer experience, with the source selected. The selectors list evolving possessions, bag contents and the cursor included, without moving them.

`hs.UI.evolving.State()` returns:

- `shown`, `pending`, `submitted`, `hasDetails`, `compatible`, `compatibility` (percent), `maxTransferLevel`, `canTransfer`, `status`;
- exact string identities `source`/`destination` (empty when unselected);
- `revision` and `items`.

Items and the optional `previewSource` / `previewDestination` have string `id`, `name`, `icon`, `level`, `maxLevel`, and `progress`. Preview items are the server's resulting forms, never substituted for the selected original identities.

The flow:

1. `hs.UI.evolving.Select("source"|"destination", id)` selects an exact string ID (empty clears it).
2. Transfer enters a separate review stage. Cancel sends nothing.
3. Confirm calls `hs.UI.evolving.Confirm(source, destination, revision)`. Every argument stays a string. A changed pair, approval revision or live original item invalidates confirmation.

`hs.UI.evolving.CanTransferDisplayed(screen)` and `hs.UI.evolving.OpenFromDisplay(screen)` provide the display entry. `hs.ShowWindow("ItemExpTransferWnd", false)` retires the selection. Gamepad Back cancels review first, then closes the window. The read functions need no extra plugin capability; selection, display entry and confirmation need Actions.

#### UI animations

`hs.UiAnimation(name)` explicitly resolves the first frame of a named `Ui2DAnimation` from the installed/default XML. It returns `texture, u0, v0, u1, v1, width, height`, or nil for unavailable/invalid art. Width and height are the authored frame dimensions, not the atlas size.

- This read-only opt-in works in an icon-only pack without changing `hs.UiTexture`, `hs.ChildArt` or the pack's normal art policy.
- Only a relative atlas filename and an entirely contained positive crop are accepted.
- Resolve handles on each draw rather than keeping them across texture-cache eviction.
- No game art or XML is bundled with the client.

#### Spell sets

Spell sets (`game/spell_sets.h`, `net/game_connection_spell_sets.cpp`) are the character ini's `[SpellLoadouts]` (`hotbuttons.ini`, read and written by `ActionsUi`). They are memorized as `CSpellBookWnd::MemorizeSet` does; `/memspellset`.

- For scripts: `hs.SpellSets`, `SaveSpellSet`, `MemorizeSpellSet`, `DeleteSpellSet`.
- For the books' set editors (Hailstone's own, not the original's): `hs.SaveSpellSet(name, spells)` saves a set from given spells, and `hs.EditSpellSet(n, name, spells)` renames and refills one, with nothing memorized. The ini is the same, so the stock client reads the sets.
- The code is in `app/lua_ui_spellbook.cpp`, with the book's `hs.SpellBook`, `SpellBookState`, `SpellIcon`, `SpellBookClick`, `SpellBookMemorize`, `ShowSpell`, `SpellDetails`.
- Testing: `test.book.tab=details|sets`, `test.book.select=<spell id>` and `test.book.edit=<set number>|new|gems` in `ui/settings/modern.txt`; `test.book.view=book|sets|edit<set number>|editnew` in `ui/settings/gamepad.txt`.

### Host code

#### The script host

`src/ui/lua/` holds the script host and the `ui.*` ImGui bindings.

The host (`lua_host`) does:

- script loading, hot reload and the `ui.window` registry;
- `ui.GetSetting` / `SetSetting` in `ui/settings/<name>.txt` in the Hailstone folder;
- the window layout keys (`LuaUi::character_setting`: `hud.*`, `tasks.listw`), kept in the character's `ui-<name>.txt` once a character is in the world (`LuaUi::set_character_settings`), falling back to the shared file (`docs/layout.md`);
- the "UI errors" and "Lua UI" windows.

The bindings (`lua_imgui.cpp`) take only ImGui's public window flags. They refuse the calls ImGui would only assert on or read through a null table with. They draw an image only from a texture the client handed out and has not freed (`src/ui/texture_ids.h`). Additions:

- `ui.CodeEditor` (`lua_code_editor.cpp`): a Lua editor with colours from `src/ui/code_syntax.h`, an error line and completion, over ImGui's own text box. An editor not drawn for about a minute, or every one not drawn last frame once more than 256 are kept, is forgotten.
- `ui.SetClipboardText` / `GetClipboardText`. A host may refuse them: MQ scripts get them only with every capability (docs/plugins.md).
- `ui.EdgeButton(id, x0, y0, x1, y1)`: an invisible button that takes the mouse up to the window's outer edge.
- `ui.FindWindowRect(name)`: a window's place and size by name.
- `ui.Begin(title, open, flags, collapseRight)`: the collapse button beside the close box, for that window alone.
- `ui.SetNextWindowCollapsed(collapsed [, cond])` and `ui.IsWindowCollapsed()`.
- `ui.PushStyleColors(list)` / `ui.PushStyleVars(list)`: many `PushStyleColor` / `PushStyleVar` in one call from flat lists `{ idx, r, g, b, a, ... }` / `{ idx, x, y or false, ... }`, each returning how many it pushed.
- A script's `PopStyleColor`, `PopStyleVar` and `PopFont` pop only what its own draw function pushed.

Other host behaviour:

- A window registered outside its script's loading (by code run later) that fails is shown in "UI errors" on its own, with a retry button. It runs again when it is registered anew.
- A long string match is held to the code's time limit by a check in the vendored Lua's pattern matching (`kMatchCheckKey`, THIRD_PARTY.md).
- `ui::lua::replaces(screen)` tells a C++ window whether a Lua window stands in for it.

#### Game art textures

`src/ui/ui_textures.{h,cpp}` makes the game's art into ImGui textures: any `uifiles/default` file, the first frame of any `<Ui2DAnimation>` of the EQUI_*.xml files, the item, spell and gem icon grids, and the screens' `<Location>` / `<Size>`.

#### The hs table

`src/app/lua_ui.{h,cpp}` is the `hs` table. The function names, arguments and returns are documented with the `hs` table. The answers come from `GameConnection`, the character sheet and the UI classes (`UiHost`, filled by the client every frame).

- Windows are models keyed by the XML screen name (`app/lua_ui_windows.h`, found through a name index).
- `hs.ReplaceWindow(name)` answers whether the game shows a window and where it goes. `hs.WindowShown(name)` answers only the first: the window still counts as replaced, but no place is worked out. Use it for a check, while a window the script draws calls `hs.ReplaceWindow` when shown, since that call places it.
- A list (`hs.ListInfo`, `hs.ListRow`, `hs.WindowList`) is built once a frame per window and list. It is kept until an `hs` call that can change it (a click, a sort, typed text, anything but a read).
- Each child is answered by its ScreenID with what the game's window class sets on it, and a click runs the client action the game's handler does.

The models are split over the files below. `hs.CursorAttachment` (the item on the cursor, `cursor.lua`) is also in the `hs` table.

### The lua_ui files

#### `lua_ui_values.cpp`

Labels and gauges by EQType (`hs.Label`, `hs.Gauge`), among them:

- the combat timer (label 137 "mm:ss" and gauge 29, the last 30 s, from OP_RestState's downtime);
- the title (74), ATK (23), the combat regeneration (212..214);
- the target of target's name (135);
- the group's and the extended targets' mana and endurance percents (139..148, 171..210);
- the pet's buffs (86..115) and the blocked buffs (550..579, 650..679);
- the aggro meter's names and percents in their colours (302..333).

Extras:

- A second argument `true` to `hs.Label` (1, 28) or `hs.Gauge` (1, 6, 27) puts the name plate's invisibility marks on the name caption. This is Hailstone's; the original's captions are plain names. Both packs' player, target and target-of-target captions use it.
- `hs.Gauge` and `hs.ChildGauge` also return the fill tint of a special value as CGaugeWnd::Draw gives it (`gauge_fill`: -2 full in dark red, -110..-10 purple, -2000..-1000 pink). The packs' group frames use it for a member out of the zone; `W.Gauge` (modern) draws it.
- `hs.BuffSlot`'s fifth answer is the buff and song windows' fade under 19 s (`game::buff_fade`, CBuffWindow::OnProcessFrame), which both packs' buff icons follow.
- The modern player frame shows the combat state (PW_CombatStateAnim's animation by `hs.ChildArt`) as a pip on the level badge, and the combat timer as a ring around it. The gamepad HUD shows the timer by the level.

#### `lua_ui_hud.cpp`

PlayerWindow, TargetWindow, GroupWindow, BuffWindow, ShortDurationBuffWindow, CastSpellWnd, CastingWindow, HotButtonWnd..HotButtonWnd10.

**Hot button menu** (`game/hot_button_menu.h`). An empty button's click, or the right button held on any button, opens the game's hotbutton menu through `UiHost::open_menu` (each UI's `menus.lua` draws it). The call is `hs.ClickHotButton(window, child, "left"|"right"|"rightheld" [, x, y])`; the test key is `HOTBTN:<screen>:<child>:rightheld`.

**Right clicks on gems and AA buttons.**

- An empty spell gem's right click pops up the spell select menu (CCastSpellWnd::MakeSpellSelectMenu), each spell in its target type's colour (`game::spell_target_color`, the shared gem tint mapping). Both packs' `menus.lua` blend a row colour too close to the panel's brightness toward the text colour.
- A right click on an alternate ability's button (a social of 120 + group) opens the AA window on that ability (`AAWindow`'s `activate`, CAAWnd::ShowAbility: its tab and row).
- On a spell gem's button it is the gem's own right click (`UiApi::click_gem`, HandleSpellRightClick).

**Bar layout.**

- The key map text sits in the button's top-right corner in both packs.
- The modern bars lay out vertically, the pager across the top, when "Buttons per row" makes them taller than wide. Dragging the modern bar's right or bottom edge or its bottom-right corner changes "Buttons per row" (its shape). Its regular right-click menu's Lock layout stops a bar being moved or resized by accident.
- The gamepad pack's bars go vertical when their page number is clicked (`hotbar.vertical.<window>`), as a column left of the minimap.
- The bar's own menu, settings and icon selection window are in `lua_ui_hotbar.cpp`: `hs.HotBarSetting(bar, name)`, `hs.HotBarMenu(bar [, x, y])`, `hs.IconSelectionIcons()` / `hs.IconSelectionIcon(n)`. A hot button child's `hs.WindowText` is its key map text. Scripts: `ui/modern/iconselection.lua`, `ui/gamepad/iconselection.lua`.

**Button faces and state.**

- `hs.HotButtonFace(window, child)` gives a social button's colour and a Lua social's face (docs/plugins.md "Lua socials"). The hot bars of both UIs draw it.
- `hs.HotButtonChecked(window, child)` is true for a hot button the pet window checks: a pet command button dragged to a bar, labelled "Pet <command>", while that toggle command is on (`game/pet_buttons.h`). The bars draw it pressed.
- `hs.HotButtonState(window, child [, y0, y1])` gives a button's check, cool-down (fraction, ms left, the rectangle's top and colour) and a spell gem's tint stage (CHotButtonWnd_UpdateButtonStates, `game/hot_button_state.h`, `app/lua_ui_button_state.cpp`).
- `hs.ButtonCoolDown(window, child, y0, y1)` gives the cool-down a window set on a plain button (the Actions and combat ability windows, the potion belt's slots).

**One call per slot.** The bars and gems read each slot in one call, every frame, since each of the calls above works the slot out again.

- `hs.HotButtonInfo(window, child [, into [, y0, y1]])` gives what `hs.HotButton`, `hs.HotButtonFace` and `hs.HotButtonState` give, as one record's fields: `kind`, `label`, `tex`..`v1`; `social` with `color`, `lua`, `running`, `enabled`, `faceCooldown`, `overlay`, `tooltip`; `state` with `checked`, `left`, `cooldown`, `top`, `tint`, `iconAlpha`.
- `hs.GemInfo(window, child [, into [, y0, y1]])` gives what `hs.ChildShown`, `hs.ChildTooltip`, `hs.ChildArt(.., "spellicon")`, `hs.GemTint` and `hs.GemOverlay` give for a spell gem: `shown`, `tooltip`, `tex`..`v1`, `tintR`..`tintA`, `overlay` with `coolR`..`coolA` and, given y0 / y1, `top`.
- `into` is a table the caller keeps, refilled in place.

**Buff and item overlays.**

- `hs.BuffSlot(window, child, true)` also gives the button's counter and limit overlays (as `hs.BuffOverlay`'s "counter" and "limit"). A buff window works them out only when asked (`StockWindow::buff`'s `overlays`).
- An item table from `hs.Item`, `hs.BagSlotItem` or `hs.SlotItem` carries `cooldown`, its slot's recast cool-down (CInvSlotWnd_Draw; colour `hs.CoolDownColor()`).

**Item preview** (CObjectPreviewWnd, `lua_ui_itemdisplay.cpp`):

- `hs.ItemPreview()`: open, show, name, onPlayer, serial;
- `hs.ItemPreviewImage(w, h)`: the 3D view's texture for this frame (`app/item_preview_view.h`);
- `hs.ItemPreviewControl("rotate"|"pan"|"zoom"|"reset", x, y)`;
- drawn by `ui/modern/objectpreview.lua` and the gamepad pack's `appearance.lua`; test key `PREVIEW:<slot>[:<bag>]`.

#### `lua_ui_web.cpp`

The in-game browser's window, BrowserWnd (`ui/modern/browser.lua`, `ui/gamepad/browser.lua`; docs/browser.md). The player's tabs:

- `hs.WebAvailable()`;
- `hs.WebOpen(url [, w, h])`: a new tab, or nil and why. Text without a scheme gets `https://`.
- `hs.WebTabs()`, and `hs.WebState(tab)` (url, title, loading, canBack, canForward, closed, why);
- `hs.WebNavigate(tab, url)`, `hs.WebNav(tab, "back"|"forward"|"reload"|"stop")`, `hs.WebClose(tab)`;
- `hs.WebText(tab, text)`: the gamepad pack's on-screen keys;
- `hs.WebOpenExternal(url)`: the system's browser;
- `hs.WebView(tab, w, h)`: draws the page at that size (the page takes it) and gives it the mouse, and the keyboard once clicked. While it has the keyboard, the game's key binds stay off and text input on; a click elsewhere gives it back.
- `hs.WebEval(tab, js)`, `hs.WebPost(tab, text)` and `hs.WebMessages(tab)` talk to the page (`window.hailstone`; messages only for a plugin's tabs).

In a plugin's window these act as that plugin ("plugin:<name>", its own tabs, its `web` permission and the server's sites; docs/plugins.md "Web pages"). `hs.WebOpenExternal` is for the player's windows only.

#### `lua_ui_scripts.cpp`

The Lua Scripts window, ScriptsWnd (`ui/modern/scripts.lua`, `ui/gamepad/scripts.lua`; docs/plugins.md "The Scripts window"), and the EQ menu's Lua Scripts row (`kMenuScripts`).

- `hs.Scripts([rescan])`: each script on disk or running (name, status, error, found, allowed, why, server, kept, autorun, caps), the folders, and whether a character's autorun list is there. The folders are read at most once a second.
- `hs.ScriptRun(name [, args])`, `hs.ScriptStop(name)`, `hs.ScriptPause(name)`, `hs.SetScriptAutorun(name, on)`.

#### `lua_ui_luasocial.cpp`

Lua socials for the Lua Social window (`ui/modern/luasocial.lua`, `ui/gamepad/luasocial.lua`): `hs.LuaSocial`, `SetLuaSocial`, `LuaSocialRules`, `LuaSocialFace`, `LuaSocialDryRun`, `PressLuaSocial`, `StopLuaSocial`, `LuaSocialExport` / `Import`, `LuaComplete` (mq.TLO paths through `mq::Registry::complete`, `mq.`, `button.`), `LuaSocialEditRequest`, `OpenLuaSocialEditor`, `FreeSocial`, `SocialToCursor`, `SocialColors`.

- `SetLuaSocial`'s `lines` make a social with no Lua plain again, with those command lines.
- `LuaSocialExport(n [, t])` shares a plain social too (or an editor's unsaved `t`).
- `LuaSocialImport` gives `lua` and `lines`.

#### `lua_ui_abilities.cpp`

`hs.Disciplines`, `hs.UsableSkills` and `hs.UseSkill(id)` (a usable skill pressed as its hot button does; the gamepad pack's Tracking tile).

- CombatAbilityWnd's `CAW_CombatEffectTimeRemainingGauge` and `hs.Gauge(26)` give the active discipline's duration fraction and seconds text. Both packs show the text above the bar and let the active icon end the discipline.
- The ability entries show a red reuse fill and seconds countdown from their shared timer.
- `hs.ButtonCoolDown` keeps its first five results and adds, for combat ability buttons, remaining milliseconds and focus-adjusted reuse milliseconds as results six and seven (both zero while idle or without timing detail). A shared remaining time may exceed that ability's own reuse.

#### `lua_ui_misc.cpp`

- **PetInfoWindow**: its command buttons' layout is `UiHost::pet_buttons` (`game::PetButtonLayout`). The right button down on Pet<n>_Button (`hs.ClickChild(window, child, "rightdown")`) pops up the command menu of the commands CanUsePetCommand allows. The choice is saved as the UI ini's `PetButtonState`. `hs.ChildState`'s fifth result is the button's bAction, a disabled button that still takes the right button.
- **ActionsWindow**, **SpellBookWnd**.
- **CompassWindow**: `hs.CompassStrips` / `CompassMarkers` over the heading the compass shows. That is the true one plus CCompassWnd's error below Sense Heading 200 (`game/compass.h`), run each frame by the app and reset for 5 s by a successful Sense Heading.
- **RespawnWnd**:
  - server destination names;
  - the marker cell as Handle_OP_RespawnWindow fills it: "*" for the default and "r" for a row whose final byte is set;
  - an unavailable row is `texts.disabled`;
  - accepted hover resurrection enables the reserved final row locally and leaves the selection;
  - resurrection text is red/green for unavailable/available;
  - Select needs an available destination and the packet delay;
  - the timer gauge is EQType 28.

The original Respawn window stays registered and drawn beside confirmation dialogs. The packs put a resurrection offer in front once, as it appears, never every frame. ConfirmationDialogBox's `ResurrectionOffer` child, not one of the XML's, is shown while the question is that offer.

#### `lua_ui_tint.cpp`

BodyTintWnd and ColorPickerWnd. CColorPickerWnd serves any caller (`UiHost::pick_color(rgb, done)`, the caller's notification 0x25): the tint brush, the map toolbar's colour buttons, the label window, the options window's Colors page.

#### `lua_ui_itemdisplay.cpp`

ItemDisplayWindow (.. #6) over `game/item_display.h`: info lines, the stat grid, heroic bonuses and the description. Pieces are placed where the game places them (`hs.ChildRect`, relative to the piece's parent).

- The description has CreatePlaceableString's "This item is placeable in ..." line, the collection hint and a trophy's benefit lines.
- `net::Item::placeable_flags`, `collectible`, `no_destroy` and `trophy_benefit` read the fields EQEmu's struct calls unknown; `ItemDisplayContext::trophy_benefit` gives the benefit.
- A left click on `IconButton` (`hs.ClickChild(window, "IconButton", "left")`; the modern pack's icon, the gamepad pack's icon or X) links the item into the active chat window's input, the Always Chat Here one when set, as the loot window's Link All does (`request_chat_link_all`). Not while `game::benefit_flag_max` is above 0.

#### `lua_ui_dialogs.cpp`

- **QuantityWnd**: the connection's stack splits and the windows' own dialogs.
- **OptionsWindow**: the key bindings, with a key bound to more than one listed command coloured red (Hailstone's own marking), and the chat filters and user colours lists that each pack's `options.lua` (this client's own Options window) shows. Its settings are client options by name (`hs.Option` / `hs.SetOption`, `app/client_app_options.cpp`).
- **SpellDisplayWindow** (.. #6) over `game/spell_display.h`. The Modern spell bar keeps the display open when a held right click is released over it, unlike the original. Releasing elsewhere closes the temporary display, and a quick right click still forgets the spell.

Which spell or item each display shows: `hs.DisplaySpellId(n)` and `hs.DisplayItemData(n)` (`lua_ui_spellbook.cpp`; n 1..6 as the window's number, nil when there is none).

#### `lua_ui_social.cpp`

FriendsWindow, GuildManagementWnd, RaidWindow.

A raid roster's `hs.ListClick("RaidWindow", list, row, "right")` opens that member's shared menu, drawn by each pack's `menus.lua`. `RAID_MemberActionsButton` opens it for the selected member. The menu captures the name and rechecks membership and permissions when activated. The native member button APIs stay available.

#### `lua_ui_socialedit.cpp`

SocialEditWnd (CSocialEditWnd). It opens on a social by a right click on its hot button or its Actions window button (`UiHost::open_social_editor`, `hs.OpenSocialEditor(index)`). Accept writes the name, lines and colour to the character's socials (`hotbuttons.ini`).

For the packs' editors:

- `hs.SocialEditorSocial()`: the social it is open on, or nil;
- `hs.SlashCommands(prefix)`: the game's commands starting with it, in the order the game tries them (not the client's or plugins');
- `hs.PressSocial(n)`: a hot button's press (`ActionsUi::press_social`);
- `hs.CurrentSocial()` and `hs.StopSocial()`;
- `hs.SocialLine(text)`: a line's `/pause` and `/timer` prefixes and its command as a press reads them (`game::split_social_line`), with what the interpreter makes of the command: `kind` and the game command's `name`.

The Actions window shows each social's name in its colour (GetSocialTextColor).

#### `lua_ui_rewards.cpp`

RewardSelectionWnd and the reward-selection API below. The reward key opens this stock window.

- Both packs have separate Pending and Preview pages, common rewards, explicit choice confirmation, item inspection, and manual Restore.
- TaskWnd's Preview uses the currently selected task id. It is unavailable when that task has no selection reward.

Choices are numbered clickable cards with a summary of their received grants:

- Long captions are fitted to the available width without splitting UTF-8 names; hover or controller focus shows the full text.
- The selected choice's full item names, quantities and lore flags appear in the details pane, apart from common grants.
- Clicking an item's tile, name or Inspect item control explicitly requests its stats.
- The page chooser takes space only when there are several offers.
- Claim and confirmation are the primary footer actions; Restore and Close have their own row.
- A lone choice still needs an explicit selection and confirmation.
- Preview stays read-only, and Restore never unlocks an uncertain claim.
- Reward item IDs are not icon IDs. When no icon is known, the UI shows a neutral tile instead of guessing or querying during drawing.

**Reward-selection API.** `hs.RewardSelection([pendingRevision, previewRevision])` returns nil outside a character session, otherwise `{shown, pending, preview}`.

- Each lane has `revision`, `shown`, `claim_pending`, `claim_uncertain`, `inspect_pending`, `message`, `unchanged`, and `tabs`. Passing a matching revision omits only `tabs`; status fields stay fresh.
- Revision and tab `token` values are opaque decimal strings, never Lua numbers.
- A tab has `title`, `pending_id`, `set_id`, `multiplier`, `blocked`, and `options`. Each option has `wire_id`, `common`, `flags`, `label`, and `entries`.
- Entries keep `type`, `entry_id`, `string_id`, `description`, the four raw `values`, `flag`, presentation labels, and item rows `{id, quantity, name, lore, lore_group}`.
- Server strings are plain text.

**Reward-selection actions.** `hs.RewardSelectionClaim(revision, tabToken, wireOption)` and `hs.RewardSelectionInspect(lane, revision, tabToken, wireOption, entryId, itemId)` return `(accepted, reason)`.

- Id arguments are exact unsigned 32-bit Lua integers; lane is `"pending"` or `"preview"`.
- Acceptance means admission to the current transport, not reward delivery. Preview tokens cannot claim.
- `hs.RestorePendingRewards()` explicitly requests the server's pending view. It never retries a claim.
- `hs.RewardSelectionClose(lane)` closes one lane, and `hs.ShowWindow("RewardSelectionWnd", false)` closes both.
- Only an explicit native-window open can request Restore; the getter stays passive.
- Action calls need the Actions capability; closing uses Commands; the getter is read-only.

The protocol page documents uncertain claims and nonce-less inspection replies.

#### `lua_ui_achievements.cpp`

AchievementsWnd and AchievementWnd, drawn by `achievements.lua` in both packs.

- The main view has ordered parent/child categories, descendant filtering, literal name search, All / Open / Completed / Locked filters, received completion and points totals, and explicit Refresh / Compare target / Clear comparison buttons. Hidden and unknown states are left out of the list.
- The independent detail window shows objectives and comparison columns, or a linked snapshot labelled with its player.
- The gamepad pack adds its search keyboard and B-close handling.

**Achievement summary API.** `hs.Achievements([catalogToken, stateRevision])` is passive and returns nil outside a character session.

- Fresh metadata includes `shown`, `detail_shown`, optional zero-based `selected_index`, `detail_source` (`"own"` or `"link"`), `ready`, `progress_ready`, `refresh_pending`, `message`, `points`, `total_points`, `completed`, `total`, and `comparison = {pending, ready, name, message}`.
- `ready` means full primary state was received. Objective count initialization is separate (`progress_ready`).
- Totals include known, non-hidden own states only.

**Category badges.** A category's received `icon` is a stock XML animation name. Both packs request its base, Hover or Selected variant through `hs.UiAnimation`, falling back to base and then the plain category name.

- These resources are wide badges with baked lettering. Their aspect is kept and no duplicate name is drawn over them.
- Completion counts share the badge's row in a separate right-aligned column.
- The full name and totals stay in the mouse/controller-focus tooltip.
- Art selection does not change category filtering or send a request.

**Tokens and the catalog.** `catalog_token` and `state_revision` are opaque unsigned 64-bit decimal strings: pass them back unchanged, without `tonumber`.

- A matching first argument sets `catalog_unchanged=true` and omits `catalog`. A matching second argument independently sets `states_unchanged=true` and omits `states`.
- Keep each cached table with its token, and clear both outside the character session.
- An available uncached catalog has `categories` and `definitions`.
- Categories contain `id`, `parent_id` (`4294967295` for a root), `name`, `description`, `icon`, `display_order`, `child_ids`, and `associations = {{achievement_id, text, display_order}, ...}`.
- Definition summaries contain `index`, `id`, `name`, `description`, `icon_id`, `points`, `has_reward`, `persistent`, and `version`. Keep the received zero-based index through sorting and filtering: it differs from the achievement ID.
- `states[index + 1]` contains `known`, `status` (`"Completed"`, `"Open"`, `"Locked"`, `"Hidden"` or `"Unknown"`), and optional `completion_time`.

**Selected detail API.** Passive `hs.Achievement()` returns the full selected detail, or nil without a current selection.

- Fields are `shown`, `revision` (opaque decimal string), `catalog_token`, `definition_index`, `id`, `name`, `description`, `icon_id`, `points`, `has_reward`, `source`, `player_name`, and `own`.
- The `own` table describes the selected source: character state for `source="own"`, or the named player's linked snapshot for `source="link"`. It contains `known`, `status`, optional `completion_time`, and `components`.
- An own-source detail can also contain `comparison`, with the same state fields plus `name`.

**Components** contain `id`, numeric `type`, `name`, `description`, `required_count`, `display_order`, `satisfied_known`, `satisfied`, `count_known`, and optional `count` and `count_text`, in definition bucket order 0, 1, 2, 3.

- `count_text` is the exact unsigned decimal value, including values beyond signed Lua integers. Presentation should use it instead of converting the legacy numeric `count` to text.
- Missing counts stay unavailable; known zero is a real count.
- Satisfaction bits never imply counts or achievement completion.
- Type 3 is descriptive only, with unknown runtime satisfaction and count.
- Comparison rows match by bucket and component ID.
- Received names and descriptions are plain text.

**The browser.** Both packs use `lib/achievement_browser.lua`: a collapsible category tree, a compact achievement/status/points list, and inline own-achievement detail.

- Category totals deduplicate received descendant associations and leave out hidden or unknown entries. Category expansion, search and status filters are local. Search includes names and descriptions.
- Wider windows put the list and detail side by side; smaller windows stack them in separate scrolling panes. Short detail panes put the description behind an expandable heading to keep objectives visible.
- Objective bars show known counts only and do not decide completion. Known completion timestamps are formatted as local calendar dates.
- Comparison matches components by bucket and ID after presentation sorting.
- Reward and Link stay explicit actions at the detail's foot.
- Closing the main browser also hides its own detail window. Linked snapshots stay independent read-only windows, and externally opened own details can still appear when the browser is closed.

**Achievement actions** return `(accepted, reason)`. Index arguments are unsigned 32-bit Lua integers; catalog tokens are exact decimal strings. The connection rejects stale tokens, unavailable definitions and invalid indices again.

- `hs.AchievementOpen(catalogToken, index)` selects the current received own definition locally and opens AchievementWnd. It needs Commands and sends nothing.
- `hs.AchievementRefresh()` requests a new snapshot. `hs.AchievementCompare()` requests the current nearby player target. `hs.AchievementReward(catalogToken, index)` requests rewards through the shared RewardSelectionWnd; View Reward carries the definition index, never its achievement ID. These need Actions.
- `hs.AchievementClearCompare()` clears comparison locally and needs Commands.
- `hs.AchievementLink(catalogToken, index)` needs Chat. It adds the known own snapshot to the current chat composition without sending chat.

**Closing and ownership.**

- Close the views independently with `hs.ShowWindow("AchievementsWnd", false)` or `hs.ShowWindow("AchievementWnd", false)`.
- An explicit main-window open may request its native refresh. Drawing or reading a getter never requests anything.
- Reopening a retained detail keeps its linked or own source.
- Catalog or connection replacement invalidates selected indices and detail ownership.
- Comparisons and linked snapshots never replace own state, and linked details do not expose the own reward/share buttons.
- Acceptance reports local selection or request admission, not a server grant.

**Chat composition** (`lib/chatlinks.lua` in both packs). Item and achievement tags are stored intact while the editor shows their readable labels.

- The expanded typed prefix goes back to `hs.ChatTakeFocus`, and an explicit Send restores the exact tags.
- Malformed achievement envelopes show a generic label without raw metadata.
- As a composition safeguard, a line carrying tags is rejected intact, with a visible error, if expansion exceeds ten tags or 512 raw bytes, rather than letting the network truncate it through a tag. The gamepad channel prefix counts toward that budget.
- Ordinary untagged chat keeps its existing longer editor/network behaviour.
- The gamepad edit field holds the same 2048-byte buffer as Modern instead of cutting a valid raw link at 256 bytes.

#### `lua_ui_map.cpp`

The map: MapViewWnd, MapToolbarWnd and EditLabelWnd (`map.lua`, `maptoolbar.lua`, `editlabel.lua`), and the map's own functions `hs.DrawMap`, `MapZoom`, `MapMouseDown`, `MapMouseUp`, `MapPanReset`, `MapRightClick` over `game/map_data.h`.

**Map files.** `maps/<zone>.txt` (layer 0) and `<zone>_1.txt`..`_3.txt`, with `L x1, y1, z1,  x2, y2, z2,  r, g, b` and `P x, y, z,  r, g, b,  size,  text_with_underscores` records. The file name is the zone's short name (EQG zones alike). Map coordinates are (−X, −Y, Z) of the game's spawn axes, i.e. (−world.y, −world.x, z) here.

**Drawing**, as MapViewMap::Draw draws:

- the 472 × 360 fit; zoom (wheel ×1.5, buttons ×1.1, range kept across zones, 1000 at first); pan;
- the layer, label and height filters; label placement (24 places); the label search;
- the maps directory combo (`Main` `MapSkin`);
- lines, labels, the line being walked, group members, the player's arrow (Sense Heading over 190; else a plus) along the heading the compass uses, and "Range = n".

The map omits other players, NPCs and the target. The modern pack's map marks the NPCs of its Find page when asked, a Hailstone addition.

**The toolbar** needs the Planes of Power bit. It has hover, marks, Ctrl-drag lasso, start / end line, add point, remove line, join at intersect, automap, add / edit / move / remove label, and save. Its colour buttons go through the colour picker (`UiHost::pick_color`), and Clear Map / Clear Layer ask for confirmation.

**Saving.** Saved layers are written to the server's maps folder (`servers/<profile>/maps/[<dir>/]<zone>_<n>.txt`) and read before the server's, the global and the game's maps (docs/layout.md "Maps"). The map window's settings are the UI ini's `[MapViewWnd]` keys (CMapViewWnd::LoadIniInfo / StoreIniInfo).

**Auto Z** is a client extension beside the original's height filter (which hides).

- With it on, lines, labels and a plugin's marks more than `AutoZAbove` above or `AutoZBelow` below the player's feet (each 1..1000, default 30) fade, to 15% at three times that side's range. A line whose span holds the feet stays whole.
- It applies to MapViewWnd and to `hs.DrawMinimap` / `hs.DrawZoneMap`.
- `on, above, below = hs.MapAutoZ([on [, above [, below]]])` reads or sets it (`above` alone sets both). It is kept as `AutoZ` / `AutoZAbove` / `AutoZBelow` in `[MapViewWnd]`; an earlier version's single `AutoZRange` is read as both.
- The stock window's layout has no button for it. The modern pack's map (the arrow beside Auto Z) and minimap (its frame menu) set the two ranges with sliders.

**Paths and the atlas.**

- The find path to the destination (`game/player_path.h`). For the modern and gamepad maps: `hs.FindMapLocation(x, y)` as the stock map's right click, `hs.FindPathShown()`, `hs.EndFind()`.
- The atlas page (`game/atlas.h`, `hs.DrawAtlas`).
- `hs.DrawZoneMap` draws another zone's map without the player.

The modern map's World view and zone finder (docs/navigation.md "Zone atlas"; `ui/modern/lib/atlas.lua`) use:

- `hs.AtlasIndex()`: the pages and every zone link;
- `hs.AtlasPageData(file)`, `hs.AtlasTexture(file)`;
- `hs.ZoneLinks()`: asks the server once a session; whether its list came, and how many zones are known;
- `hs.ZoneRoute(to [, from])`;
- `hs.ZoneExits()`: the current zone's zone lines in map units;
- `hs.ZoneExitWalk(zone)`: a `/nav` walk through the nearest line to that zone;
- `hs.ShowZoneMap([zone])` and `hs.MapZoneShown()`.

#### `lua_ui_player.cpp`

InspectWnd, ConfirmationDialogBox (the ground drop's question, the connection's questions and a window's own: `UiHost::confirm`), BookWindow (ProcessText's paging), NoteWindow, TextEntryWnd (`UiHost::text_entry`), BandolierWnd, LargeDialogWindow (server popups, OP_OnLevelMessage), TargetOfTargetWindow, ExtendedTargetWnd.

The file also has PotionBeltWnd, the extended target window's context menu, and the two CTipWnd windows on the TipWindow screen:

- **The context tips** (`TipWindow`, `UiApi::activate_tip`). They come from:
  - the windows' OnShow as each opens (the table in `lua_ui.cpp`, `context_tips_on_show`);
  - the connection's events (`GameConnection::take_context_tips`: sitting, level 10, consider, the faction messages, experience, the group and guild invitations, consent, world containers; the duel and death tips);
  - the app's (the red con tip on the attack key and on a target change while attacking, the interactive object click);
  - the Actions window's page and the player customization window as they show.

  The table keeps the original's 90 ids, the voice macro and audio trigger entries included.
- **The tip of the day** (`TipWindow#2`): help\\tips\\totd.txt at each zone-in (`UiApi::activate_tip_of_day`).

#### `lua_ui_help.cpp`

HelpWindow (help\\help.html, the pages' href links followed with Back through the history, Contents, the Tips button's tip of the day, Customer Service's /petition) and `load_stml_page` (a game page).

A link in an STML box goes to its window: `hs.StmlLink(window, child, message, data)`, `StockWindow::stml_link` (message -1 for an href; `lib/stml.lua`).

#### `lua_ui_login.cpp`

No pack draws these since the Classic UI went; the client's own screens show.

**Character creation page** (EQUI_CharacterCreate.xml, which the Classic UI's `layouts/charcreate.lua` drew):

- the race and class portraits of world's combinations, gender, the deity and city combos, the stats with their points, Random Name (OP_RandomNameGenerator), the help box from dbstr_us.txt, and Create with the expansion and points checks;
- the name goes for approval through `UiHost::char_create_submit`;
- the page covers the screen without a background (`sidl.Window`'s `fullscreen` / `transparent` options, CCharacterCreation_UpdateLayout), over the pre-game scene's model player (app/client_app_pregame.cpp). The window dresses the model (`UiHost::model_player`, `char_create_look`), and the Appearance button opens PlayerCustomizationWnd on it;
- the Classic UI's `lib/shade.lua` drew the portraits as labelled tiles;
- testing: `HAILSTONE_CHAR_CREATE=1` opens it at the character select.

Without it, the client's own creation dialog (`ui::character_create_screen`) shows: name, race, class, gender, the Deity combo (by the deity's name, GetDeityDesc) and the Start city combo (zone long names), as FillDeityCombo / FillStartCityCombo list them. The stats have points to spend under the same rules: each at its base, + up to base + 25 and at most 150, and Create once every point is spent.

**Server select page** (EQLSUI_ServerSelectWnd.xml, once the Classic UI's `layouts/serverselect.lua`), over the client's world list (`UiHost::server_select`, its buttons as `UiHost::server_select_action`). The client's own screen stays off while the script runs it. It has the quick connect to the world played last on that login server (hailstone.ini `login.last_world.<profile>`). Testing: `HAILSTONE_SERVER_SELECT=1` keeps a test login (`--user`) on the server select.

A window of the screens before the world answers `StockWindow::pre_game()`.

#### `lua_ui_pregame.cpp`

Hailstone's own screens, not the original's: the login, server select, character select and character creation screens. The packs draw them in their own look (`ui/<pack>/login.lua`, `serverselect.lua`, `charselect.lua`, `charcreate.lua`).

**Views.** `hs.LoginScreen()`, `hs.ServerSelect()`, `hs.CharacterSelect()` and `hs.CharacterCreate()` give the client's views (`ui::LoginView`, `ServerSelectView`, `CharacterSelectView`, `CharacterCreateView`). Each is nil while another screen shows; creation's also while the customization window is up on the model.

**Revisions.** The login, server select and character select views carry `view.revision`, a fingerprint of everything in the view.

- `hs.LoginScreen(revision)` (and `hs.ServerSelect(revision)`, `hs.CharacterSelect(revision)`) gives `true` while nothing changed since the view with that revision. A window then keeps the table it holds instead of a new one each frame; the screen is still the pack's that frame.
- Without the argument the whole view comes each call.
- A script that writes into its held view (the modern pack's fields show what was typed at once) drops it and reads the whole view the next frame.

**Actions.** These do what the ImGui screens' fields and buttons do (the file's comments list them; lists are 1-based):

- `hs.LoginSet(field, value)` and `hs.LoginDo(action, ...)`. The view has `view.version`, the running build as `build_version()` gives it, which every login screen shows small at its foot. It also has the updater's `view.update` and its `"updateDownload"`, `"updateRestart"`, `"updateSkip"` and `"updateNotes"` actions (docs/updater.md).
- `hs.ServerSelectDo(action [, i])`. The worlds come sorted: the favourites first, then the others, each by name ignoring case (`ui::server_list_order`). `"favorite", i` marks or unmarks one, kept per login server as hailstone.ini `login.favorite_worlds.<profile>`, the names joined by `|`. A Hailstone difference: the original lists the worlds in the login server's order.
- `hs.CharacterSelectDo(action [, arg])`.
- `hs.CharacterCreateDo(action [, value])`. Creation's rules are shared with the ImGui screen (`ui::create_normalize`, `create_set_deity`, `create_stat_up` and the rest in `src/ui/dev_screens.h`). `hs.CharacterCreateDo("randomName")` asks OP_RandomNameGenerator for a name, which the client puts in the view.
- `hs.CharacterCreateHelp(kind, id)` gives the game's description of a race, class, deity, start city or stat (dbstr_us.txt types 8, 9, 14, 15 and 10, as the stock page's help box) and its name.

**How the screens are replaced.**

- Reading a view in a frame is what replaces the client's screen (`UiApi::replaces("LoginScreen" | "serverselect" | "CharacterListWnd" | "CharacterCreateScreen")`).
- The actions come back to the client through `UiHost::login_action`, `server_select_action`, `char_select_action` and `char_create_action` the next frame.
- Only the pack's own windows see these screens. A plugin's window gets nil and its actions do nothing, since the login view holds the typed or saved password.
- A pack without these scripts (or `ui.cpp_windows=1`) gets the ImGui screens of `src/ui/dev_screens.cpp`.

**Interface per server.** On the login screen the interface is the picked server's (or the one its Interface list names), switched to at once, so the screen shows in the UI that server plays with. `hs.LoginSet("ui", id)` also writes it to a saved server's `server.ini` at once (only `ui`: its other fields wait for Save or Log in), so picking another server or quitting keeps it.

#### `lua_ui_layout.cpp`

LoadskinWnd (the user interfaces as skins, /loadskin) and LayoutCopyWindow (/copylayout).

- LayoutCopyWindow copies another character's UI.ini or defaults.ini, for a screen size its keys keep, over this character's windowed places. The characters played last come first.
- Hailstone adds the LC_CopyChatCheckBox / LC_CopyOptionsCheckBox boxes, which copy chat.ini and options.ini.
- Hailstone also adds the first-entry offer: activate(0) at the zone-in of a character with no UI.ini, when hailstone.ini ui.offer_layout_copy (option offer_layout_copy, on by default) is on and another character has a layout. LC_FirstVisitText and LC_OfferCheckBox show only then.

#### `lua_ui_names.cpp`

NameChangeWnd, NameChangePetWnd and NameChangeMercWnd over `net/game_connection_names.cpp` (CNameChangeWnd and its copies).

- The server's invitations (OP_InvokeNameChangeImmediate / Lazy, OP_InvokeChangePetName(Immediate), 0x63d7 / 0x4cd9 for the mercenary) open them with the reminder prompt, unless the character ini's `[Preferences] Disable*NameChangeReminder` is set. The immediate ones clear it.
- `/changename`, `/changepetname`, `/changemercname` with no name open the manual prompt.
- Submit Name (or Enter in the box) runs the command (4..15 characters, the confirmation 0x7c / 0x8c / 0x8d). Its yes sends {new name, character name, 0} as OP_GMNameChange, OP_ChangePetName or 0x36db and shows "Request Pending" until the answer. The result string is printed and shown as an OK dialog; a success closes the window.
- Disable Reminder asks, then saves the flag.
- The character ini is this client's `hotbuttons.ini` (`ActionsUi::char_ini_int` / `write_char_ini`, `GameConnection::set_character_ini`).

#### Other lua_ui files

- `lua_ui_items.cpp`: InventoryWindow, Bag1..Bag36, WorldContainer, LootWnd, MerchantWnd, BankWnd / BigBankWnd, TradeWnd, GiveWnd. WorldContainer is the open tradeskill object in the zone, when it has a window of its own: opened as a container or by the tradeskill window's Experiment. The client's plain fallback window shows only without a UI pack's.
- `lua_ui_chat.cpp`: the chat windows (32 windows, each chat channel routed to one, the game's chat window menu and [ChatManager] keys; this client's tabs) over the client's chat lines, in the eqclient.ini user colours.
- `lua_ui_menu.{h,cpp}`: the main button's cascade menu (Hailstone's own layout, or a server's `uifiles/default/CascadeMenu.txt`; docs/layout.md), drawn by `menus.lua`. The main button window itself (EQMainWnd) is in `lua_ui.cpp`.
- `lua_ui_progress.cpp`: SkillsWindow, TrainWindow, AAWindow, TaskWnd, TaskSelectWnd (over M9's `GameConnection` functions).
- `lua_ui_market.cpp`: BazaarWnd (the trader window), BazaarSearchWnd, BazaarConfirmationWnd, BarterMerchantWnd, BarterSearchWnd, BarterWnd.
- `lua_ui_marketplace.cpp`: MarketplaceWnd (the MKPW_* children). MKPW_CategoriesTree, MKPW_Thumbnails, MKPW_NewItems and MKPW_FeaturedItems read as lists; the file's header gives their columns.
- `lua_ui_mail.cpp`: MailWindow and MailCompositionWindow.
- `lua_ui_video.cpp`: VideoModesWindow (the display modes, refresh rates in full screen, Accept with the keep-it question).
- `lua_ui_zoneguide.cpp`: FindLocationWnd and ZonePathWnd (no zone guide data on EQEmu).
- `lua_ui_fellowship.cpp`: FellowshipWnd (up to 64 server-supplied members, invitations, MOTD, leadership, XP sharing, exact vitality values, resource-backed campsite filters, descriptions and expiry; both inventory packs use vitality gauges 147/148) and AlertHistoryWnd.
- `lua_ui_story.cpp`: StoryWnd (storyline\\story*.txt, newest first). A page's href links are followed (`stml_href_path`, shared with the help window). Settings: hailstone.ini news.last_read / news.automatic. It shows at the first zone-in when a story is new and Automatic is on.

### Shared host pieces

#### User colours

`user_color_rgb(i)` (app/lua_ui_windows.h) is CDisplay's table of 99 (and Hailstone's 54 extended ones). It is read from the character's `chat.ini` [TextColors] over the asset folder's eqclient.ini, with the game's defaults for missing ones.

The Colors page writes them to `chat.ini` (`servers/<profile>/characters/<world>/<name>/`, core/character_files.h), as the chat filters page does. The asset folder is never written.

#### Context menus

A model's `context_menu(host, x, y)` opens its menu with `UiHost::open_menu(x, y, rows)` (rows: text, colour, check, enabled, separator, `act`, `submenu`).

- `menus.lua` draws them (`hs.GameMenus` / `GameMenuHover` / `GameMenuClick` / `GameMenuClose`).
- `hs.GameMenus(scale)` measures the rows at a scale. The modern pack takes it from its `menus.scale` setting: 125 % by default, set in Options > Interface "Menu Size" or by Ctrl+wheel over a menu.
- A right click on a Lua window opens the model's menu when it has one. Alt + right click (or a menu's "Window" row, `UiHost::window_menu`) opens this client's window menu.

#### Dialog owners

- A window opens the quantity dialog with `open_quantity(host, QuantityOpen, done)` (`game::quantity_dialog_open`). It is accepted at once, or shown at the caller's point with the slider's start; `done` gets the quantity.
- A yes / no question uses `UiHost::confirm`, and a text question `UiHost::text_entry`.
- `UiHost::click_rect` is the screen rectangle of the child the last click went to (the callers' points).
- Models can also answer drop-downs (`StockWindow::combo` / `combo_select`, `hs.ComboInfo` / `hs.ComboSelect`) and move their window (`take_move`: centred dialogs).

#### The UI scale

The game fonts are built at the display's content scale (or `--ui-scale`), so the packs' windows lay themselves out at that scale (`ui.GetIO().uiScale`). Sizes stored in the UI ini stay unscaled.

`ui.GetIO()` gives the same table to every call (each Lua state its own), with that frame's values. It is read-only: a write raises an error.

#### Window places

`src/ui/ui_ini.{h,cpp}`: when a character enters the world, its windows are placed from `servers/<profile>/characters/<world>/<name>/UI.ini` over the game's `defaults.ini`, as the game reads them. The game directory's `UI_<name>_<server>.ini` seeds it and is never written.

- **Place and size**: the XML rectangle, then `Width`/`Height` and `XPos`/`YPos` for 1280x1024 and `...Windowed`. The windowed ones are stored for a 4096 x 4096 screen and moved to the window's size by `ConvertToRes`. Its sums wrap in 32 bits as the original's do, so a value near the int limits places the window as the original would.
- **Shown or not**: `Show` is read from the character's file only (defaults.ini too for HotButtonWnd). Without it a window starts shown when its IniFlags have bit 9: player, target, group, buffs, songs, spell gems, compass, chat, EQ button, hot bar 1 from defaults.ini. Hailstone clears it for the aura window, which RoF2 starts shown, so that one starts closed.
- **Chat windows**: a character whose file keeps no chat windows of its own yet (no `[ChatManager] NumWindows`) takes defaults.ini's extra chat windows as tabs of the main window. After that the chat windows are read from the character's file alone. This is a deliberate change: the game's defaults.ini makes "Other Chat" a window of its own, and read through defaults.ini it came back at every start after the player closed it.
- Moves, resizes and show / hide are written back in the same keys.

The game's defaults.ini lays the HUD out for a large window: at 1280 x 720 player, hot bar, group and chat overlap. `--window-size 1920x1080` shows them side by side.

### Developer tools

#### Developer windows

The info panel, the spawn list and the "Lua UI" control window are off unless `ui.dev_windows=1` in `hailstone.ini`. Ctrl+Shift+D turns them on or off.

#### The performance window

Open it with Ctrl+Shift+P, `/perf`, or `ui.perf_window=1`. It shows frame times, where each frame's time goes (the main loop's phases, averaged over 60 frames beside the slowest frame of the last 300), renderer counts, memory and traffic.

**With frame generation** (docs/graphics.md "Frame generation"):

- the headline shows the rendered and the displayed frame rate (`app::frame_rates`: every image presented against the real frames);
- a Frame generation section adds the share of real frames that had an in-between frame, the totals and why frames were skipped (a cut or resize; no free swapchain image), the present mode and image count, and the last wait for the second image (part of the Present phase);
- it also shows the GPU time of frame generation's work, from timestamps round it (with an in-between frame, and for a real frame's composite alone; not shown where the device cannot time);
- the graph and the phases stay per real frame.

**Log hitches** (`ui.perf_log_hitches=1`) writes every frame of 2.5 times the typical frame time (20 ms at least) to the log as `perf: hitch ...`. The line has its phases, longest first, then the named sections of the frame that took 1 ms or more (`; in it: ...`).

**Frame sections** are laps the main loop and `LuaUi::frame` mark (`core/frame_sections.h`):

- the Update, UI and Record phases' main calls. Update's are the connection (where a zone loads), automation, the world objects, the player, and the spawns' smoothing (where a new race's model is built);
- the Record phase's cover and water maps, apart from the lights' cubes;
- each Lua window, and the map views' lines, labels and spawn marks inside it;
- the script scan and the Lua garbage collection.

The window's "Frame sections" table shows the same sections at all times: each one's average over the last 60 frames (0 ms in a frame it was not marked in) and its slowest frame, longest first, with a filter (`app::SectionAverages`).

- While the scene is recorded on the worker threads (`display.parallel_record`, docs/graphics.md "Worker threads"), the Record phase's per-renderer laps are named "... queued" ("zone queued", "actors queued"). They time only the setting up and queuing of each draw. The recording itself is in "scene record (opaque)" and "scene record". With it off they keep their "... draw" names and time the drawing.
- The table is worked out again twice a second (`app::kSectionsRefresh`) and at once when the filter changes, and only the rows scrolled into view are drawn. Rebuilding and drawing hundreds of rows every frame cost the frame time it was measuring.
- Sections nest: "Lua UI frame" holds every "Lua window ..." row.
- The "KB/frame" column is what the Lua state allocated in "Lua UI frame" and in each Lua window, averaged the same way. The state's allocator counts every byte it hands out (`LuaUi::allocated_bytes`; `core::frame_sections::add_bytes`). Nearly all of it is garbage the collector takes later, so it shows which windows make the collector's work.
- Short strings (40 bytes or less) are interned by Lua and cost nothing when the same text comes again. Tables, closures and long strings made in a draw function each frame are what the column counts.
- The table's history reuses its buffers, so keeping it costs no allocation a frame while the window is closed.
- The Actors row counts the instances not posed this frame (in neither the view nor a shadow's reach).

**GPU passes.** The "GPU passes" table shows the GPU's time per pass, averaged the same way, with the total.

- The passes are the sun's cascades, the cover and water maps, the light cubes, the sky, the zone, the objects, the characters, the translucent pass, the AO, the bloom, the post pass, the UI and frame generation.
- Each pass is timed from the timestamp `VkContext::gpu_mark` writes after it to the one before it (`render/gpu_timer.h`).
- With `display.parallel_record`, the scene's rendering holds only the secondary buffers' execution and takes no timestamps between them. The scene's passes then show as two rows, "scene opaque (worker-recorded)" and "scene (worker-recorded)". `display.parallel_record 0` gives the per-pass rows again.
- The timestamps are written only while the window is open. They are read back a frame or two late, when that frame's fence has been waited for, so nothing waits on the GPU.
- The zone viewer's `--gpu-timing` (with `--frames`) logs each pass's mean over the timed frames as `gpu pass: ...`.

#### Lua garbage collection

The Lua UI's garbage collector runs in Lua's generational mode. Once the scripts have loaded, `LuaUi::frame`'s first collection switches it over and makes the scripts' state the old generation.

- **Minor collections**: the Modern UI's windows make a few hundred KB of garbage a frame, nearly all of it dead by the next frame. The minor collections (each 20 % the heap grows) take it as the allocations run.
- **Major collections** are the host's. `LuaUi::frame`, between the windows' draws, collects in full (a whole mark and sweep at once) when the heap reaches three times what was live after the last one (`kGcMajorTrigger`). This is timed as the "Lua GC major" frame section and logged when it takes 8 ms or more.
- Lua 5.5's own shift to major collections would run inside the windows' draws, untimed. It is pushed to about eleven times the live heap (`LUA_GCPMINORMAJOR` 1000 %). Should Lua shift anyway (major mode, lgc.c `checkminormajor`), the next frame collects and puts it back in minor mode.
- "What was live" is the bytes the last major collection marked (Lua's `GCmajorminor`), read in `src/ui/lua/lua_gc_live.cpp`, the one file that includes Lua's internals.

Before this, the incremental collector was stopped between 1 ms slices at the end of each frame, and its catch-up finished a cycle in one frame when the slices fell behind (pauses of up to 40 ms).

#### Settings

- Graphics settings (`display.msaa`, `display.tonemap`, `display.exposure`) are in `docs/graphics.md`.
- `ui.cpp_windows=1` in `hailstone.ini` keeps the C++ windows on next to the Lua ones.
- In `ui/settings/<name>.txt` (in the folder of `hailstone.ini`), `enabled.<window>=0` turns a script window off, and `test.pos.<screen>=x,y` pins a window's position (for screenshots).

#### Testing aids

- `test.tab.<window>.<tab box id>=n` in `ui/settings/<name>.txt` opens page n once (the modern pack's `W.Tabs`).
- `--auto-keys` takes these next to key commands:
  - `UI:<window>:<child>`: a click on a game window's child;
  - `LIST:<window>:<list>:<row>[:<column>]`;
  - `DLIST:<window>:<list>:<row>`: a double click;
  - `COMBO:<window>:<child>:<index>`: a combo box's choice (a list cell's box is `<list>:<row>`);
  - `MENU:<window>:<x>:<y>`: a right click on a window, its context menu;
  - `MENUROW:<menu>:<row>` (0-based).
- `HAILSTONE_AUTO_DOOR_ID=n` makes `--auto-door` walk to door n, and `HAILSTONE_AUTO_DOOR_SECONDS` lengthens its walk.
- `GameConnection::receive_zone_packet_for_test` feeds packets to windows in tests (`tests/ui_windows_test.cpp`).
- The frame count of `--frames` includes the login and loading frames and runs below the cap there. Time a click late in the run (or log `ui.GetTime()`) when a screenshot must show a dialog that closes itself.

#### Checking

Log in to the local test server (`--host 127.0.0.1 --port 5997 --user ... --character NAME --frames 900 --screenshot FILE`, with `XDG_CONFIG_HOME` set to a scratch directory; a fresh one for first-login layouts) and look for "ui script error" in the log.

- `--auto-keys INVENTORY,TOGGLE_OPTIONSWIN,...` presses window keys.
- `--show-items N` opens the inventory and bags (1), picks up the first item (2, shift-pickup 8), shows the primary weapon's item display (4), opens the main menu (16) or a spell display (32).
