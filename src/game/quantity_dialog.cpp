#include "game/quantity_dialog.h"

namespace hailstone::game {

namespace {
using K = QuantityCall::Kind;
std::int32_t sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
} // namespace

QuantityResult quantity_dialog_open(const QuantityOpen& in, std::int32_t quantity_before) {
    QuantityResult r;
    r.quantity = quantity_before;
    auto call = [&](K k, std::int32_t a = 0, std::int32_t b = 0) { r.calls.push_back({k, a, b}); };
    if (in.max == in.min) {
        call(K::Close);
        r.closed = true;
        return r;
    }
    std::int32_t q = in.min;
    if (sub(in.max, in.min) < 2) q = in.max;
    if (in.use_keys) {
        if (in.shift) q = in.max;
        else if (in.ctrl) q = 1;
    }
    r.quantity = q;
    call(K::SliderTicks, add(sub(in.max, in.min), 1));
    std::int32_t slider = sub(q, in.min);
    call(K::SliderValue, slider);
    call(K::SetTitle, in.has_title ? 0 : 1);
    if (q > in.min) {
        // sub_00725ab0: the slider's value + min, the dialog closes, the owner is told.
        call(K::SliderGetValue, slider);
        r.quantity = quantity_dialog_accept(in.min, slider);
        call(K::Close);
        call(K::OwnerNotified, 0x24, r.quantity);
        r.accepted = true;
        return r;
    }
    slider = sub(in.start, in.min);
    call(K::SliderValue, slider);
    call(K::UpdateText);
    std::int32_t x = in.x, y = in.y;
    if (x > -1 && y > -1) {
        const auto* rc = reinterpret_cast<const std::uint32_t*>(in.rect);
        if (static_cast<std::int32_t>(rc[2] - rc[0] + static_cast<std::uint32_t>(x)) > in.screen_width)
            x = static_cast<std::int32_t>(rc[0] - rc[2] + static_cast<std::uint32_t>(in.screen_width));
        if (static_cast<std::int32_t>(rc[3] - rc[1] + static_cast<std::uint32_t>(y)) > in.screen_height)
            y = static_cast<std::int32_t>(rc[1] - rc[3] + static_cast<std::uint32_t>(in.screen_height));
        call(K::Move, x, y);
        r.position = std::pair{x, y};
    } else {
        call(K::Center);
    }
    call(K::FocusEdit);
    call(K::Show);
    r.shown = true;
    r.slider = slider;
    return r;
}

QuantityLimits merchant_buy_limits(std::int32_t merchant_quantity, std::int32_t stack_size, bool spell_scroll) {
    std::int32_t max = stack_size < 1 ? 1 : stack_size;
    if (merchant_quantity > 0 && merchant_quantity < max) max = merchant_quantity;
    return {max, spell_scroll ? 1 : max};
}

} // namespace hailstone::game
