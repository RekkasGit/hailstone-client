#pragma once

// The camera of character select and creation (and of the player customization window in game): EverQuest::Cameras[7],
// an EQCamera_User, as the original places it around the model player. Ported from rof2decomp:
//   unk_InitCameras @0x5338a0 (Cameras[7] is an EQCamera_User, vftable 0x9d1148), its virtuals: +4 reset @0x542550,
//   +8 update EQCamera_User::vf2 @0x798c90, +0xc..+0x28 the setters @0x542580..0x5425f0;
//   FUN_00517840 (StartNetworkGame: SetCameraType(7) and the first placement);
//   CDisplay::SetCCreateCamera / CharSelect_PositionCameraForModel @0x4950f0 (re/src/game/spawn/char_create_camera.cpp);
//   DoCharacterSelection @0x53aac0's 50 ms turn (CharCreateRotateOn) and DoNewCharacterCreation @0x53a760's;
//   the customization window's face camera (CPlayerCustomizationWnd AboutToShow @0x715cf0, sub_00714850).
// Positions are world (zone file) coordinates (game/coords.h): the original's +0x64 / +0x68 are world x / y.

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

namespace hailstone::game {

// The EQCamera_User fields the original writes (the camera object's offsets).
struct UserCamera {
    float c28 = 0.0f;      // +0x28 (setter +0xc; never read by the update)
    float height = 0.0f;   // +0x2c: above the target's eye (setter +0x10)
    float pitch = 0.0f;    // +0x30 (setter +0x14)
    float distance = 0.0f; // +0x34 (setter +0x18)
    float heading = 0.0f;  // +0x38: where the camera stands from the target, 0..512 (setter +0x20, getter +0x24)
    float c3c = 0.0f;      // +0x3c (setter +0x1c)
    float fov = 90.0f;     // +0x40: the field of view in 512ths (setter +0x28)
    bool look_pitch = false;    // +0x44: pitch toward the target instead of +0x30
    bool fixed_heading = false; // +0x45: the target's heading (+0x80) instead of the bearing to it

