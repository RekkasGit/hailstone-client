// What a click does in the isometric camera (game/click_intent.h).
#include <doctest/doctest.h>

#include "game/click_intent.h"

using namespace hailstone::game;

namespace {
IsoClickAction act(bool right, IsoClickTarget t, bool in_reach = false, bool cursor = false, bool placing = false,
                   bool walk_to_spawn = false) {
    IsoClickInput in;
    in.right = right;
    in.target = t;
    in.in_reach = in_reach;
    in.cursor_item = cursor;
    in.placing = placing;
    in.walk_to_spawn = walk_to_spawn;
    return iso_click_action(in);
}
} // namespace

TEST_CASE("iso click: the left button walks to the ground and to things out of reach") {
    CHECK(act(false, IsoClickTarget::Ground) == IsoClickAction::WalkTo);
    CHECK(act(false, IsoClickTarget::Nothing) == IsoClickAction::Original);
    for (IsoClickTarget t : {IsoClickTarget::Door, IsoClickTarget::Object, IsoClickTarget::Ladder}) {
        CHECK(act(false, t) == IsoClickAction::WalkThenUse);
        CHECK(act(false, t, true) == IsoClickAction::Original);
    }
    CHECK(act(false, IsoClickTarget::Placed) == IsoClickAction::Original);
}

TEST_CASE("iso click: a spawn is only targeted, never attacked") {
    // The left click on any spawn is the original's (targeting), near or far: no walk, no attack.
    CHECK(act(false, IsoClickTarget::Spawn) == IsoClickAction::Original);
    CHECK(act(false, IsoClickTarget::Spawn, true) == IsoClickAction::Original);
}

TEST_CASE("iso click: the right button walks to an NPC or a corpse out of reach") {
    CHECK(act(true, IsoClickTarget::Spawn, false, false, false, true) == IsoClickAction::WalkThenUse);
    CHECK(act(true, IsoClickTarget::Spawn, true, false, false, true) == IsoClickAction::Original);
    // A player's right click opens their menu where they stand.
    CHECK(act(true, IsoClickTarget::Spawn, false, false, false, false) == IsoClickAction::Original);
    CHECK(act(true, IsoClickTarget::Ground) == IsoClickAction::Original);
    CHECK(act(true, IsoClickTarget::Door) == IsoClickAction::Original);
}

TEST_CASE("iso click: placing and the cursor's item keep the original's click") {
    CHECK(act(false, IsoClickTarget::Ground, false, false, true) == IsoClickAction::Original);
    CHECK(act(true, IsoClickTarget::Spawn, false, false, true, true) == IsoClickAction::Original);
    CHECK(act(false, IsoClickTarget::Ground, false, true) == IsoClickAction::Original);
    CHECK(act(false, IsoClickTarget::Door, false, true) == IsoClickAction::Original);
}
