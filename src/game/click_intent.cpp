#include "game/click_intent.h"

namespace hailstone::game {

IsoClickAction iso_click_action(const IsoClickInput& in) {
    if (in.placing) return IsoClickAction::Original;
    if (in.right) {
        if (in.target == IsoClickTarget::Spawn && in.walk_to_spawn && !in.in_reach) return IsoClickAction::WalkThenUse;
        return IsoClickAction::Original;
    }
    // An item on the cursor keeps the original's left click: dropped on the ground, placed, or used as a key.
    if (in.cursor_item) return IsoClickAction::Original;
    switch (in.target) {
    case IsoClickTarget::Ground: return IsoClickAction::WalkTo;
    case IsoClickTarget::Door:
    case IsoClickTarget::Object:
    case IsoClickTarget::Ladder: return in.in_reach ? IsoClickAction::Original : IsoClickAction::WalkThenUse;
    case IsoClickTarget::Nothing:
    case IsoClickTarget::Spawn:
    case IsoClickTarget::Placed: break;
    }
    return IsoClickAction::Original;
}

} // namespace hailstone::game
