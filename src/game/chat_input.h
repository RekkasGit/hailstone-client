#pragma once

// The chat input mode and the fixed key slots 479..499: exact ports of rof2decomp batch 265
// (re/src/game/ui/chat_input_mode.cpp), checked against CEverQuest_SetChatInputMode.jsonl and
// CEverQuest_ToggleChatInputMode.jsonl.
//
// __InChatMode: 0 while the active chat window's input line has the keys,
// 1 while they go to the game; any other value counts as "not chat" for the Enter key. The slots 479..499 of
// __ExecuteCmd have no command names, are never loaded from or saved to the ini, and fire only when the focused
// window does not consume the key (so Enter typed in the chat input never reaches 488):
//   479 / 480 clear the chat buffer when not in chat mode;
//   481 ';', 482 '/', 499 '[' put that character into the active chat input and give it the focus (in chat mode);
//   485 exit; 488 Enter toggles the chat focus (ToggleChatInputMode); 489 Alt+Enter recentres the mouse;
//   492 the player customization window (eqstr 12377 for pre-Luclin models);
//   493 / 494 character select next / previous row, 495 quit character select, 496 enter the world;
//   497 submit the new character's name, 498 Escape on character creation (CCharacterCreation_Escape).

#include <cstdint>
#include <vector>

namespace hailstone::game {

// What SetChatInputMode / ToggleChatInputMode read.
struct ChatInputWorld {
    std::int32_t mode = 1;            // __InChatMode
    bool chat_manager = true;         // pinstCChatWindowManager
    bool active_chat_window = true;   // CChatWindowManager::GetActiveChatWindow non-null
    std::int32_t forward = 0, backward = 0, turn_left = 0, turn_right = 0; // __pulForward, ... (held movement)
    std::uint8_t held_extra = 0;      // 0xddf704 (the strafe state): adds 0x10 to the held mask
};

// The calls they make, in order.
struct ChatInputCall {
    enum class Fn : std::uint8_t { GetActiveChatWindow, InputSetFocus, InputClearFocus, ExecuteCmd } fn{};
    bool ret_nonnull = false;     // GetActiveChatWindow's answer
    std::uint32_t held_mask = 0;  // ExecuteCmd(0 = AUTORUN, key down 1, mask)
};

// 0x518230: sets the mode when it changes. With an active chat window, mode 1 takes the focus from
// its input line; any other mode gives it the focus and, with movement keys held, AUTORUN with the held mask
// (forward 1, else backward 2; turn left 4, else turn right 8; 0x10 while 0xddf704 is set) keeps the player moving.
void set_chat_input_mode(ChatInputWorld& w, std::int32_t mode, std::vector<ChatInputCall>& calls);
// 0x51ebf0 (slot 488, Enter): from mode 0 the input line loses the focus and the mode becomes 1
// (no AUTORUN); from any other mode set_chat_input_mode(0).
void toggle_chat_input_mode(ChatInputWorld& w, std::vector<ChatInputCall>& calls);

// The unnamed key slots.
inline constexpr int kChatKeyFirst = 479, kChatKeyLast = 499;
enum class ChatKeyAction : std::uint8_t {
    None,
    ClearChatBuffer,     // 479, 480 (outside chat mode)
    InsertCharacter,     // 481 ';', 482 '/', 499 '[' (in chat mode)
    Exit,                // 485
    ToggleChatFocus,     // 488
    RecentreMouse,       // 489
    PlayerCustomization, // 492 (12377 for pre-Luclin models)
    CharSelectNext,      // 493
    CharSelectPrevious,  // 494
    CharSelectQuit,      // 495
    EnterWorld,          // 496
    SubmitNewName,       // 497
    CreationEscape,      // 498
};
struct ChatKey {
    ChatKeyAction action = ChatKeyAction::None;
    char character = 0; // InsertCharacter
};
ChatKey chat_key(int slot);
inline constexpr std::uint32_t kStrPreLuclinCustomization = 12377;

} // namespace hailstone::game
