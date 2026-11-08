// The player customization window's rules (game/player_customization.h) against the original's control flow
// (CPlayerCustomizationWnd: 0x715770, 0x714a40, 0x714850, WndNotification).

#include <doctest/doctest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/char_create_look.h"
#include "game/player_customization.h"

using namespace hailstone::game;

namespace {

// A player whose model takes the features listed in `takes` ({type, id}) and every colour in range.
struct FakeActor final : CustomizationActor {
    std::int32_t race_ = 1, profile_race_ = 1;
    std::uint8_t gender_ = 0, class_ = 1;
    bool luclin = true, new_style = false;
    FacialFeatures f;
    std::set<std::pair<int, int>> takes;
    std::vector<std::pair<int, int>> fheb_calls, color_calls, new_model_calls;
    CustomizationCounts c;
    int applied = 0;

    bool has_actor() override { return true; }
    bool luclin_model() override { return luclin; }
    bool new_style_model() override { return new_style; }
    std::int32_t race() override { return race_; }
    std::int32_t profile_race() override { return profile_race_; }
    std::uint8_t gender() override { return gender_; }
    std::uint8_t class_id() override { return class_; }
    FacialFeatures& features() override { return f; }
    std::uint32_t set_fheb(std::uint8_t id, std::uint8_t type) override {
        fheb_calls.emplace_back(type, id);
        if (!takes.count({type, id})) return 0;
        if (type == 1) f.face = id;
        else if (type == 2) f.hair_style = id;
        else if (type == 3) f.eye1 = f.eye2 = id;
        else if (type == 4) f.beard = id;
        return 1;
    }
    std::uint32_t set_fheb_color(std::uint8_t color, std::uint8_t type) override {
        color_calls.emplace_back(type, color);
        if (type == 2) f.hair_color = color;
        else if (type == 4) f.beard_color = color;
        return color < 0x12 ? 1 : 0;
    }
    void set_facial_feature_new_model(std::int32_t value, std::int32_t type) override {
        new_model_calls.emplace_back(type, value);
        if (type == 1) f.face = static_cast<std::uint8_t>(value);
    }
    void apply_facial_features() override { ++applied; }
    void set_heritage(std::int32_t h) override { f.heritage = h; }
    void swap_face(std::uint8_t face) override { f.face = face; }
    CustomizationCounts counts() override { return c; }
    std::vector<std::int32_t> heritages() override { return {0, 1, 2}; }
    std::string heritage_name() override { return "Heritage"; }
    std::int32_t random() override { return 5; }
    std::int32_t random_low_high(std::int32_t low, std::int32_t) override { return low; }
};

} // namespace

TEST_CASE("player customization: CanBeBald and CanHaveFacialHair by race and gender") {
    CHECK(can_be_bald(1, 0) == 1);
    CHECK(can_be_bald(3, 1) == 2);   // Erudite: the scalp
    CHECK(can_be_bald(128, 0) == 0); // Iksar, Vah Shir, Froglok
    CHECK(can_be_bald(130, 1) == 0);
    CHECK(can_be_bald(9, 0) == 0);   // male Troll
    CHECK(can_be_bald(9, 1) == 1);   // female Troll
    CHECK(can_have_facial_hair(1, 0));
    CHECK_FALSE(can_have_facial_hair(1, 1));
    CHECK(can_have_facial_hair(10, 1)); // female Ogre
}

TEST_CASE("player customization: the rows of a Luclin model (sub_00715770)") {
    PlayerCustomizationModel m;
    FakeActor a;
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, false, refusal));
    CHECK(m.feature() == 1);
    CHECK(m.camera_zoom() == 100);
    CHECK_FALSE(m.row_enabled(static_cast<int>(CustomizationRow::Face))); // left as the XML has it
    CHECK(*m.row_enabled(static_cast<int>(CustomizationRow::Eyes)));
    CHECK(*m.row_enabled(static_cast<int>(CustomizationRow::Hair)));
    CHECK(*m.row_enabled(static_cast<int>(CustomizationRow::HairColor)));
    CHECK(*m.row_enabled(static_cast<int>(CustomizationRow::Beard)));
    CHECK_FALSE(*m.row_enabled(static_cast<int>(CustomizationRow::Tattoo)));
    CHECK_FALSE(*m.row_enabled(static_cast<int>(CustomizationRow::Heritage)));
    CHECK_FALSE(*m.row_enabled(static_cast<int>(CustomizationRow::FacialAttachment)));
    CHECK(m.heritage_text() == "Heritage");

    PlayerCustomizationModel b;
    FakeActor barbarian;
    barbarian.race_ = barbarian.profile_race_ = 2;
    REQUIRE(b.about_to_show(barbarian, false, false, refusal));
    CHECK(*b.row_enabled(static_cast<int>(CustomizationRow::Tattoo)));

    PlayerCustomizationModel o;
    FakeActor old;
    old.luclin = false;
    REQUIRE(o.about_to_show(old, false, false, refusal));
    CHECK_FALSE(*o.row_enabled(static_cast<int>(CustomizationRow::Eyes)));
    CHECK_FALSE(*o.row_enabled(static_cast<int>(CustomizationRow::Hair)));
}

