#pragma once

// The quantity dialog (CQuantityWnd): follows the original's
// CQuantityWnd::Open @0x725b00 and its accept (sub_00725ab0), checked against
// CQuantityWnd_Open.jsonl; and the callers' limits (HandleBuy, HandleSell,
// answered from their disassembly).
//  - min == max closes the dialog and does nothing else.
//  - The quantity starts at min, or max when max - min < 2; with use_keys,
//    Shift gives max, else Ctrl gives 1.
//  - A quantity above min is accepted at once (the owner is told, nothing
//    shows). Otherwise the slider starts at `start` and the dialog shows at
//    (x, y) kept on screen, or centred when x or y is below 0.
//  - Buying (CMerchantWnd::HandleBuy): max = the merchant's quantity capped
//    at the stack size (or the stack size when unlimited); start 1 for a
//    scroll (item type 20) with a spell, else max.
//  - Selling (HandleSell): max = start = the stack count.
//
// For the UI: call quantity_dialog_open() with the caller's limits and the
// keyboard state; when `accepted` is set, use `quantity` at once and show
// nothing; when `shown`, show the slider at `slider` (min..max) at
// (`x`, `y`) or centred, and on OK call quantity_dialog_accept() with the
// slider's value.

#include <cstdint>
#include <optional>
#include <vector>

namespace hailstone::game {

struct QuantityOpen {
    std::int32_t min = 0, max = 0, start = 0;
    std::int32_t x = -1, y = -1;         // where to show it; -1: centred
    bool use_keys = true;                // Shift / Ctrl pick max / 1
    bool shift = false, ctrl = false;    // CXWndManager::GetKeyboardFlags bits 0 / 1
    bool has_title = false;              // `title` given (else the default title)
    std::int32_t screen_width = 1024, screen_height = 768;
    std::int32_t rect[4] = {0, 0, 0, 0}; // the dialog's relative rect (left, top, right, bottom)
};

struct QuantityCall {
    enum class Kind : std::uint8_t {
        Close, SliderTicks, SliderValue, SetTitle, SliderGetValue, OwnerNotified, UpdateText, Move, Center, FocusEdit,
        Show,
    };
    Kind kind{};
    std::int32_t a = 0, b = 0;
};

struct QuantityResult {
    std::int32_t quantity = 0;     // CQuantityWnd +0x224 (unchanged when min == max)
    bool closed = false;           // min == max: closed, nothing else
    bool accepted = false;         // accepted at once: `quantity` goes to the owner
    bool shown = false;            // the dialog shows
    std::int32_t slider = 0;       // the slider's value (quantity - min) when shown
    std::optional<std::pair<std::int32_t, std::int32_t>> position; // where it shows; none: centred
    std::vector<QuantityCall> calls;
};

// CQuantityWnd::Open; `quantity_before` is the dialog's old +0x224.
QuantityResult quantity_dialog_open(const QuantityOpen& in, std::int32_t quantity_before = 0);
// sub_00725ab0: the quantity a slider value gives.
inline std::int32_t quantity_dialog_accept(std::int32_t min, std::int32_t slider) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(slider) + static_cast<std::uint32_t>(min));
}

// CMerchantWnd::HandleBuy / HandleSell's limits (min 0).
struct QuantityLimits {
    std::int32_t max = 0, start = 0;
};
// `merchant_quantity` -1 (or 0) for unlimited stock.
QuantityLimits merchant_buy_limits(std::int32_t merchant_quantity, std::int32_t stack_size, bool spell_scroll);
inline QuantityLimits merchant_sell_limits(std::int32_t stack_count) { return {stack_count, stack_count}; }

} // namespace hailstone::game
