#pragma once

// The pet window's command buttons as hot buttons (CPetInfoWnd, rof2decomp re/src/game/ui/pet_info_notification_eh.cpp
// and the CPetInfoWnd_* helpers in export/_globals.c):
//   - a drag from a command button (WndNotification message 0x17, unless the window is locked, +0x2d) puts on the
//     cursor (CCursorAttachment::AttachToCursor(0, 0, 10, index, "%s %s" of eqstr 6700 "Pet" and the button's
//     text, -1)) the button's index among Pet0_Button..Pet13_Button (+0x234..);
//   - dropped on a hot button (FUN_00552e00, the attachment type 10 → hot button type 8 by FUN_0054ee90) it becomes
//     a type 8 button whose value is that index and whose label is the attachment's text (63 characters, no icon);
//     a toggle command's makes the hot button a check button (+0x258);
//   - pressed (DoHotButton type 8, game/hot_button_press.h) the pet window's button of that index is clicked: the
//     command the button holds then goes to /pet (only while there is a pet);
//   - every frame (CPetInfoWnd_RefreshPetState → CPetInfoWnd_SyncHotButtonStates @0x70fa00) each command button
//     that holds a command checks the hot buttons labelled "Pet <its text>" (SetHotButtonsCheckedByLabel @0x70eb40,
//     stricmp) while it is a toggle command that is on, and unchecks them otherwise.
//
// Which command each button holds (PetButtonLayout): the right button down on a command button (CButtonWnd vf18
// sends XWM_RSELITEM_DOWN 0x2d, a disabled button too, as the constructor sets their bAction) pops up the command
// menu (+0x8f8) at the mouse; its rows are the commands CharacterZoneClient_CanUsePetCommand allowed while there was a
// pet (can_use_pet_command), then "none"; the row chosen goes to that button (WndNotification 0x14,
// CPetInfoWnd_AssignCommandToButton) and the layout is saved as the UI ini's PetButtonState.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "game/hotbuttons.h"

namespace hailstone::game {

inline constexpr std::uint32_t kStrPet = 6700; // "Pet" (string 0x1a2c)
inline constexpr int kPetWindowButtons = 14;   // Pet0_Button .. Pet13_Button
inline constexpr int kPetCommandCount = 18;    // the command names at 0xb3a180
inline constexpr int kPetCommandNone = 0x13;   // PetCommandNameToIndex's "not found", the menu's "none" row

// The command names (0xb3a180), a command index's: health, leader, attack, qattack, follow, guard, sit, stop,
// regroup, taunt, hold, ghold, spellhold, focus, feign, back, leave, target. A button's text is one of these.
inline constexpr std::array<std::string_view, kPetCommandCount> kPetCommandNames = {
    "health", "leader", "attack",    "qattack", "follow", "guard", "sit",  "stop", "regroup",
    "taunt",  "hold",   "ghold",     "spellhold", "focus", "feign", "back", "leave", "target"};

// What a click on the button holding command `index` gives cmd_pet (WndNotification message 1: the eqstr of the
// table at 0x710800's +0x26c loop, 7512 "report health" .. 13103 "target"), in the words parse_pet_command reads;
// "" outside 0..17.
std::string_view pet_command_words(int index);
// PetButtonIndexToPetCommand @0x70f1c0: a command index to the pet command (EQEmu PET_*): 0..6 as they are, stop 9,
// regroup 0x1f, taunt 0xc, hold 0xf, ghold 0x12, spellhold 0x15, focus 0x18, feign 0x1b, back 0x1c, leave 0x1d,
// target 0x1e; others 0x22.
std::uint32_t pet_command_of_index(int index);
// PetCommandNameToIndex @0x70e540 (strcmp): the command index of a name, kPetCommandNone when none.
int pet_command_index(std::string_view name);
// The OP_PetCommandState index (EQEmu PET_BUTTON_*) whose state checks a toggle command index's button
// (CPetInfoWnd_GetButtonToggleState @0x70f400: the toggle bytes +0x913..+0x91c), -1 for the others.
int pet_command_toggle_state(int index);

// IsTogglePetCommandName @0x70e670 (strcmp): follow, guard, sit, stop, regroup, taunt, hold, ghold, spellhold,
// focus: the commands that stay on.
bool is_toggle_pet_command(std::string_view name);

// What CharacterZoneClient_CanUsePetCommand @0x4438e0 reads (rof2decomp re/src/game/character/pet_commands.cpp,
// checked against the disassembly): the pet spawn (CharacterZoneClient_GetPetSpawn), its race (virtual +0x88),
// bSummoned (+0x2e0, the spawn bitfield's bit 2), its property 0x1f (PlayerBase::HasProperty(0x1f, 0, 0)), and the
// character's TotalEffect(spa, 1, sub, 1, 1).
struct PetCommandQuery {
    bool has_pet = false;
    std::uint32_t race = 0;
    bool summoned = false;
    bool property_1f = false;
    std::function<std::int32_t(std::int32_t spa, std::int32_t sub)> effect;
};
inline constexpr std::int32_t kSpaPetCommand = 0x10b;  // 267: allows the command its sub names (EQEmu PetDiscipline2)
inline constexpr std::int32_t kSpaFeignedMinion = 0x119; // 281: feign for a summoned pet
inline constexpr std::int32_t kSpaFeignDeath = 0x4a;     // 74: feign for a race 127 summoned pet
// CanUsePetCommand: false without a pet. A summoned race 0x7f (127) pet: leave (0x1d) always, feign (0x1b) with SPA
// 74, the rest of 0..0x21 with SPA 267 for the command. Another with property 0x1f: only leave. Otherwise 0..0xb,
// back off 0x1c and 0x1e always; taunt 0xc..0xe and leave when summoned; spellhold / focus (0x15..0x1a) summoned
// and SPA 267 for the command; feign summoned and SPA 281; hold, ghold (0xf..0x14) and regroup (0x1f..0x21) SPA
// 267 for the command. Commands past 0x21 never.
bool can_use_pet_command(const PetCommandQuery& q, std::uint32_t command);

// The window's 14 command buttons: their texts, the command map (+0x26c: command index → the button holding it) and
// their enabled state.
class PetButtonLayout {
public:
    // The constructor: the texts from the table at 0xb3a210 (attack, follow, guard, taunt, sit, stop, regroup, back,
    // leave, then five "none"; the "none" buttons keep their XML text, "none"), the map from it
    // (CPetInfoWnd_BuildCommandButtonMap), every button disabled, no command usable yet.
    PetButtonLayout();

