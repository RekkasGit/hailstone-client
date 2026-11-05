#pragma once

// What character creation does to the model player (the local player at GameState 2), ported from rof2decomp's
// CCharacterCreation (export _globals.c / CCharacterCreation.c; re/src/game/ui/char_create_faces.cpp):
//   CCharacterCreation_SetRace @0x64eeb0 (the model part: ApplyNewPCToPlayer, then RandomizeFacialFeatures),
//   sub_0064d930 (the gender: ApplyNewPCToPlayer), the class (sub_0064dff0, then CCharacterCreation_SetHeritage with
//   the actor's heritage, WndNotification @0x64f150), CCharacterCreation_ApplyNewPCToPlayer @0x64b080,
//   CCharacterCreation::RandomizeFacialFeatures @0x64a9a0, CCharacterCreation_SetHeritage @0x64a840 (the wrapper
//   that keeps the actor's features in the window's character), CCharacterCreation_CopyAppearanceFrom @0x64af00 (the
//   customization window's Accept) and Send_OP_CharacterCreate @0x64b560 (the packet's appearance comes from the
//   actor).
// The window's character (CCharacterCreation +0x228, a PcClient) keeps a copy of the face, hair, beard, eyes,
// heritage, tattoo and details (profile +0x33b4, PcBase hair / beard / eyes, +0x3478..+0x3480): ApplyNewPCToPlayer
// puts that copy back on the model after a race or gender switch. The model itself (its race, gender and class,
// SwitchPlayerModel) is the caller's: the actor's race(), gender() and class_id() are already the new ones.
// The model is bare: ApplyNewPCToPlayer clears the nine equipment slots and refreshes them from the window's new
// character, which has no items (RefreshEquipSlot with its empty armour), and sets the held models 7 and 8 to "0".
// No class armour is shown.

#include <cstdint>

#include "game/player_customization.h"

namespace nmsc::game {

class CharCreateLook {
public:
    // CCharacterCreation_SetRace's model part: ApplyNewPCToPlayer, then RandomizeFacialFeatures.
    void race_changed(CustomizationActor& a) {
        apply_new_pc(a);
        randomize_facial_features(a);
    }
    // sub_0064d930: the gender, then ApplyNewPCToPlayer.
    void gender_changed(CustomizationActor& a) { apply_new_pc(a); }
    // WndNotification's class button: the class (sub_0064dff0), then CCharacterCreation_SetHeritage(the actor's
    // heritage): a Drakkin's heritages depend on the class.
    void class_changed(CustomizationActor& a) { set_heritage(a, a.features().heritage); }

    // CCharacterCreation_ApplyNewPCToPlayer: the window character's face and features onto the model.
    void apply_new_pc(CustomizationActor& a);
    // CCharacterCreation::RandomizeFacialFeatures: a new-style model of a classic head race gets
    // CCharacterCreation_SetHeritage(-1); an old one a random face (0..7, 0..9 for Froglok), SwapFace,
    // SetDefaultFacialFeaturesByFace(-1, 1, 1), ApplyFacialFeatures, hair 1 for hair 0xff on a model that cannot
    // have facial hair; the features into the window's character.
    void randomize_facial_features(CustomizationActor& a);
    // CCharacterCreation_SetHeritage: ActorClient::SetHeritage(heritage, 1), the actor's features into the window's
    // character.
    void set_heritage(CustomizationActor& a, std::int32_t heritage);
    // CCharacterCreation_CopyAppearanceFrom (the customization window's Accept at GameState 2): the actor's
    // features (as the window stored them in the local PC) into the window's character.
    void copy_appearance_from(const FacialFeatures& f) { pc_ = f; }

    // The window's character's copy (CCharacterCreation +0x228).
    const FacialFeatures& character() const { return pc_; }

private:
    void keep_actor_features(CustomizationActor& a); // the actor's features into the window's character

    FacialFeatures pc_;
};

} // namespace nmsc::game
