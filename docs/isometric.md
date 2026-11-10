# Isometric camera

Code: `src/game/iso_camera.{h,cpp}` (the camera and movement relative to it), `src/game/click_intent.{h,cpp}` (what a
click does), `src/game/iso_occlusion.{h,cpp}` and `reveal_cut` in `shaders/lighting.glsl` (seeing the player through
what is in front), `src/app/client_app_iso.cpp` (the clicks' walks and the keys), `ClientApp::update_iso_camera` and
`ClientApp::camera_command` (`src/app/client_app_input.cpp`), `Navigator::walk_to_click` (`src/app/navigator.cpp`).
Tests: `tests/iso_camera_test.cpp`, `tests/click_intent_test.cpp`, `tests/iso_occlusion_test.cpp`.

The original client has no such camera. This is a Hailstone addition, off unless the player chooses it: a view from
above and to one side, in the manner of isometric RPGs. It changes only where the scene is seen from. Movement,
facing, speed, collision and every game rule stay the original's, and the server sees ordinary movement.

## The camera

Camera type 7 (`game::kCameraIsometric`), Hailstone's own beside the camera behind the player (6). It looks down at a
fixed pitch (45 degrees by default) from a distance (160 units at the start), through a perspective lens (63 degrees
across by default, about the stock cameras' 63.3). It follows the player's body with a little easing (0.08 s) and
jumps with a teleport or a zone-in. It does not stop at scenery: what stands between it and the player is cut away
instead ("Seeing the player" below).

- The right button held turns the camera freely about the player, never the player. A short right click still
  clicks.
- The left button always clicks; it never starts a mouse look.
- The wheel zooms, a tenth of the distance a notch, from 60 to 320 units.
- TOGGLECAM's cycle counts it as first person (0 -> 1), as it counts camera 6; the first person camera's key leaves
  it.

To the movement controls it is the camera behind the player (6) with no mouse look, so neither button runs.

## Movement keys

With `camera.iso_wasd = camera` (the default) the movement keys point relative to the camera, as in isometric RPGs:
forward runs up the screen, back down it toward the camera, the strafe keys left and right across it, and two
together the diagonal between. The player turns at once to that way (as /follow and /nav set the heading) and the
controls see only the forward key held, so the speed, the run and walk, collision and the position updates are the
original's. The turn keys turn the camera (2 radians a second), not the player. Autorun runs the way the player
faces. At a boat's helm the keys sail the boat as in the other cameras.

The gamepad's left stick moves along its own angle, not eight ways; the right stick turns the camera (sideways) and
zooms (up and down).

With `camera.iso_wasd = classic` (`/camera wasd classic`) the keys move, turn and strafe as in the other cameras.

## Clicks

A click either does what it does in the other cameras or walks first, over the navigation mesh
(`docs/navigation.md`), and then does that. It never targets, attacks or casts on its own: auto attack is turned on
with its key or `/attack`, as everywhere (`game::iso_click_action`).

| Click on | Does |
|---|---|
| the ground, left | walks there; held, the walk follows the cursor |
| a door, a ground item or a ladder out of reach, left | walks to it, then clicks it |
| an NPC or a corpse out of reach, right | walks to it (inside the melee range), turns to it, then right clicks it: a merchant's or banker's window, a corpse's loot; a hostile NPC is only walked to |
| any spawn, left | targets it, near or far, as in the other cameras |
| a player, right | their menu, where they stand |
| anything, with an item on the cursor or while placing one | the other cameras' click (drop, place, a key on a door) |

The walk is the navigator's, so it goes round walls, opens doors on the way and gets itself unstuck. Any movement key
ends it, without a word in the chat (each click would say so). A walk that cannot be started says why ("Cannot walk
there: ..."). A click that walks to the spot says nothing when the mesh goes only part of the way, and goes straight
at the spot in a zone without a mesh. The click at the end is made only when the walk ended near enough; a walk
stopped short (a key, no path, stuck) clicks nothing.

## Measured from the player

The camera stands well back from the player, so what the other cameras measure from the camera is measured from the
player here:

- **Fog and far plane** (`game::iso_view`): the fog's start and end and the far plane are pushed out by the camera's
  distance, so the fog starts at the player where it would in first person and as much of the zone shows round them.
- **The region** the fog and the underwater tint read is the player's eye's, not the air's or the hillside's the
  camera hangs in.
- **Shadow cascades** (`game::iso_shadow_near`) start 40 units short of the player rather than at the lens, and reach
  that much further, so the sharpest cascade covers the player.
- **The sound listener** is the player's in every camera already.

A click that walks to a spot or an item puts a gold ring on the ground there (drawn as the target ring is, a decal on
the scene's depth): it lands wide and closes in with a flash, spins slowly while the walk goes on, follows the held
button's aim, and fades when the walk ends. A walk to a spawn or a door shows none (the target ring, or the door
itself, is there).

## Seeing the player

What stands between the camera and the player is cut away round them, so they stay in sight behind walls, trees,
hills, roofs and the floors above (`game::iso_reveal`, `shaders/lighting.glsl` `reveal_discard`). A surface is cut
where it is

- nearer the camera than the player's middle by at least 3 units, fully from 9,
- inside a cone round the line from the camera to the player, which shows as a circle on the screen: 20 units in
  radius at the player at the camera's starting distance (`camera.iso_reveal`), scaled with the zoom so it keeps its
  size on the screen, its outer 60 % thinning out, and
- higher than 1 unit above the player's feet, fully from 4.

So the floor the player stands on and the ground falling away in front stay, and a wall, a slope rising in front,
a roof or an upper floor goes. The zone, its objects (doors, placed objects, ground items) and the grass are cut; the
characters are not. The cut drops pixels against a 4x4 ordered dither rather than blending, so the passes stay opaque
and nothing has to be sorted. The shadow passes do not cut: what is cut still casts its shadow.

A click goes through the cut as well: the ground point a click walks to is the first surface along the cursor's ray
that the cut leaves (`ClientApp::world_point_at`, with `game::iso_reveal_cut`, the shader's test on the CPU).

## /camera

| Command | Effect |
|---|---|
| `/camera` | which camera, and the isometric camera's settings |
| `/camera iso` | the isometric camera |
| `/camera first`, `/camera behind`, `/camera toggle` | first person, the camera behind the player, or between those and the isometric camera |
| `/camera pitch <degrees>` | the look down, 20..89 |
| `/camera fov <degrees>` | the field of view across, 10..90 |
| `/camera distance <units>` | the distance, 60..320 |
| `/camera wasd camera` or `classic` | the movement keys relative to the camera, or as in the other cameras |
| `/camera reveal <units>` | the cut round the player: its radius at the starting distance, 0..60 (0 off) |

## Settings

`hailstone.ini`, written by `/camera`:

| Key | Default | |
|---|---|---|
| `camera.iso` | 0 | start in the isometric camera |
| `camera.iso_pitch` | 45 | degrees below the horizon |
| `camera.iso_fov` | 63 | degrees across |
| `camera.iso_distance` | 160 | units from the player |
| `camera.iso_wasd` | camera | `camera`: the movement keys relative to the camera; `classic`: as in the other cameras |
| `camera.iso_reveal` | 20 | the cut round the player: its radius at the player at the starting distance (160), 0 off |

## Still to do

- A silhouette of the player where something not cut (a character, the edge of the cut) still hides them.
- Whole objects fading, and a roof or ceiling over the player taken away beyond the circle.
- The cut only while something is in the way, and the spawn pick through it (a click on a spawn behind a cut wall).
- A highlight on what is under the cursor.
- An Options page, the camera key, and `eq.*` for the UI packs.
