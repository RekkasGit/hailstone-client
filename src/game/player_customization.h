#pragma once

// The player customization window (CPlayerCustomizationWnd, EQUI_PlayerCustomizationWnd.xml; /facepick and key 492
// open it): the face, hair, eyes, beard, Barbarian tattoo, Drakkin heritage, tattoo and facial attachment of the
// local player, tried on the model and sent with OP_FaceChange. Ported from the original's functions (rof2decomp
// export CPlayerCustomizationWnd.c and disassembly; the window's members by their offsets):
//   AboutToShow @0x715cf0, AboutToHide @0x714620, OnProcessFrame @0x715ef0, WndNotification @0x715f30,
//   sub_00715770 (the rows' states), PlayerCustomization_ClampFeaturesToModel @0x714f60 (the new-style rows),
//   sub_00714850 (a new-style feature stepped), sub_00714a40 (an old-style feature stepped through SetFHEB),
//   ReadLocalPlayerFeatures @0x714690, SaveOriginalFeatures @0x7147c0,
//   EQPlayer::SetDefaultFacialFeaturesByFace @0x58de30 (Random on a Luclin model),
//   PlayerClient::CanBeBald @0x58bdf0, PlayerClient_CanHaveFacialHair @0x58be50.
// The model work (SetFHEB, SetFacialFeatureNewModel, ApplyFacialFeatures, SwapFace, SetHeritage) goes through
// CustomizationActor, which the client implements on the player's spawn and model.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "game/actor_look.h"
#include "game/char_select_camera.h"

namespace nmsc::game {

// The actor's features (ActorBase +8..+0xe, +0x80..+0x88; PlayerClient +0xeac..+0xeb2, +0xf24..+0xf2c).
struct FacialFeatures {
    std::uint8_t face = 0;        // +0x8 (Barbarians: tattoo * 10 + face)
    std::uint8_t hair_color = 0;  // +0x9
    std::uint8_t beard_color = 0; // +0xa
    std::uint8_t eye1 = 0, eye2 = 0; // +0xb, +0xc
    std::uint8_t hair_style = 0;  // +0xd
    std::uint8_t beard = 0;       // +0xe
    std::int32_t heritage = 0, tattoo = 0, details = 0; // +0x80, +0x84, +0x88
    bool operator==(const FacialFeatures&) const = default;
};

// PlayerClient::CanBeBald @0x58bdf0 on the race shown (vtable +0x88) and gender: 0 for Iksar, Vah Shir, Froglok and
// male Trolls and Ogres (no hair rows), 2 for Erudites (the scalp is the face's tens digit), else 1.
std::uint8_t can_be_bald(std::int32_t race, std::uint8_t gender);
// PlayerClient_CanHaveFacialHair @0x58be50: males, and female Trolls and Ogres.
bool can_have_facial_hair(std::int32_t race, std::uint8_t gender);

// The local player as CPlayerCustomizationWnd reaches it (pinstLocalPlayer, its ActorClient and the customization
// manager).
class CustomizationActor {
public:
    virtual ~CustomizationActor() = default;
    virtual bool has_actor() = 0;            // +0x1024 (the animation set)
    virtual bool luclin_model() = 0;         // the animation set's +0x34: a Luclin (SoL) model
    virtual bool new_style_model() = 0;      // PlayerClient_IsUsingNewStyleModel (the actor's vtable +8)
    virtual std::int32_t race() = 0;         // PlayerClient vtable +0x88 (the race shown) and ActorBase +0x10
    virtual std::int32_t profile_race() = 0; // the character's own race (profile +0x3370), -1 without a profile
    virtual std::uint8_t gender() = 0;       // +0xeb9
    virtual std::uint8_t class_id() = 0;     // +0xeb8
    virtual FacialFeatures& features() = 0;  // the actor's, as the functions below change them
    // PlayerClient_SetFHEB @0x58c3e0 / SetFHEBColor @0x58c400 (ActorClient::SetFHEB / SetFHEB_Color): non-zero when
    // the model took the feature.
    virtual std::uint32_t set_fheb(std::uint8_t id, std::uint8_t type) = 0;
    virtual std::uint32_t set_fheb_color(std::uint8_t color, std::uint8_t type) = 0;
    // ActorClient::SetFacialFeatureNewModel @0x40c7d0 (types 1 face .. 10 facial attachment; 8 the heritage by its
    // index), ApplyFacialFeatures @0x40ecf0, SetHeritage @0x40f2f0 (with argument 0), PlayerClient_SwapFace.
    virtual void set_facial_feature_new_model(std::int32_t value, std::int32_t type) = 0;
    virtual void apply_facial_features() = 0;
    virtual void set_heritage(std::int32_t heritage) = 0;
    virtual void swap_face(std::uint8_t face) = 0;
    // The customization manager: the counts of {race, heritage, gender}, the heritages of {race, class, gender} in
    // its order, and the heritage's name (FUN_00408ed0).
    virtual CustomizationCounts counts() = 0;
    virtual std::vector<std::int32_t> heritages() = 0;
    virtual std::string heritage_name() = 0;
    // Random() and RandomNumberLowHigh (inclusive).
    virtual std::int32_t random() = 0;
    virtual std::int32_t random_low_high(std::int32_t low, std::int32_t high) = 0;
};

// EQPlayer::SetDefaultFacialFeaturesByFace(face, 1, 1) @0x58de30 on a Luclin model of a classic race: random eyes,
// hair, beard and colours for the face (`face` < 0: the current one), then SwapFace when the face changed.
void set_default_facial_features_by_face(CustomizationActor& a, std::int32_t face);

// ActorClient::SetHeritage(heritage, keep) @0x40f2f0 on the actor's {race, class, gender} heritages (nothing but
// ApplyFacialFeatures without any): -1 picks a heritage and every feature at random (RandNbr % count, 0 for a count
// below 2); otherwise the heritage is taken when the current one is among them (else a random one), and each
// feature is picked again unless `keep` and it is within its new count (face, hair, beard, eyes, tattoo, facial
// attachment, hair colour, beard colour). Then ApplyFacialFeatures. (The window's ClampFeaturesToModel follows on
// its next frame.)
void actor_set_heritage(CustomizationActor& a, std::int32_t heritage, bool keep);

// The window's rows, in the order of its down / up button arrays (+0x234.. / +0x25c..; slot 8 has no buttons).
enum class CustomizationRow : int {
    Face = 0, Hair, HairColor, Eyes, Beard, BeardColor, Tattoo, Heritage, None8, FacialAttachment, Count
};
inline constexpr int kCustomizationRows = static_cast<int>(CustomizationRow::Count);
// The ScreenIDs of a row's buttons and label ("" for slot 8), and the window's other children.
const char* customization_down_button(int row);
const char* customization_up_button(int row);
const char* customization_text(int row);

class PlayerCustomizationModel {
public:
    // AboutToShow @0x715cf0: refused (false) under an illusion of a race without customization (eqstr 4035, `refusal`)
    // or while the body tint window is open; else the rows are set up, feature 1 picked and the camera on the face.
    // `char_create`: GameState 2 (character creation: the heritage row, Random's new heritage, Accept without
    // OP_FaceChange).
    bool about_to_show(CustomizationActor& a, bool body_tint_open, bool char_create, std::uint32_t& refusal);
    bool char_create() const { return char_create_; }
    // AboutToHide @0x714620: no feature picked (the camera goes back).
    void about_to_hide() { feature_ = 0; }
    // OnProcessFrame @0x715ef0: the rows again when the race shown changed.
    void frame(CustomizationActor& a);
    // WndNotification @0x715f30 for a left click (notification 1): the row buttons, Random, Accept and Cancel. Returns
    // what the click asks of the window.
    enum class Result { None, Accept, Cancel };
    Result click(CustomizationActor& a, std::string_view id);
    // A row's buttons and label as sub_00715770 / ClampFeaturesToModel left them; nullopt: untouched (the XML's).
    std::optional<bool> row_enabled(int row) const { return rows_[static_cast<std::size_t>(row)]; }
    const std::string& heritage_text() const { return heritage_text_; } // ParentId_Text
    bool heritage_text_set() const { return heritage_text_set_; }
    std::optional<bool> heritage_label_enabled() const { return heritage_label_; } // Heritage_Text
    // CharSelect_PositionCameraForModel's last zoom (100 the face, 0 the whole model) while the window frames the
    // player (SetCameraType(7)).
    int camera_zoom() const { return camera_zoom_; }
    // The camera calls made since the last take, in order (the client replays them on Cameras[7]).
    std::vector<CustomizationCameraCall> take_camera_calls() { return std::exchange(camera_calls_, {}); }
    int feature() const { return feature_; } // +0x2b4: 1 face .. 10 facial attachment, 0 none