    const std::string& text(int button) const { return text_[static_cast<std::size_t>(button)]; }
    bool enabled(int button) const { return enabled_[static_cast<std::size_t>(button)]; }
    // The command index whose map entry is the button (the click's +0x26c lookup), -1 for none.
    int command_of(int button) const;
    // The button holding command index `index` (+0x26c[index]), -1 for none.
    int button_of(int index) const;
    // CPetInfoWnd_FindButtonByText @0x70e3f0 (stricmp; "none" finds nothing): the first button with the text, -1.
    int find(std::string_view text) const;
    // CPetInfoWnd_AssignCommandToButton @0x70f8f0: the button's old command leaves the map, the new one maps to it;
    // another button holding the text becomes "none" and disabled. The button's text is the caller's to set.
    void assign(int button, std::string_view command);
    // WndNotification 0x14 from the command menu: the menu row `item` (1..18: kPetCommandNames[item - 1]; 0x13
    // "none") goes to the button (+0x8fc): assign, its text, disabled when "none". The caller saves.
    void choose(int button, int item);
    // CPetInfoWnd_LoadPetButtonState @0x70fbb0: the UI ini's PetButtonState split at spaces (strtok), one word per
    // button in order (assign, then the text); false for an empty value (the constructor then saves the defaults).
    bool load(std::string_view value);
    // CPetInfoWnd_SavePetButtonState @0x70ec40: each button's text and a space.
    std::string save() const;
    // CPetInfoWnd_UpdateToggleButtonChecks @0x70fd20, run only while there is a pet: each command index's
    // CanUsePetCommand (bPetButtonEnable, +0x900..).
    void set_usable(const std::array<bool, kPetCommandCount>& usable) { usable_ = usable; }
    bool usable(int index) const { return usable_[static_cast<std::size_t>(index)]; }
    // The command menu as RefreshPetState rebuilds it: the usable commands as rows 1..18 in order, then "none" (0x13).
    struct MenuRow {
        int item = 0;
        std::string_view name;
    };
    std::vector<MenuRow> menu() const;
    // SyncHotButtonStates' SetEnabled: every button the map holds whose text is not "none" is enabled.
    void sync_enabled();

private:
    std::array<std::string, kPetWindowButtons> text_;
    std::array<int, kPetCommandCount> map_{};
    std::array<bool, kPetWindowButtons> enabled_{};
    std::array<bool, kPetCommandCount> usable_{};
};

// The window's toggle bytes (+0x913..+0x91c), by EQEmu PET_BUTTON_* (Handle_OP_PetCommandState's index: 0 sit, 1 stop,
// 2 regroup, 3 follow, 4 guard, 5 taunt, 6 hold, 7 ghold, 8 focus, 9 spellhold). Three writers: the constructor
// (CPetInfoWnd_LoadButtonToggleStates @0x70ed20: the UI ini's PetButtonToggleState0..9, ReadUIINIBool default 0),
// cmd_pet's toggle words (Set / FlipButtonToggleState @0x70f4a0 / @0x70f5a0, each saving its key at once through
// SaveButtonToggleState @0x70f090) and the server's OP_PetCommandState (not saved). The buttons' checks, the hot
// buttons' and ReissueToggledPetCommands read them.
inline constexpr int kPetToggles = 10;
using PetToggleStates = std::array<bool, kPetToggles>;
// The UI ini key (in the window's section) of toggle command index 4..13: "PetButtonToggleState" and index - 4, so 0
// follow, 1 guard, 2 sit, 3 stop, 4 regroup, 5 taunt, 6 hold, 7 ghold, 8 spellhold, 9 focus; "" for the others.
std::string pet_toggle_ini_key(int index);
// CPetInfoWnd_GetButtonToggleState @0x70f400: command index 4..13's byte; false for the others.
bool pet_toggle(const PetToggleStates& states, int index);
// SetButtonToggleState's store (nothing outside 4..13); the caller saves the key.
void set_pet_toggle(PetToggleStates& states, int index, bool on);
// CPetInfoWnd_GetPetCommandAvailable @0x70ea00: a pet command (EQEmu PET_*) by the bytes UpdateToggleButtonChecks
// last stored (+0x900.., PetButtonLayout::usable, all false until there was a pet): 0..5 their own, the sit, stop,
// taunt, hold, ghold, spellhold, focus and regroup families (and their on / off) their command's, feign, back off,
// leave and target theirs; 0x22 never; anything else always.
bool pet_command_available(const PetButtonLayout& layout, std::uint32_t command);
// CPetInfoWnd_ReissueToggledPetCommands @0x70f6f0, run by RefreshPetState when the player gets a pet (PetID set and
// its spawn there, after none): for command indexes 4..13 in order, the pet command (PetButtonIndexToPetCommand:
// follow 4, guard 5, sit 6, stop 9, regroup 0x1f, taunt 0xc, hold 0xf, ghold 0x12, spellhold 0x15, focus 0x18,
// the toggle words' commands) of each whose toggle byte is on and which `usable` allows (CanUsePetCommand and
// GetPetCommandAvailable, which UpdateToggleButtonChecks has just set from it); the caller issues each with the
// pet's spawn id as target and the flag byte 1 (IssuePetCommand(command, pet id, 1)).
std::vector<std::uint32_t> pet_commands_to_reissue(const PetToggleStates& states,
                                                   const std::function<bool(std::uint32_t command)>& usable);

// The hot button a drag from command button `index` makes (`pet_word`: eqstr 6700); nullopt for an index outside
// the window's buttons.
std::optional<HotButton> pet_command_hot_button(int index, std::string_view pet_word, std::string_view text);

// A command button as SyncHotButtonStates reads it: its text, and whether it is checked.
struct PetButtonFace {
    std::string_view text;
    bool checked = false;
};
// Whether a hot button labelled `label` is checked by the pet window's buttons: nullopt when no button holding a
// command ("none" holds none) has the label "<pet_word> <text>"; otherwise checked while that button's command is
// a toggle (+0x258) and the button is checked. The last match wins, as each button's sync overwrites the last.
std::optional<bool> pet_hot_button_checked(std::string_view label, std::string_view pet_word,
                                           std::span<const PetButtonFace> buttons);

} // namespace hailstone::game
