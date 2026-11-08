#include "game/char_create_look.h"

#include "game/appearance_data.h"

namespace hailstone::game {

void CharCreateLook::keep_actor_features(CustomizationActor& a) { pc_ = a.features(); }

void CharCreateLook::apply_new_pc(CustomizationActor& a) {
    // The window character's face (+0x33b4), beard, beard colour, hair, hair colour, eyes, heritage, tattoo and
    // details onto the model (+0xeac..+0xeb2, +0xf24..+0xf2c), the heritage as the byte the profile keeps. Then
    // CharSelect_SwitchPlayerModel (the caller's model), the equipment cleared, PlayerClient_SwapFace(face).
    FacialFeatures& f = a.features();
    f.face = pc_.face;
    f.beard = pc_.beard;
    f.beard_color = pc_.beard_color;
    f.hair_style = pc_.hair_style;
    f.hair_color = pc_.hair_color;
    f.eye1 = pc_.eye1;
    f.eye2 = pc_.eye2;
    f.heritage = static_cast<std::uint8_t>(pc_.heritage);
    f.tattoo = pc_.tattoo;
    f.details = pc_.details;
    a.swap_face(pc_.face);
}

void CharCreateLook::set_heritage(CustomizationActor& a, std::int32_t heritage) {
    actor_set_heritage(a, heritage, true);
    keep_actor_features(a);
}

void CharCreateLook::randomize_facial_features(CustomizationActor& a) {
    if (a.new_style_model() && classic_head_race(a.race())) {
        set_heritage(a, -1);
        return;
    }
    const std::int32_t faces = a.profile_race() == 330 ? 9 : 7; // the window's race (+0x3370)
    pc_.face = static_cast<std::uint8_t>(a.random() % (faces + 1));
    a.swap_face(pc_.face);
    set_default_facial_features_by_face(a, -1);
    a.apply_facial_features();
    FacialFeatures& f = a.features();
    if (!can_have_facial_hair(a.race(), a.gender()) && f.hair_style == 0xff) f.hair_style = 1;
    // The hair, hair colour, beard, beard colour and eyes into the window's character (the face stays its own).
    pc_.hair_style = f.hair_style;
    pc_.hair_color = f.hair_color;
    pc_.beard = f.beard;
    pc_.beard_color = f.beard_color;
    pc_.eye1 = f.eye1;
    pc_.eye2 = f.eye2;
    a.apply_facial_features();
}

} // namespace hailstone::game