    // The window's copies (ReadLocalPlayerFeatures / SaveOriginalFeatures), for the tests.
    struct Selection {
        std::uint8_t face = 0, eye1 = 0, eye2 = 0, hair_style = 0, beard = 0, hair_color = 0, beard_color = 0;
        std::uint8_t face_variant = 0; // +0x2bf: Barbarian tattoo, Erudite scalp, Elf / Dwarf beard (face / 10)
        std::uint8_t heritage_index = 0, tattoo = 0, details = 0;
    };
    const Selection& selection() const { return sel_; }

private:
    void refresh(CustomizationActor& a);                   // sub_00715770
    void clamp_features_to_model(CustomizationActor& a);   // PlayerCustomization_ClampFeaturesToModel
    void read_features(CustomizationActor& a);             // ReadLocalPlayerFeatures
    void save_original() { orig_ = sel_; }                 // SaveOriginalFeatures
    void step_new_model(CustomizationActor& a, bool up);   // sub_00714850
    void step_old_model(CustomizationActor& a, bool up, bool color); // sub_00714a40
    void face_camera(bool raise);       // FaceCamera (AboutToShow's raises a Froglok's)
    void position_camera(int mode);     // CharSelect_PositionCameraForModel(mode)

    Selection sel_, orig_;
    int feature_ = 0;
    std::int32_t race_seen_ = -1; // +0x2b2
    std::optional<bool> rows_[kCustomizationRows];
    std::optional<bool> heritage_label_;
    std::string heritage_text_;
    bool heritage_text_set_ = false;
    int camera_zoom_ = 100;
    std::vector<CustomizationCameraCall> camera_calls_;
    bool zoomed_ = true; // 0xb3b198: the last zoom was the face's
    bool char_create_ = false;
};

} // namespace nmsc::game
