#pragma once

// What a mouse click does in the isometric camera (game/iso_camera.h): Hailstone's click to move, an addition the
// original does not have (docs/isometric.md). It only chooses between the original's click and a walk over the
// navigation mesh (app/navigator.h) followed by that same click; it never targets, attacks or casts on its own.
// Combat stays the player's: auto attack is turned on with its key or /attack, as in the other cameras.
//
//  - Left on the ground: walk there.
//  - Left on a door, a ground item or a ladder out of reach: walk to it, then click it.
//  - Right on an NPC or a corpse out of reach: walk to it, then right click it (a merchant's window, a corpse's loot,
//    a banker; a hostile NPC is only walked to).
//  - Everything else is the original's click: a spawn is targeted, an item on the cursor is dropped or placed, a
//    player's right click opens their menu, placing an item confirms it.

#include <cstdint>

namespace hailstone::game {

enum class IsoClickTarget : std::uint8_t {
    Nothing, // no spawn or object, and no ground under the cursor (the sky)
    Ground,  // no spawn or object, the zone's surface under the cursor
    Spawn,
    Door,
    Object,  // a ground item or a tradeskill container
    Ladder,
    Placed,  // a real estate item
};

struct IsoClickInput {
    bool right = false;          // the right button (else the left)
    IsoClickTarget target = IsoClickTarget::Nothing;
    bool placing = false;        // item placement mode is on
    bool cursor_item = false;    // an item is on the cursor
    bool in_reach = false;       // already close enough for the original's click to work
    bool walk_to_spawn = false;  // right on a spawn: it is an NPC or a corpse (a player's right click is their menu)
};

enum class IsoClickAction : std::uint8_t {
    Original,    // the original's click, as in the other cameras
    WalkTo,      // walk to the ground point
    WalkThenUse, // walk to the target, then the original's click on it
};

IsoClickAction iso_click_action(const IsoClickInput& in);

} // namespace hailstone::game
