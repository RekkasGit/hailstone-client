#include "game/player_customization.h"

#include <algorithm>

namespace hailstone::game {

namespace {

constexpr std::int32_t kRaceBarbarian = 2, kRaceErudite = 3, kRaceDwarf = 8, kRaceTroll = 9,
                       kRaceOgre = 10, kRaceIksar = 128, kRaceVahShir = 130, kRaceFroglok = 330;

struct RowIds {
    const char* down;
    const char* up;
    const char* text;
};
// CPlayerCustomizationWnd's constructor @0x716b80 (rof2decomp re/src/game/ui/child_items/cplayercustomizationwnd.inc):
// the down buttons at +0x234.., the up buttons at +0x25c.., the labels at +0x284...
constexpr RowIds kRows[kCustomizationRows] = {
    {"Face_Down_Button", "Face_Up_Button", "Face_Text"},
    {"Hair_Down_Button", "Hair_Up_Button", "Hair_Text"},
    {"Hair_Color_Down_Button", "Hair_Color_Up_Button", "Hair_Color_Text"},
    {"Eyes_Down_Button", "Eyes_Up_Button", "Eyes_Text"},
    {"Beard_Down_Button", "Beard_Up_Button", "Beard_Text"},
    {"Beard_Color_Down_Button", "Beard_Color_Up_Button", "Beard_Color_Text"},
    {"TattooIndex_Down_Button", "TattooIndex_Up_Button", "TattooIndex_Text"},
    {"ParentId_Down_Button", "ParentId_Up_Button", "ParentId_Text"},
    {"", "", ""},
    {"FacialAttachmentIndex_Down_Button", "FacialAttachmentIndex_Up_Button", "FacialAttachmentIndex_Text"},
};

// FUN_0040c2a0's return: the face without its tens digit (a new-style classic race keeps it whole).
std::uint8_t face_index(CustomizationActor& a, std::int32_t face) {
    return split_face(face, a.features().face, a.race(), a.new_style_model()).index;
}

// The races whose new-style model takes the customization lists (the checks after IsUsingNewStyleModel).
bool new_model_customization(CustomizationActor& a) { return a.new_style_model() && classic_head_race(a.race()); }

std::int32_t heritage_index(CustomizationActor& a, std::int32_t heritage) {
    // CPlayerCustomizationManager_GetHeritageIndex @0x8cef60: the heritage's place in the list, 0 when not in it.
    auto list = a.heritages();
    auto it = std::find(list.begin(), list.end(), heritage);
    return it == list.end() ? 0 : static_cast<std::int32_t>(it - list.begin());
}

std::int32_t heritage_by_index(CustomizationActor& a, std::int32_t index) {
    // CPlayerCustomizationManager_GetHeritageByIndex @0x8ceef0 (FUN_00714470): 0 past the list.
    auto list = a.heritages();
    return index >= 0 && index < static_cast<std::int32_t>(list.size()) ? list[static_cast<std::size_t>(index)] : 0;
}

} // namespace

std::uint8_t can_be_bald(std::int32_t race, std::uint8_t gender) {
    if (race == kRaceIksar || race == kRaceVahShir || race == kRaceFroglok) return 0;
    if (race == kRaceErudite) return 2;
    if ((race == kRaceTroll || race == kRaceOgre) && gender == 0) return 0;
    return 1;
}

bool can_have_facial_hair(std::int32_t race, std::uint8_t gender) {
    return gender == 0 || race == kRaceOgre || race == kRaceTroll;
}

void actor_set_heritage(CustomizationActor& a, std::int32_t heritage, bool keep) {
    const std::vector<std::int32_t> list = a.heritages();
    if (list.empty()) {
        a.apply_facial_features();
        return;
    }
    FacialFeatures& f = a.features();
    // RandNbr % count, or 0 when the count is below 2 (no number drawn).
    auto pick = [&a](std::int32_t count) { return count - 1 < 1 ? 0 : a.random() % count; };
    if (heritage == -1) {
        f.heritage = list[static_cast<std::size_t>(pick(static_cast<std::int32_t>(list.size())))];
        // Each count is read again for the new heritage, in this order.
        f.face = static_cast<std::uint8_t>(pick(a.counts().faces));
        f.hair_style = static_cast<std::uint8_t>(pick(a.counts().hair_styles));
        f.hair_color = static_cast<std::uint8_t>(pick(a.counts().hair_colors));
        f.beard = static_cast<std::uint8_t>(pick(a.counts().beards));
        f.beard_color = static_cast<std::uint8_t>(pick(a.counts().hair_colors));
        f.eye1 = f.eye2 = static_cast<std::uint8_t>(pick(a.counts().eyes));
        f.tattoo = pick(a.counts().tattoos);
        f.details = pick(a.counts().facial_attachments);
    } else {
        // FUN_008cec80: the record of the actor's current heritage lists its class (it is among the heritages of
        // {race, class, gender}); then the asked heritage is taken, else one at random.
        if (std::find(list.begin(), list.end(), f.heritage) != list.end())
            f.heritage = heritage;
        else
            f.heritage = list[static_cast<std::size_t>(pick(static_cast<std::int32_t>(list.size())))];
        auto again = [&](std::int32_t current, std::int32_t count) { return !keep || count <= current; };
        if (again(f.face, a.counts().faces)) f.face = static_cast<std::uint8_t>(pick(a.counts().faces));
        if (again(f.hair_style, a.counts().hair_styles))
            f.hair_style = static_cast<std::uint8_t>(pick(a.counts().hair_styles));
        if (again(f.beard, a.counts().beards)) f.beard = static_cast<std::uint8_t>(pick(a.counts().beards));
        if (again(f.eye1, a.counts().eyes)) f.eye1 = f.eye2 = static_cast<std::uint8_t>(pick(a.counts().eyes));
        if (again(f.tattoo, a.counts().tattoos)) f.tattoo = pick(a.counts().tattoos);
        if (again(f.details, a.counts().facial_attachments)) f.details = pick(a.counts().facial_attachments);
        if (again(f.hair_color, a.counts().hair_colors))
            f.hair_color = static_cast<std::uint8_t>(pick(a.counts().hair_colors));
        if (again(f.beard_color, a.counts().hair_colors))
            f.beard_color = static_cast<std::uint8_t>(pick(a.counts().hair_colors));
    }
    a.apply_facial_features();
}

const char* customization_down_button(int row) { return kRows[row].down; }
const char* customization_up_button(int row) { return kRows[row].up; }
const char* customization_text(int row) { return kRows[row].text; }

void set_default_facial_features_by_face(CustomizationActor& a, std::int32_t face) {
    // Called as (face, 1, 1): no early return on the eye colour, and the features are picked at random.
    if (!classic_head_race(a.race()) || !a.has_actor() || !a.luclin_model()) return;
    FacialFeatures& f = a.features();
    std::uint8_t new_face = f.face;
    const std::uint8_t index = face_index(a, face < 0 ? -1 : face);
    f.hair_style = a.race() == kRaceVahShir && index == 7 ? 0 : 0xff;
    f.beard = 0xff;
    f.hair_color = 0xff;
    f.beard_color = 0xff;
    const std::uint8_t bald = can_be_bald(a.race(), a.gender());
    if (bald == 1) {
        f.hair_style = 0;
        f.hair_color = static_cast<std::uint8_t>(allowed_hair_color_range(a.race()).low);
    } else if (bald == 2) {
        f.hair_style = 1;
        new_face = static_cast<std::uint8_t>(index + 10);
        f.hair_color = static_cast<std::uint8_t>(allowed_hair_color_range(a.race()).low);
    }
    const auto eyes = static_cast<std::uint8_t>(a.random_low_high(1, 9));
    f.eye1 = eyes;
    f.eye2 = eyes;
    new_face = index;
    if (a.race() == kRaceBarbarian && a.random_low_high(1, 100) < 0x1f)
        new_face = static_cast<std::uint8_t>(a.random_low_high(0, 8) * 10 + index);
    if (can_be_bald(a.race(), a.gender()) == 1) {
        HairColorRange range = allowed_hair_color_range(a.race());
        const std::int32_t styles = can_have_facial_hair(a.race(), a.gender()) ? 3 : 2;
        auto style = static_cast<std::uint8_t>(a.random_low_high(0, styles));
        f.hair_style = style;
        if (style == 3) f.hair_style = static_cast<std::uint8_t>(a.random_low_high(0, styles));
        if (f.hair_style == 3) f.hair_style = 0xff;
        f.hair_color = static_cast<std::uint8_t>(a.random_low_high(range.low, range.high));
    } else if (can_be_bald(a.race(), a.gender()) == 2) {
        const std::int32_t scalps = a.race() == kRaceErudite ? (a.gender() != 0 ? 8 : 5) : 5;
        new_face = static_cast<std::uint8_t>(a.random_low_high(0, scalps) * 10 + index);
    }
    const std::uint8_t beards = beard_kind(a.race(), a.gender());
    if (beards == 1) {
        if (a.race() == kRaceDwarf || a.random_low_high(1, 100) < 0x29) {
            f.beard = static_cast<std::uint8_t>(a.random_low_high(0, 4));
            f.beard_color = f.hair_color;
        } else {
            f.beard = 0xff;
            f.beard_color = 0xff;
        }
    } else if (beards == 2 && a.random_low_high(1, 100) < 0x29) {
        new_face = static_cast<std::uint8_t>(a.random_low_high(0, a.race() != kRaceDwarf ? 3 : 1) * 10 + index);
        f.beard_color = f.hair_color;
    }
    if (f.face != new_face) a.swap_face(new_face);
}

bool PlayerCustomizationModel::about_to_show(CustomizationActor& a, bool body_tint_open, bool char_create,
                                             std::uint32_t& refusal) {
    refusal = 0;
    const std::int32_t own = a.profile_race();
    if (own >= 0 && own != a.race() && !classic_head_race(a.race()) && !char_create) {
        refusal = 4035; // You cannot change your face while under an illusion.
        return false;
    }
    if (body_tint_open) return false;
    char_create_ = char_create;
    refresh(a);
    feature_ = 1;
    face_camera(true);
    return true;
}

void PlayerCustomizationModel::frame(CustomizationActor& a) {
    if (a.has_actor() && race_seen_ != a.race()) refresh(a);
}

void PlayerCustomizationModel::face_camera(bool raise) {
    camera_zoom_ = 100;
    camera_calls_.push_back({true, 100, raise});
}

void PlayerCustomizationModel::position_camera(int mode) {
    camera_zoom_ = mode;
    camera_calls_.push_back({false, mode, false});
}

void PlayerCustomizationModel::read_features(CustomizationActor& a) {
    const FacialFeatures& f = a.features();
    sel_.beard = f.beard;
    sel_.beard_color = f.beard_color;
    sel_.hair_style = f.hair_style;
    sel_.hair_color = f.hair_color;
    sel_.eye1 = f.eye1;
    sel_.eye2 = f.eye2;
    sel_.face = f.face;
    sel_.heritage_index = static_cast<std::uint8_t>(heritage_index(a, f.heritage));
    sel_.tattoo = static_cast<std::uint8_t>(f.tattoo);
    sel_.details = static_cast<std::uint8_t>(f.details);
    if (auto v = split_face(f.face, f.face, a.race(), a.new_style_model()).variant) sel_.face_variant = *v;
}

void PlayerCustomizationModel::refresh(CustomizationActor& a) {
    if (!a.has_actor()) {
        race_seen_ = 0;
        return;
    }
    if (new_model_customization(a)) {
        clamp_features_to_model(a);
        return;
    }
    const bool luclin = a.luclin_model();
    race_seen_ = a.race();
    read_features(a);
    auto set = [&](CustomizationRow r, bool on) { rows_[static_cast<int>(r)] = on; };
    const std::uint8_t bald = can_be_bald(a.race(), a.gender());
    set(CustomizationRow::Eyes, luclin);
    set(CustomizationRow::Hair, luclin && bald != 0);
    set(CustomizationRow::HairColor, luclin && bald != 0 && bald != 2);
    const bool beard = luclin && beard_kind(a.race(), a.gender()) != 0;
    set(CustomizationRow::Beard, beard);
    set(CustomizationRow::BeardColor, beard);
    set(CustomizationRow::Tattoo, luclin && a.race() == kRaceBarbarian);
    heritage_text_ = a.heritage_name();
    heritage_text_set_ = true;
    heritage_label_ = false;
    set(CustomizationRow::Heritage, false);
    set(CustomizationRow::FacialAttachment, false);
    save_original();
}

void PlayerCustomizationModel::clamp_features_to_model(CustomizationActor& a) {
    race_seen_ = a.race();
    read_features(a);
    auto set = [&](CustomizationRow r, bool on) { rows_[static_cast<int>(r)] = on; };
    // The heritage changes only before the world: with heritages for {race, class, gender} outside GameState 5.
    const bool heritages = !a.heritages().empty() && char_create_;
    set(CustomizationRow::Heritage, heritages);
    heritage_label_ = heritages;
    heritage_text_ = a.heritage_name();
    heritage_text_set_ = true;
    const CustomizationCounts c = a.counts();
    set(CustomizationRow::BeardColor, c.hair_colors >= 2 && c.beards >= 1);
    set(CustomizationRow::HairColor, c.hair_colors >= 2 && c.hair_styles >= 1);
    set(CustomizationRow::Hair, c.hair_styles >= 2);
    set(CustomizationRow::Face, c.faces >= 2);
    set(CustomizationRow::Eyes, c.eyes >= 2);
    set(CustomizationRow::Beard, c.beards >= 2);
    set(CustomizationRow::Tattoo, c.tattoos >= 2);
    set(CustomizationRow::FacialAttachment, c.facial_attachments >= 2);
    save_original();
}

void PlayerCustomizationModel::step_new_model(CustomizationActor& a, bool up) {
    const CustomizationCounts c = a.counts();
    std::int32_t count = 0, current = 0;
    bool face_zoom = true;
    switch (feature_) {
    case 1: count = c.faces, current = sel_.face; break;
    case 2: count = c.hair_styles, current = sel_.hair_style; break;
    case 3: count = c.hair_colors, current = sel_.hair_color; break;
    case 4: count = c.eyes, current = sel_.eye2; break;
    case 5: count = c.beards, current = sel_.beard; break;
    case 6: count = c.hair_colors, current = sel_.beard_color; break;
    case 8: count = static_cast<std::int32_t>(a.heritages().size()), current = sel_.heritage_index; break;
    case 9: count = c.tattoos, current = sel_.tattoo, face_zoom = false; break;
    case 10: count = c.facial_attachments, current = sel_.details; break;
    default: return; // 7 and anything else
    }
    std::int32_t value = count - 1;
    if (!up) {
        if (value < current) current = value;
        if (current != 0) value = current - 1;
    } else {
        value = current + 1;
        if (count - 1 < value) value = 0;
    }
    if (face_zoom != zoomed_) {
        position_camera(face_zoom ? 100 : 0);
        zoomed_ = face_zoom;
    }
    if (feature_ != 9 && feature_ != 2 && feature_ != 3) face_camera(false); // SetCameraType(7), the face again
    a.set_facial_feature_new_model(value, feature_);
    read_features(a);
}

void PlayerCustomizationModel::step_old_model(CustomizationActor& a, bool up, bool color) {
    auto type = static_cast<std::uint8_t>(feature_);
    bool found = false;
    std::uint8_t high = 0x1d;
    std::uint8_t low = 0;
    std::uint8_t* cur = nullptr;
    switch (type) {
    case 1:
        if (!color) {
            sel_.face = face_index(a, sel_.face);
            cur = &sel_.face;
            high = a.race() == kRaceFroglok ? 9 : 7;
        } else {
            if (a.race() != kRaceBarbarian) return;
            high = 8;
            cur = &sel_.face_variant;
        }
        break;
    case 2:
        if (can_be_bald(a.race(), a.gender()) == 0) return;
        if (a.race() == kRaceErudite) {
            if (color) return;
            type = 1;
            color = true;
            high = a.gender() != 0 ? 8 : 5;
            cur = &sel_.face_variant;
        } else if (!color) {
            high = static_cast<std::uint8_t>((can_have_facial_hair(a.race(), a.gender()) ? 1 : 0) + 2);
            cur = &sel_.hair_style;
        } else {
            high = 0x17;
            cur = &sel_.hair_color;
        }
        break;
    case 3:
        if (color) return;
        cur = &sel_.eye2;
        low = a.race() == kRaceFroglok ? 0 : 1;
        high = 0x14;
        break;
    case 4: {
        const std::uint8_t kind = beard_kind(a.race(), a.gender());
        if (kind == 2) {
            if (!color) {
                type = 1;
                color = true;
                cur = &sel_.face_variant;
                high = a.race() != kRaceDwarf ? 3 : 1;
            } else {
                high = 0x17;
                cur = &sel_.beard_color;
            }
        } else {
            if (kind == 0) return;
            if (!color) {
                cur = &sel_.beard;
            } else {
                high = 0x17;
                cur = &sel_.beard_color;
            }
        }
        break;
    }
    default:
        return;
    }
    // The face's tens digit (Barbarian tattoo, Erudite scalp, Elf / Dwarf beard) goes with the face index.
    auto with_variant = [&](std::int32_t v) {
        std::int32_t id = v;
        if (type == 1 && sel_.face_variant != 0) id = sel_.face_variant * 10 + v;
        return static_cast<std::uint8_t>(id);
    };
    std::int32_t v;
    int wraps = 0;
    if (!up) {
        if (high < *cur) *cur = high;
        if (*cur == low) *cur = high;
        else *cur = static_cast<std::uint8_t>(*cur - 1);
        v = *cur;
        for (; low <= v; --v) {
            std::uint32_t took;
            // The face's own digit stays when the tens digit is stepped (the wode, scalp or beard is the colour call).
            if (!color) took = a.set_fheb(with_variant(v), type);
            else took = a.set_fheb_color(static_cast<std::uint8_t>(v), type);
            if (took != 0 || (v == high && can_have_facial_hair(a.race(), a.gender()))) {
                found = true;
                break;
            }
        }
    } else {
        *cur = static_cast<std::uint8_t>(*cur + 1);
        if (high < *cur) *cur = low;
        v = *cur;
        if (v <= high) {
            for (;;) {
                bool took;
                if (!color) took = a.set_fheb(with_variant(v), type) != 0;
                else took = a.set_fheb_color(static_cast<std::uint8_t>(v), type) != 0;
                if (took) {
                    found = true;
                    break;
                }
                if (!color && v == high) {
                    // At the top: eyes always start over from the bottom, hair when there is no facial hair, a
                    // Dwarf's beard for a male; once more than one wrap, it stops.
                    bool wrap;
                    if (type == 3) wrap = true;
                    else if (type == 2) wrap = !can_have_facial_hair(a.race(), a.gender());
                    else if (type == 4 && a.race() == kRaceDwarf) wrap = a.gender() == 0;
                    else wrap = false;
                    if (wrap) {
                        v = low - 1;
                        ++wraps;
                    }
                }
                if (wraps > 1) break;
                ++v;
                if (high < v) break;
            }
        }
    }
    *cur = static_cast<std::uint8_t>(v);
    if (type == 3) {
        sel_.eye1 = sel_.eye2;
        return;
    }
    if (type == 1 && color) {
        if (!found) *cur = 0;
        return;
    }
    if (found) {
        if (type == 2) a.set_fheb_color(sel_.hair_color, 2);
        else if (type == 4) a.set_fheb_color(sel_.beard_color, 4);
        return;
    }
    if (type == 2) {
        if (can_have_facial_hair(a.race(), a.gender())) *cur = 1;
    } else if (type == 4 && a.race() != kRaceDwarf) {
        *cur = 1;
    }
}

PlayerCustomizationModel::Result PlayerCustomizationModel::click(CustomizationActor& a, std::string_view id) {
    if (!a.has_actor()) return Result::None;
    if (id == "Random_Button") {
        if (new_model_customization(a)) {
            // In creation (GameState 2) a new heritage, else the actor's; then ApplyFacialFeatures.
            actor_set_heritage(a, char_create_ ? -1 : a.features().heritage, false);
            a.apply_facial_features();
            read_features(a);
        }
        if (!a.luclin_model()) {
            sel_.face = static_cast<std::uint8_t>(a.random() % 8);
            feature_ = 1;
            step_old_model(a, true, false);
            return Result::None;
        }
        // SetDefaultFacialFeaturesByFace(Random() % 8, 1, 1) and ApplyFacialFeatures, without
        // ReadLocalPlayerFeatures (0x716041 .. 0x716059): the window's copies keep the features from before, so the
        // next step goes on from those, not from the random face.
        set_default_facial_features_by_face(a, a.random() % 8);
        a.apply_facial_features();
        return Result::None;
    }
    if (id == "Accept_Button") return Result::Accept; // the features go to the profile and OP_FaceChange
    if (id == "Cancel_Button") {
        // The features saved when the window showed go back on the model.
        a.set_fheb(orig_.face, 1);
        FacialFeatures& f = a.features();
        f.face = orig_.face;
        f.hair_style = orig_.hair_style;
        f.hair_color = orig_.hair_color;
        f.beard = orig_.beard;
        f.beard_color = orig_.beard_color;
        f.eye1 = orig_.eye1;
        f.eye2 = orig_.eye2;
        f.heritage = heritage_by_index(a, orig_.heritage_index);
        f.tattoo = orig_.tattoo;
        f.details = orig_.details;
        a.apply_facial_features();
        read_features(a);
        return Result::Cancel;
    }
    for (int row = 0; row < kCustomizationRows; ++row) {
        bool up;
        if (id == kRows[row].up && *kRows[row].up) up = true;
        else if (id == kRows[row].down && *kRows[row].down) up = false;
        else continue;
        if (new_model_customization(a)) {
            static constexpr int kNewFeature[kCustomizationRows] = {1, 2, 3, 4, 5, 6, 9, 8, 0, 10};
            if (kNewFeature[row]) feature_ = kNewFeature[row];
            step_new_model(a, up);
            return Result::None;
        }
        static constexpr int kOldFeature[kCustomizationRows] = {1, 2, 2, 3, 4, 4, 1, 0, 0, 0};
        static constexpr bool kOldColor[kCustomizationRows] = {false, false, true, false, false, true, true,
                                                               false, false, false};
        if (kOldFeature[row]) feature_ = kOldFeature[row];
        step_old_model(a, up, kOldColor[row]);
        return Result::None;
    }
    return Result::None;
}

} // namespace hailstone::game