    // Virtual +4 @0x542550: every field 0 (+0x44 / +0x45 kept), the field of view 90.
    void reset();
};

// The model player as the camera and SetCCreateCamera read it.
struct UserCameraTarget {
    glm::vec3 position{0.0f}; // the spawn's x, y, z (world)
    float eye_height = 0.0f;  // PlayerClient_GetEyeHeight (vtable +0xd0): the eye above z
    bool flag_35c = false;    // PlayerClient +0x35c == 'o' (unknown; not seen at character select): eye z + 1.5
};

// Where the camera is and how it looks, from EQCamera_User::vf2.
struct UserCameraPose {
    glm::vec3 position{0.0f};
    float heading = 0.0f; // +0x10: the camera's heading, 0..512 (toward the target)
    float pitch = 0.0f;   // +0x14 (512ths)
};

// EQCamera_User::vf2 @0x798c90 in GameState 1 / 2 (the 2..200 distance and 0..30 height clamps are GameState 5's):
// the heading is wrapped (FixHeading); the camera stands at the target's x + cos(h) * d, y + sin(h) * d (the sine
// table: integer 512ths, no interpolation), z + eye (+ 1.5 for flag 0x35c) + height. The pose looks along the
// bearing from the camera to the target (__get_bearing) with pitch +0x30. Not ported: the mount's dag 0xe eye
// (no mounts outside the game), and unk_EQCamera_SetPosition's collision sweeps (0.25 radius) from the target to
// that point: nothing at character select stands between the model and the camera. `in_game`: GameState 5's clamps.
UserCameraPose user_camera_update(UserCamera& cam, const UserCameraTarget& target, bool in_game = false);

// What CDisplay::SetCCreateCamera reads of the model player.
struct CCreateInputs {
    std::int32_t race = 0;       // PlayerClient vtable +0x88
    std::uint8_t gender = 0;     // +0xeb9
    bool new_style_model = false; // PlayerClient_IsUsingNewStyleModel
    bool model_settings = false;  // the actor definition (+0x1024) exists and its +0x34 is set (a Luclin model)
    std::uint8_t can_be_bald = 1; // PlayerClient::CanBeBald (game/player_customization.h)
    float heading = 0.0f;         // PlayerBase +0x80
    float size = 6.0f;            // +0x13c Height
    float view_height = 0.0f;     // +0x190 ViewHeight (FUN_00506330)
    float ccc_distance = 2.5f;    // ModDat_GetCCCDis(model code): moddat.ini CCCDis, 2.5 without one
    float ccc_height = -1.0f;     // ModDat_GetCCCHt: CCCHt, -1.0 without one
};
// What SetCCreateCamera does besides the camera.
struct CCreateResult {
    std::optional<float> fov;            // CDisplay +0x28f4 (RebuildProjection): 90 for mode 0, 70 for modes but 100
    std::optional<std::uint8_t> animation; // the model's animation (PlayerAnimation vtable +0xc): 0x47 or 0x20
};
// CDisplay::SetCCreateCamera(mode) @0x4950f0: mode 0 the whole model at character select (field of view 90; height
// -(ViewHeight x 0.33); distance Height + 2 + 6, or + 7 for Trolls, Ogres and Iksar); mode 100 the face (the
// customization window), 2 / 3 the hair pages; others take field of view 70. A new-style model takes moddat.ini's
// CCCDis / CCCHt; an old one the per race and gender constants of the original (re/src/game/spawn/char_create_camera
// .cpp lists them).
CCreateResult set_ccreate_camera(UserCamera& cam, int mode, const CCreateInputs& in);

// FUN_00517840 (StartNetworkGame, before character select): SetCameraType(7), distance 6, height -(ViewHeight x
// 0.33), heading 456, pitch 0, +0x28 0, field of view 90.
void start_network_game_camera(UserCamera& cam, float view_height);

// The customization window's face camera (sub_00714850 / AboutToShow's FUN): SetCameraType(7), the camera reset,
// SetCCreateCamera(100), the heading the player's (+0x80), the distance + 1.5, pitch 0; a Froglok (race 330) also
// raises the height by 1.5 (AboutToShow only: `froglok_raise`).
CCreateResult face_camera(UserCamera& cam, const CCreateInputs& in, bool froglok_raise);

// A camera call of the player customization window (CPlayerCustomizationWnd, at character creation and in game alike:
// Cameras[7] is the camera of both): its face camera (`face`; AboutToShow's with `raise`), or sub_00714850's
// CharSelect_PositionCameraForModel(mode) when a step zooms in (100) or out (0, the tattoos).
struct CustomizationCameraCall {
    bool face = false;
    int mode = 100;
    bool raise = false;
    bool operator==(const CustomizationCameraCall&) const = default;
};
CCreateResult customization_camera(UserCamera& cam, const CustomizationCameraCall& call, const CCreateInputs& in);

// One 50 ms step of character select's camera (DoCharacterSelection): the heading follows the model's. A deliberate
// difference: the original's Rotate button (CharCreateRotateOn: the heading turns by 3 a step) is left out.
void char_select_camera_tick(UserCamera& cam, float model_heading);
// One 50 ms step of creation's camera (DoNewCharacterCreation): while the customization window shows the hair
// (page 2) or, on a new-style model, the hair colour (page 3), SetCCreateCamera(page) and, when the model can be
// bald (1), the heading + 1.5; otherwise the heading follows the model's.
CCreateResult char_create_camera_tick(UserCamera& cam, int customization_page, const CCreateInputs& in);

// CMath's table sine and cosine (EQGraphicsDX9 vtable +0x08 / +0x0c): entry (int)trunc(h) & 511, no interpolation.
float table_sin(float heading);
float table_cos(float heading);

} // namespace hailstone::game
