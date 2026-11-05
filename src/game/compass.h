#pragma once

// The compass's heading error (CCompassWnd @0x65b5f0..0x65c100). Below Sense Heading 200 the compass does not show
// the true heading: it shows the heading plus an error that drifts toward a random target and, on reaching it, picks
// another. A successful use of Sense Heading (UseSkill 40) puts the error back to 0 and holds it there for 5 s.
//
//  - PickTarget @0x65bac0 (also run when the window is shown): skill 200 and up, target 0; else
//    range = (200 - skill) * 512 / 200, target = rand() % range - range / 2 (C division), pushed out to at least
//    51 - skill / 5 either way (0 counts as positive). Without a character the skill counts as 1.
//  - UpdateRate @0x65b5f0: 80 heading units a second, 160 while the target is more than 50 away or the hold runs;
//    negative while the error is above the target.
//  - SenseHeading reset @0x65b650: error 0, hold 5000 ms.
//  - OnProcessFrame @0x65bb90, while the window is shown and a character is in: with the hold run out and the skill
//    under 200, step = trunc((now - last) * rate * 0.001); |target - error| < step: the error becomes the target, a
//    new target is picked; else the error moves by step and wraps past +-256 by 512; the rate is updated either
//    way. At 200 the error is 0. The strips are placed for (trunc(heading) + error) % 512 (C remainder). Every frame
//    (shown or not) the hold counts down by the time since the last frame. The step is truncated per frame, so at a
//    high frame rate (under 13 ms a frame at 80 a second) the error stands still, as the original's does.
//
// The sense heading message itself reads the true heading (UseSkill's 0x45c85f), not the compass's.

#include <cstdint>
#include <functional>

namespace nmsc::game {

class CompassError {
public:
    // rand(): 0..0x7fff (the C runtime's).
    using Rand = std::function<std::int32_t()>;

    // CCompassWnd::OnProcessFrame for this frame at `now_ms`; `skill` is the base Sense Heading skill, -1 without a
    // character (the window then does nothing but count the hold down). A window that was hidden the frame before
    // picks a target first (AboutToShow, vtable +0xdc = 0x65bb80).
    void process(std::uint32_t now_ms, bool shown, std::int32_t skill, const Rand& rand);
    // A successful Sense Heading (FUN_0065b650).
    void sense_heading() {
        error_ = 0;
        hold_ = 5000;
    }
    // The heading the compass shows for the true heading `heading` (0..512): (trunc(heading) + error) % 512, which
    // may be negative as the original's is.
    std::int32_t heading(float heading) const;

    std::int32_t error() const { return error_; }
    std::int32_t target() const { return target_; }
    double rate() const { return rate_; }
    std::int32_t hold() const { return hold_; }

private:
    void pick_target(std::int32_t skill, const Rand& rand);
    void update_rate();

    std::int32_t target_ = 0; // +0x22c
    std::int32_t error_ = 0;  // +0x230
    std::uint32_t last_ = 0;  // +0x234
    double rate_ = 0.0;       // +0x238
    std::int32_t hold_ = 0;   // +0x240
    bool was_shown_ = false;
};

} // namespace nmsc::game
