#pragma once

// The large centre-screen text (CTextOverlay, CBroadcast::Get @0x41f3a0): one line at a time, shown with
// CTextOverlay::DisplayText @0x41f840 (text, colour, priority, alpha, fade-in ms, fade-out ms, hold ms) and faded by
// its Draw @0x41f610. Read from the disassembly (rof2decomp has no rewrite):
//   - DisplayText (@0x41f430): while a text shows, a new one with a lower priority (signed) is ignored; otherwise it
//     replaces it: start = now, fading in, the colour index mapped to an RGB (table below), alpha 0;
//   - Draw: once the hold time (from the start) has passed, the fade-out starts; fading in, the alpha is
//     trunc(elapsed / fade_in x alpha) until the fade-in time, then alpha; fading out, alpha - trunc(elapsed /
//     fade_out x alpha), and the text ends when the fade-out time has passed;
//   - the text is drawn wrapped in the rectangle {x0 + W/4, y0 + 0.33 H, x0 + 3W/4, y1} of the screen (flags 0x10).
// OP_TaskActivityComplete (CTaskManager::HandleMessage @0x48253a) and OP_Marquee use it.

#include <cstdint>
#include <optional>
#include <string>

namespace nmsc::game {

// The colour index → 0xRRGGBB of DisplayText's switch (@0x41f4b9); other indices are 0x606060.
std::uint32_t text_overlay_rgb(std::int32_t colour);

class TextOverlay {
public:
    void display(std::string text, std::int32_t colour, std::int32_t priority, std::int32_t alpha,
                 std::uint32_t fade_in_ms, std::uint32_t fade_out_ms, std::uint32_t hold_ms, std::uint32_t now_ms);

    struct Frame {
        std::string text;
        std::uint32_t argb = 0; // alpha << 24 | rgb
    };
    // Draw's timing for this frame (advances the fades); nullopt when nothing shows.
    std::optional<Frame> frame(std::uint32_t now_ms);
    bool active() const { return active_; }
    void clear() { active_ = false; }

private:
    std::string text_;
    bool active_ = false, fading_out_ = false, fading_in_ = false; // +4, +5, +6
    std::uint32_t start_ = 0;       // +0x08
    std::uint32_t fade_in_ = 0;     // +0x0c
    std::uint32_t fade_start_ = 0;  // +0x10
    std::uint32_t fade_out_ = 0;    // +0x14
    std::uint32_t hold_ = 0;        // +0x18
    std::uint32_t color_ = 0;       // +0x1c (alpha in the top byte once faded in)
    std::int32_t priority_ = 0;     // +0x20
    std::int32_t alpha_ = 0;        // +0x24
};

// OP_TaskActivityComplete's text (CTaskManager::HandleMessage @0x48253a): a stage (stage_complete != 0) shows eqstr
// 6005 in colour 15 with priority 0; otherwise the task slot of `type` / `index` (the jump table at 0x4826f0: types
// 0 and 1 index 0, types 2 and 3 index 0..28) must hold `task_id`, and shows 6004 "failed" (colour 13) or 6003
// "completed" (colour 14) with the task's title, priority 1. All with alpha 255, 500 ms fades and a 3000 ms hold.
// Nothing goes to chat. An invalid type or index (or an empty slot) crashes the original (a null slot is read):
// dropped here (task_slot_valid), as is a different task id.
struct TaskCompleteText {
    std::uint32_t eqstr = 0;
    std::int32_t colour = 0;
    std::int32_t priority = 0;
    bool with_title = false;
};
bool task_slot_valid(std::uint32_t type, std::uint32_t index);
TaskCompleteText task_complete_text(bool stage_complete, bool completed);

inline constexpr std::int32_t kTaskTextAlpha = 0xff;
inline constexpr std::uint32_t kTaskTextFadeInMs = 500, kTaskTextFadeOutMs = 500, kTaskTextHoldMs = 3000;

} // namespace nmsc::game