TEST_CASE("player customization: AboutToShow's refusals") {
    PlayerCustomizationModel m;
    FakeActor a;
    a.race_ = 75; // an illusion of a race without customization
    std::uint32_t refusal = 0;
    CHECK_FALSE(m.about_to_show(a, false, false, refusal));
    CHECK(refusal == 4035);
    FakeActor b;
    CHECK_FALSE(m.about_to_show(b, true, false, refusal)); // the body tint window is open
    CHECK(refusal == 0);
}

TEST_CASE("player customization: stepping a Luclin model's features (sub_00714a40)") {
    PlayerCustomizationModel m;
    FakeActor a;
    a.takes = {{2, 0}, {2, 1}, {2, 2}, {1, 3}, {1, 4}};
    a.f.hair_style = 0;
    a.f.hair_color = 5;
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, false, refusal));
    // Hair up: style 1 takes, then the hair colour is put on it again.
    m.click(a, "Hair_Up_Button");
    CHECK(a.f.hair_style == 1);
    CHECK(m.selection().hair_style == 1);
    REQUIRE_FALSE(a.color_calls.empty());
    CHECK(a.color_calls.back() == std::pair<int, int>{2, 5});
    // Face up from 3: 4 takes.
    a.f.face = 3;
    m = PlayerCustomizationModel{};
    REQUIRE(m.about_to_show(a, false, false, refusal));
    m.click(a, "Face_Up_Button");
    CHECK(a.f.face == 4);
    // Face down from 4: 3 takes.
    m.click(a, "Face_Down_Button");
    CHECK(a.f.face == 3);
    // Eyes up from the top (20) start over at 1 (the eyes' bottom; Froglok 0); both eyes take it.
    a.takes.insert({3, 1});
    a.f.eye1 = a.f.eye2 = 20;
    m = PlayerCustomizationModel{};
    REQUIRE(m.about_to_show(a, false, false, refusal));
    m.click(a, "Eyes_Up_Button");
    CHECK(m.selection().eye2 == 1);
    CHECK(m.selection().eye1 == 1);
    CHECK(a.f.eye2 == 1);
    // Hair colour up: the first colour the model takes.
    m.click(a, "Hair_Color_Up_Button");
    CHECK(a.f.hair_color == 6);
}

TEST_CASE("player customization: new-style models step through the customization counts (sub_00714850)") {
    PlayerCustomizationModel m;
    FakeActor a;
    a.race_ = a.profile_race_ = 522; // Drakkin
    a.new_style = true;
    a.luclin = false;
    a.c.faces = 7;
    a.c.hair_colors = 4;
    a.c.hair_styles = 3;
    a.f.face = 6;
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, false, refusal));
    CHECK(*m.row_enabled(static_cast<int>(CustomizationRow::Face)));
    CHECK_FALSE(*m.row_enabled(static_cast<int>(CustomizationRow::Eyes))); // no eye colours listed
    CHECK(*m.row_enabled(static_cast<int>(CustomizationRow::HairColor)));
    CHECK_FALSE(*m.row_enabled(static_cast<int>(CustomizationRow::Heritage))); // only before the world
    m.click(a, "Face_Up_Button"); // 6 is the last: wraps to 0
    REQUIRE_FALSE(a.new_model_calls.empty());
    CHECK(a.new_model_calls.back() == std::pair<int, int>{1, 0});
    m.click(a, "Face_Down_Button"); // 0: wraps to the last
    CHECK(a.new_model_calls.back() == std::pair<int, int>{1, 6});
    m.click(a, "TattooIndex_Up_Button"); // tattoos zoom out to the whole model
    CHECK(m.camera_zoom() == 0);
    CHECK(a.new_model_calls.back().first == 9);
}

TEST_CASE("player customization: Cancel puts the features back, Accept asks for the packet") {
    PlayerCustomizationModel m;
    FakeActor a;
    a.takes = {{2, 0}, {2, 1}, {2, 2}, {1, 2}};
    a.f.face = 2;
    a.f.hair_style = 0;
    a.f.eye1 = a.f.eye2 = 4;
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, false, refusal));
    m.click(a, "Hair_Up_Button");
    CHECK(a.f.hair_style == 1);
    a.f.eye1 = 9;
    CHECK(m.click(a, "Cancel_Button") == PlayerCustomizationModel::Result::Cancel);
    CHECK(a.f.hair_style == 0);
    CHECK(a.f.eye1 == 4);
    CHECK(a.f.face == 2);
    CHECK(a.applied > 0);
    CHECK(m.click(a, "Accept_Button") == PlayerCustomizationModel::Result::Accept);
}

