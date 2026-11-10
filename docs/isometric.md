# Isometric camera

Code: `src/game/iso_camera.{h,cpp}` (the camera), `ClientApp::update_iso_camera` and `ClientApp::camera_command`
(`src/app/client_app_input.cpp`). Tests: `tests/iso_camera_test.cpp`.

The original client has no such camera. This is a Hailstone addition, off unless the player chooses it: a view from
above and to one side, in the manner of isometric RPGs. It changes only where the scene is seen from. Movement,
facing, speed, collision and every game rule stay the original's, and the server sees ordinary movement.

## The camera

Camera type 7 (`game::kCameraIsometric`), Hailstone's own beside the camera behind the player (6). It looks down at a
fixed pitch (45 degrees by default) from a distance (160 units at the start), through a perspective lens (63 degrees
across by default, about the stock cameras' 63.3). It follows the player's body with a little easing (0.08 s) and
jumps with a teleport or a zone-in. It does not stop at scenery: what stands between it and the player is left to
the occlusion (not done yet, see "Still to do").

- The right button held turns the camera freely about the player, never the player. A short right click still
  clicks.
- The left button always clicks; it never starts a mouse look.
- The wheel zooms, a tenth of the distance a notch, from 60 to 320 units.
- TOGGLECAM's cycle counts it as first person (0 -> 1), as it counts camera 6; the first person camera's key leaves
  it.

To the movement controls it is the camera behind the player (6) with no mouse look, so the turn keys turn and
neither button runs.

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

## /camera

| Command | Effect |
|---|---|
| `/camera` | which camera, and the isometric camera's settings |
| `/camera iso` | the isometric camera |
| `/camera first`, `/camera behind`, `/camera toggle` | first person, the camera behind the player, or between those and the isometric camera |
| `/camera pitch <degrees>` | the look down, 20..89 |
| `/camera fov <degrees>` | the field of view across, 10..90 |
| `/camera distance <units>` | the distance, 60..320 |

## Settings

`hailstone.ini`, written by `/camera`:

| Key | Default | |
|---|---|---|
| `camera.iso` | 0 | start in the isometric camera |
| `camera.iso_pitch` | 45 | degrees below the horizon |
| `camera.iso_fov` | 63 | degrees across |
| `camera.iso_distance` | 160 | units from the player |

## Still to do

- WASD relative to the camera, and the gamepad's stick.
- Click to move and to interact, on the navigator (`docs/navigation.md`).
- Seeing the player through what stands in front: a silhouette, a see-through circle, fading objects, roofs and
  ceilings.
- An Options page, the camera key, and `eq.*` for the UI packs.
