#include "game/chat_input.h"

namespace nmsc::game {

void set_chat_input_mode(ChatInputWorld& w, std::int32_t mode, std::vector<ChatInputCall>& calls) {
    using F = ChatInputCall::Fn;
    if (w.mode == mode) return;
    if (w.chat_manager) {
        calls.push_back({F::GetActiveChatWindow, w.active_chat_window, 0});
        if (w.active_chat_window) {
            if (mode == 1) {
                calls.push_back({F::InputClearFocus, false, 0});
                w.mode = mode;
                return;
            }
            calls.push_back({F::InputSetFocus, false, 0});
            std::uint32_t held = 0;
            if (w.forward != 0) held = 1;
            else if (w.backward != 0) held = 2;
            if (w.turn_left != 0) held |= 4;
            else if (w.turn_right != 0) held |= 8;
            if (w.held_extra != 0) held |= 0x10;
            if (held != 0) calls.push_back({F::ExecuteCmd, false, held});
        }
    }
    w.mode = mode;
}

void toggle_chat_input_mode(ChatInputWorld& w, std::vector<ChatInputCall>& calls) {
    using F = ChatInputCall::Fn;
    if (w.mode != 0) {
        set_chat_input_mode(w, 0, calls);
        return;
    }
    if (w.chat_manager) {
        calls.push_back({F::GetActiveChatWindow, w.active_chat_window, 0});
        if (w.active_chat_window) calls.push_back({F::InputClearFocus, false, 0});
    }
    w.mode = 1;
}

ChatKey chat_key(int slot) {
    using A = ChatKeyAction;
    switch (slot) {
    case 479:
    case 480: return {A::ClearChatBuffer, 0};
    case 481: return {A::InsertCharacter, ';'};
    case 482: return {A::InsertCharacter, '/'};
    case 499: return {A::InsertCharacter, '['};
    case 485: return {A::Exit, 0};
    case 488: return {A::ToggleChatFocus, 0};
    case 489: return {A::RecentreMouse, 0};
    case 492: return {A::PlayerCustomization, 0};
    case 493: return {A::CharSelectNext, 0};
    case 494: return {A::CharSelectPrevious, 0};
    case 495: return {A::CharSelectQuit, 0};
    case 496: return {A::EnterWorld, 0};
    case 497: return {A::SubmitNewName, 0};
    case 498: return {A::CreationEscape, 0};
    default: return {};
    }
}

} // namespace nmsc::game
