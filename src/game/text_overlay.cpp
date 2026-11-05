#include "game/text_overlay.h"

#include <utility>

namespace nmsc::game {

std::uint32_t text_overlay_rgb(std::int32_t colour) {
    switch (colour) {
    case 2: return 0x008000;
    case 4: return 0x000080;
    case 5: return 0xf000f0;
    case 6: return 0x808080;
    case 7: return 0xe0e0e0;
    case 10: return 0xf0f0f0;
    case 12: return 0xa0a0a0;
    case 13: return 0xf00000;
    case 14: return 0x00f000;
    case 15: return 0xf0f000;
    case 16: return 0x0000f0;
    case 18: return 0x00f0f0;
    case 20: return 0x000000;
    case 21: return 0xf0a000;
    case 22: return 0x806020;
    default: return 0x606060;
    }
}

void TextOverlay::display(std::string text, std::int32_t colour, std::int32_t priority, std::int32_t alpha,
                          std::uint32_t fade_in_ms, std::uint32_t fade_out_ms, std::uint32_t hold_ms,
                          std::uint32_t now_ms) {
    if (active_ && priority < priority_) return;
    text_ = std::move(text);
    active_ = true;
    fading_in_ = true;
    fading_out_ = false;
    start_ = now_ms;
    fade_in_ = fade_in_ms;
    fade_out_ = fade_out_ms;
    hold_ = hold_ms;
    priority_ = priority;
    alpha_ = alpha;
    color_ = text_overlay_rgb(colour);
}

namespace {
// trunc(elapsed / span x alpha), the x87's chop of fild / fdivp / fimul.
std::int32_t part(std::uint32_t elapsed, std::uint32_t span, std::int32_t alpha) {
    double v = static_cast<double>(elapsed) / static_cast<double>(span) * static_cast<double>(alpha);
    return static_cast<std::int32_t>(v);
}
} // namespace

std::optional<TextOverlay::Frame> TextOverlay::frame(std::uint32_t now_ms) {
    if (!active_) return std::nullopt;
    if (hold_ != 0 && now_ms - start_ > hold_) {
        fade_start_ = now_ms;
        fading_out_ = true;
        hold_ = 0;
    }
    // The colour's alpha byte, at most the alpha (all of it once the top byte is 0xff).
    std::int32_t a = static_cast<std::int8_t>(color_ >> 24);
    if ((color_ & 0xff000000u) == 0xff000000u || static_cast<std::uint32_t>(a) > static_cast<std::uint32_t>(alpha_))
        a = alpha_;
    if (fading_out_) {
        std::uint32_t elapsed = now_ms - fade_start_;
        if (fade_out_ <= elapsed) {
            active_ = fading_out_ = fading_in_ = false;
            return std::nullopt;
        }
        a = alpha_ - part(elapsed, fade_out_, alpha_);
    } else if (fading_in_) {
        std::uint32_t elapsed = now_ms - start_;
        if (fade_in_ <= elapsed) {
            a = alpha_;
            fading_in_ = false;
        } else {
            a = part(elapsed, fade_in_, alpha_);
        }
    }
    color_ = (color_ & 0xffffffu) | (static_cast<std::uint32_t>(a) << 24);
    return Frame{text_, color_};
}

bool task_slot_valid(std::uint32_t type, std::uint32_t index) {
    switch (type) {
    case 0: case 1: return index == 0;
    case 2: case 3: return index <= 0x1c;
    default: return false;
    }
}

TaskCompleteText task_complete_text(bool stage_complete, bool completed) {
    if (stage_complete) return TaskCompleteText{6005, 15, 0, false};
    return completed ? TaskCompleteText{6003, 14, 1, true} : TaskCompleteText{6004, 13, 1, true};
}

} // namespace nmsc::game
