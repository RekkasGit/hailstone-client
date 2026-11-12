#pragma once

// The original's in-game cameras besides first person and the wheel-follow camera: EverQuest::Cameras[1..5]
// (unk_InitCameras @0x5338a0): the overhead camera (1, EQCamera_Overhead), the chase camera (2, EQCamera_Chase), the
// two user cameras (3, 4, EQCamera_User) and the tether camera (5, vftable 0x9d11c8). Each frame
// unk_CameraDriverPerFrame @0x796dd0 runs the active camera's update (vtable +8) on the view actor's spawn (the
// local player). Their fields are game/char_select_camera.h's UserCamera (every camera shares the base layout).
//
// Here: their defaults (CDisplay_ResetCameraDefaults @0x4882a0), their updates, CDisplay::ToggleView's cycle,
// what the keys and the mouse change in each (ProcessControls @0x52bbd0, HandleMouseLook @0x516d40, __ExecuteCmd
// case 19) and unk_EQCamera_SetPosition @0x490f60's two sweeps. Positions are world (zone file) coordinates;
// headings and pitches are the original's 512ths (128 = 90 degrees).

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include <glm/glm.hpp>

#include "game/char_select_camera.h"

namespace hailstone::game {

// CDisplay::cameraType.
enum CameraType : int {
    kCameraFirstPerson = 0,
    kCameraOverhead = 1,
    kCameraChase = 2,
    kCameraUser1 = 3,
    kCameraUser2 = 4,
    kCameraTether = 5,
    kCameraWheelFollow = 6, // this client's camera behind the player
    kCameraIsometric = 7,   // this client's isometric camera (game/iso_camera.h)
};

// A camera object: its fields, and the position the update leaves (+0x04..+0x0c) and the one before (+0x1c..+0x24).
struct GameCamera {
    UserCamera f;
    glm::vec3 position{0.0f};
    glm::vec3 previous{0.0f};
    bool placed = false; // the position was set once (before that the original's is the constructor's zeros)
};

// The spawn the camera follows (the view actor's: the local player).
struct CameraTarget {
    glm::vec3 position{0.0f}; // +0x64 / +0x68 / +0x6c (z: the feet plus AvatarHeight)
    float heading = 0.0f;     // +0x80
    float eye_height = 0.0f;  // PlayerClient_GetEyeHeight (vtable +0xd0)
    bool ducking = false;     // StandState (+0x35c) 111
    bool sitting = false;     // StandState 110
    // 0x58cf90: riding. With the mount's dag 0xe found (0x58c8a0), its world z: the eye z is eye + that z;
    // else z + eye + 3.5.
    bool riding = false;
    std::optional<float> mount_seat_z;
    // The tether camera's sitting case: the spawn's bone 0 world z (0x58c8a0(S, 0)), when found.
    std::optional<float> root_bone_z;
};

// unk_EQCamera_SetPosition's sweep (s3dCollideSphereWithWorld, mode 3, radius 0.25): where a sphere moved from `from`
// toward `to` stops, or nullopt when the sweep is refused (the outputs are then left as they were).
using CameraSweep = std::function<std::optional<glm::vec3>(const glm::vec3& from, const glm::vec3& to, float radius)>;

// Where the camera is and how it looks.
struct CameraPose {
    glm::vec3 position{0.0f};
    float heading = 0.0f; // +0x10, 0..512
    float pitch = 0.0f;   // +0x14 (a look-at pitch may be 256..512 for below: CMath's arctangent's range)
};

// CDisplay_ResetCameraDefaults @0x4882a0 for cameras 1..5 (each Reset first): the overhead's height 25 and pitch
// -100; the chase camera's stay 0 (its 8.44 / 3.06 / -17.52 are the Korean locale's); user 1 distance 30, heading 192,
// height 5; user 2 distance 82, heading 277, height 18; both with +0x44 (look at the target) set; the tether's 0.
void reset_camera_defaults(std::array<GameCamera, 6>& cameras);

// The active camera's update (vtable +8) in game (GameState 5): cameras 1..5.
CameraPose update_camera(int type, GameCamera& cam, const CameraTarget& target, const CameraSweep& sweep);

// Hailstone's camera behind the player (6, the mouse wheel's): `distance` from `eye` along `back` (a unit vector),
// stopped where unk_EQCamera_SetPosition's sweep (radius 0.25, the zone and its objects) stops, as cameras 1..5 are.
// It used to step the distance down by a fifth until the line behind was clear, so in a small room it jumped between
// those steps as the player moved. A refused sweep leaves the camera at the eye. Without a sweep: the full distance.
glm::vec3 wheel_follow_position(const glm::vec3& eye, const glm::vec3& back, float distance, const CameraSweep& sweep);

// CDisplay::ToggleView @0x494160 (not drunk): the camera after `type`: 0 -> 1 -> .. -> 5 -> 0, and this client's own
// cameras (the wheel-follow 6, the isometric 7) count as 0.
int toggle_view_next(int type);

// Which camera is active and the wheel camera's distance (0 outside camera 6), with the distance it returns to.
struct CameraSelection {
    int type = kCameraFirstPerson;
    float distance = 0.0f;
    float pad_distance = 20.0f;
};
// Any camera but 6: the wheel camera's distance is kept to return to, then 0.
void select_camera(CameraSelection& s, int type);
// Camera 6 at the kept distance (1..max); nothing when it is already active.
void select_behind(CameraSelection& s, float max_distance);
// KnockedOut saves the active camera and selects overhead; GetConscious selects the saved one again: camera 6 comes
// back at its distance, as select_behind brings it.
void restore_camera(CameraSelection& s, int saved, float max_distance);

// Camera virtual +0x2c: true for the chase and user cameras only (0x5aa530); the zoom and camera turn keys need it.
bool camera_active(int type);

// __ExecuteCmd case 19 (CENTERVIEW) on cameras 1..5: the overhead's pitch back to -100 (vf14); the others' +0x44 set
// (vf30(1)).
void center_view(int type, GameCamera& cam);

// The camera pitch setter (vtable +0x14): stored; the user cameras' (0x542d30) also clear +0x44.
void set_camera_pitch(int type, GameCamera& cam, float pitch);

// HandleMouseLook @0x516d40 on cameras 1..5 (the mouse's motion in heading / pitch units, after the client's
// scaling; `raw_dy` the motion the height and distance take; `sensitivity` the mouse sensitivity; `rate`
// g_LocalPhysicsTimeFactor): returns whether the mouse's sideways motion turns the player (cameras 1 and 2), else
// it turned the camera (3..5). Up and down: the overhead's pitch (+-128), the chase camera's height, the user
// cameras' distance (at least 3) and the tether's pitch (-120..64); under water (`underwater`) the user and tether
// cameras take the pitch as the overhead does.
bool camera_mouse_look(int type, GameCamera& cam, float dx, float dy, float raw_dy, float sensitivity, float rate,
                       bool underwater);

} // namespace hailstone::game