TEST_CASE("player customization: Random on a Luclin model leaves the window's copies (WndNotification)") {
    // SetDefaultFacialFeaturesByFace(Random() % 8, 1, 1) and ApplyFacialFeatures, no ReadLocalPlayerFeatures: the
    // next Face step goes on from the face the window had read, not from the random one.
    PlayerCustomizationModel m;
    FakeActor a;
    a.race_ = a.profile_race_ = 130; // Vah Shir
    for (int face = 0; face < 8; ++face) a.takes.insert({1, face});
    a.f.face = 2;
    a.f.hair_style = a.f.beard = a.f.hair_color = a.f.beard_color = 0xff;
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, true, refusal));
    m.click(a, "Random_Button"); // FakeActor::random() is 5
    CHECK(a.f.face == 5);
    CHECK(m.selection().face == 2);
    m.click(a, "Face_Up_Button");
    CHECK(a.f.face == 3);
    CHECK(m.selection().face == 3);
}

TEST_CASE("player customization: a Luclin face steps through 0..7 (0..9 for a Froglok) and wraps") {
    for (std::int32_t race : {130, 128, 330}) {
        CAPTURE(race);
        PlayerCustomizationModel m;
        FakeActor a;
        a.race_ = a.profile_race_ = race;
        const int faces = race == 330 ? 10 : 8;
        for (int face = 0; face < faces; ++face) a.takes.insert({1, face});
        a.f.face = 6;
        std::uint32_t refusal = 0;
        REQUIRE(m.about_to_show(a, false, true, refusal));
        for (int i = 1; i <= faces; ++i) {
            m.click(a, "Face_Up_Button");
            CHECK(a.f.face == (6 + i) % faces);
            CHECK(m.selection().face == a.f.face);
        }
        m.click(a, "Face_Down_Button");
        CHECK(a.f.face == 5);
    }
}

TEST_CASE("character creation: the face shown is the face kept and sent") {
    // CCharacterCreation's look on the model, the customization window's steps on it, its Accept
    // (CopyAppearanceFrom), then a gender switch (ApplyNewPCToPlayer): Send_OP_CharacterCreate reads the model's face,
    // which is the one the window showed last.
    FakeActor a;
    a.race_ = a.profile_race_ = 130;
    for (int face = 0; face < 8; ++face) a.takes.insert({1, face});
    CharCreateLook look;
    look.race_changed(a); // Random() 5: face 5
    CHECK(a.f.face == 5);
    PlayerCustomizationModel m;
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, true, refusal));
    m.click(a, "Face_Up_Button");
    m.click(a, "Face_Up_Button");
    m.click(a, "Face_Up_Button"); // 5 -> 6 -> 7 -> 0
    CHECK(a.f.face == 0);
    REQUIRE(m.click(a, "Accept_Button") == PlayerCustomizationModel::Result::Accept);
    look.copy_appearance_from(a.f);
    a.gender_ = 1;
    look.gender_changed(a);
    CHECK(a.f.face == 0);
    CHECK(look.character().face == 0);
}

TEST_CASE("player customization: the camera calls (AboutToShow's face camera, sub_00714850's zoom and face)") {
    PlayerCustomizationModel m;
    FakeActor a;
    a.race_ = a.profile_race_ = 330; // a Froglok: AboutToShow raises its camera
    std::uint32_t refusal = 0;
    REQUIRE(m.about_to_show(a, false, false, refusal));
    auto calls = m.take_camera_calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0] == CustomizationCameraCall{true, 100, true});
    CHECK(m.take_camera_calls().empty());
    m.click(a, "Face_Up_Button"); // an old model's step: no camera call
    CHECK(m.take_camera_calls().empty());

    // A new-style model: each face step puts the face camera back; the tattoos zoom out, the next face step in.
    PlayerCustomizationModel n;
    FakeActor d;
    d.race_ = d.profile_race_ = 522;
    d.new_style = true;
    d.luclin = false;
    d.c.faces = 7;
    d.c.tattoos = 3;
    d.c.hair_styles = 3;
    REQUIRE(n.about_to_show(d, false, false, refusal));
    n.take_camera_calls();
    n.click(d, "Face_Up_Button");
    n.click(d, "Face_Up_Button");
    calls = n.take_camera_calls();
    REQUIRE(calls.size() == 2);
    CHECK(calls[0] == CustomizationCameraCall{true, 100, false});
    CHECK(calls[1] == CustomizationCameraCall{true, 100, false});
    n.click(d, "TattooIndex_Up_Button");
    calls = n.take_camera_calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0] == CustomizationCameraCall{false, 0, false});
    n.click(d, "Hair_Up_Button"); // back in, without the face camera (hair)
    calls = n.take_camera_calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0] == CustomizationCameraCall{false, 100, false});
    n.click(d, "TattooIndex_Up_Button");
    n.click(d, "Face_Up_Button");
    calls = n.take_camera_calls();
    REQUIRE(calls.size() == 3);
    CHECK(calls[1] == CustomizationCameraCall{false, 100, false});
    CHECK(calls[2] == CustomizationCameraCall{true, 100, false});
}
