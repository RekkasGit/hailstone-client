// Character select and creation's model player: the camera (game/char_select_camera.h, EQCamera_User and
// CDisplay::SetCCreateCamera @0x4950f0), ActorClient::SetHeritage @0x40f2f0 and CCharacterCreation's look
// (game/char_create_look.h), and moddat.ini's creation camera keys.

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "game/char_create_look.h"
#include "game/char_select_camera.h"
#include "game/player_customization.h"
#include "game/spawn_heights.h"

using namespace nmsc::game;

namespace {

struct Actor final : CustomizationActor {
    std::int32_t race_ = 1;
    std::uint8_t gender_ = 0, class_ = 1;
    bool luclin = true, new_style = false;
    FacialFeatures f;
    std::vector<std::int32_t> list{3, 5};
    std::vector<std::int32_t> draws; // what random() hands out, in turn (then 0)
    std::size_t next = 0;
    int applied = 0;
    CustomizationCounts c{};

    bool has_actor() override { return true; }
    bool luclin_model() override { return luclin; }
    bool new_style_model() override { return new_style; }
    std::int32_t race() override { return race_; }
    std::int32_t profile_race() override { return race_; }
    std::uint8_t gender() override { return gender_; }
    std::uint8_t class_id() override { return class_; }
    FacialFeatures& features() override { return f; }
    std::uint32_t set_fheb(std::uint8_t, std::uint8_t) override { return 1; }
    std::uint32_t set_fheb_color(std::uint8_t, std::uint8_t) override { return 1; }
    void set_facial_feature_new_model(std::int32_t, std::int32_t) override {}
    void apply_facial_features() override { ++applied; }
    void set_heritage(std::int32_t h) override { f.heritage = h; }
    void swap_face(std::uint8_t face) override { f.face = face; }
    CustomizationCounts counts() override { return c; }
    std::vector<std::int32_t> heritages() override { return list; }
    std::string heritage_name() override { return ""; }
    std::int32_t random() override { return next < draws.size() ? draws[next++] : 0; }
    std::int32_t random_low_high(std::int32_t low, std::int32_t) override { return low; }
};

} // namespace

TEST_CASE("character select camera: the sine table and the whole-model placement (mode 0)") {
    CHECK(table_sin(128.0f) == 1.0f);
    CHECK(table_cos(256.0f) == -1.0f);
    CHECK(table_sin(0.0f) == 0.0f);
    CHECK(table_cos(511.9f) == doctest::Approx(std::cos(511.0 * 3.14159265358979 / 256.0)));

    UserCamera cam;
    CCreateInputs in;
    in.race = 1;
    in.size = 6.0f;
    in.view_height = 5.72307682f;
    CCreateResult r = set_ccreate_camera(cam, 0, in);
    REQUIRE(r.fov);
    CHECK(*r.fov == 90.0f);
    CHECK_FALSE(r.animation);
    CHECK(cam.distance == 14.0f); // Height + 2 + 6
    CHECK(cam.height == -(in.view_height * 0.33f));
    in.race = 9; // Troll: 7
    set_ccreate_camera(cam, 0, in);
    CHECK(cam.distance == 15.0f);
}

TEST_CASE("character select camera: the face (mode 100) by model kind") {
    UserCamera cam;
    CCreateInputs in;
    in.race = 130; // Vah Shir, a Luclin model
    in.model_settings = true;
    CCreateResult r = face_camera(cam, in, true);
    CHECK_FALSE(r.fov); // 100 keeps the field of view
    REQUIRE(r.animation);
    CHECK(*r.animation == 0x47);
    CHECK(cam.distance == doctest::Approx(2.5f + 1.5f));
    CHECK(cam.height == doctest::Approx(-0.6f));
    in = {};
    in.race = 522;
    in.new_style_model = true;
    in.ccc_distance = 2.5f;
    in.ccc_height = -0.5f;
    r = set_ccreate_camera(cam, 2, in);
    REQUIRE(r.fov);
    CHECK(*r.fov == 70.0f);
    CHECK(cam.distance == 2.5f);
    CHECK(cam.height == -0.5f);
    // An old model without settings: Iksar
    in = {};
    in.race = 128;
    r = set_ccreate_camera(cam, 100, in);
    CHECK(*r.animation == 0x20);
    CHECK(cam.distance == 2.5f);
    CHECK(cam.height == 0.5f);
}

TEST_CASE("character select camera: the customization window's calls on Cameras[7]") {
    CCreateInputs in;
    in.race = 330; // Froglok, a Luclin-style model: distance 6, height -2.5
    in.model_settings = true;
    in.heading = 200.0f;
    UserCamera cam, want;
    customization_camera(cam, {true, 100, true}, in);
    face_camera(want, in, true);
    CHECK(cam.distance == want.distance);
    CHECK(cam.height == doctest::Approx(-2.5f + 1.5f)); // AboutToShow's raise
    CHECK(cam.heading == 200.0f);
    customization_camera(cam, {true, 100, false}, in); // a step's face camera: no raise
    CHECK(cam.height == doctest::Approx(-2.5f));
    in.race = 130;
    customization_camera(cam, {true, 100, true}, in); // the raise is a Froglok's only
    CHECK(cam.height == doctest::Approx(-0.6f));
    CCreateResult r = customization_camera(cam, {false, 0, false}, in); // the tattoos' zoom out
    REQUIRE(r.fov);
    CHECK(*r.fov == 90.0f);
    CHECK(cam.height == -(in.view_height * 0.33f));
    // In game (GameState 5) EQCamera_User::vf2 clamps the face camera's negative height to 0.
    customization_camera(cam, {true, 100, false}, in);
    UserCameraTarget t;
    t.position = glm::vec3(10.0f, 20.0f, 3.0f);
    t.eye_height = 4.0f;
    UserCameraPose p = user_camera_update(cam, t, true);
    CHECK(cam.height == 0.0f);
    CHECK(p.position.z == doctest::Approx(7.0f));
    CHECK(glm::distance(glm::vec2(p.position), glm::vec2(10.0f, 20.0f)) == doctest::Approx(4.0f).epsilon(0.01));
}

TEST_CASE("character select camera: EQCamera_User's place and the 50 ms steps") {
    UserCamera cam;
    cam.distance = 14.0f;
    cam.height = -1.9f;
    cam.heading = 0.0f;
    UserCameraTarget t;
    t.position = glm::vec3(0.0f, 0.0f, 3.67f);
    t.eye_height = 2.84f;
    UserCameraPose p = user_camera_update(cam, t);
    CHECK(p.position.x == doctest::Approx(14.0f));
    CHECK(p.position.y == doctest::Approx(0.0f));
    CHECK(p.position.z == doctest::Approx(3.67f + 2.84f - 1.9f));
    CHECK(p.heading == 255.75f); // looking back at the model (__get_bearing's 0..511.5 scale)
    cam.heading = 128.0f;
    p = user_camera_update(cam, t);
    CHECK(p.position.y == doctest::Approx(14.0f));
    CHECK(p.heading == 383.625f);

    cam.heading = 511.0f;
    char_select_camera_tick(cam, 100.0f);
    CHECK(cam.heading == 100.0f); // follows the model

    CCreateInputs in;
    in.race = 1;
    in.model_settings = true;
    in.heading = 40.0f;
    cam.heading = 10.0f;
    char_create_camera_tick(cam, 2, in); // the hair page: SetCCreateCamera(2) and + 1.5
    CHECK(cam.heading == doctest::Approx(11.5f));
    char_create_camera_tick(cam, 1, in);
    CHECK(cam.heading == 40.0f);

    start_network_game_camera(cam, 5.72307682f);
    CHECK(cam.heading == 456.0f);
    CHECK(cam.distance == 6.0f);
}

TEST_CASE("ActorClient::SetHeritage: a random heritage and features, or the asked one kept within the counts") {
    Actor a;
    a.c.faces = 4;
    a.c.hair_styles = 1; // below 2: no number drawn
    a.c.hair_colors = 3;
    a.c.beards = 2;
    a.c.eyes = 5;
    a.c.tattoos = 7;
    a.c.facial_attachments = 2;
    a.draws = {3, 6, 4, 5, 3, 8, 9, 11};
    actor_set_heritage(a, -1, false);
    CHECK(a.f.heritage == 5);    // 3 % 2
    CHECK(a.f.face == 2);        // 6 % 4
    CHECK(a.f.hair_style == 0);
    CHECK(a.f.hair_color == 1);  // 4 % 3
    CHECK(a.f.beard == 1);       // 5 % 2
    CHECK(a.f.beard_color == 0); // 3 % 3
    CHECK(a.f.eye1 == 3);        // 8 % 5
    CHECK(a.f.eye2 == 3);
    CHECK(a.f.tattoo == 2);      // 9 % 7
    CHECK(a.f.details == 1);     // 11 % 2
    CHECK(a.applied == 1);

    // keep: the current heritage (5) is among the class's, so 3 is taken; features within the counts stay.
    a.next = 0;
    a.draws = {};
    a.f.face = 3;
    a.f.tattoo = 9; // past 7: drawn again (0)
    actor_set_heritage(a, 3, true);
    CHECK(a.f.heritage == 3);
    CHECK(a.f.face == 3);
    CHECK(a.f.tattoo == 0);
    // the current heritage not among them: one at random instead of the one asked
    a.f.heritage = 9;
    a.draws = {1};
    a.next = 0;
    actor_set_heritage(a, 3, true);
    CHECK(a.f.heritage == 5);
    // no heritages: ApplyFacialFeatures alone
    a.list.clear();
    int before = a.applied;
    actor_set_heritage(a, -1, false);
    CHECK(a.applied == before + 1);
    CHECK(a.f.heritage == 5);
}

TEST_CASE("CCharacterCreation's look: a race's random face, a gender switch keeps the window character's") {
    Actor a;
    a.race_ = 1;
    a.luclin = false; // an old model: SetDefaultFacialFeaturesByFace leaves it
    a.draws = {13};   // 13 % 8
    CharCreateLook look;
    look.race_changed(a);
    CHECK(look.character().face == 5);
    CHECK(a.f.face == 5);
    a.f.face = 1; // the model's face changes (another gender's model), the window's character keeps 5
    look.gender_changed(a);
    CHECK(a.f.face == 5);

    // Froglok: faces 0..9
    a.race_ = 330;
    a.draws = {19};
    a.next = 0;
    look.race_changed(a);
    CHECK(look.character().face == 9);

    // A new-style classic-head model (Drakkin): CCharacterCreation_SetHeritage(-1)
    a.race_ = 522;
    a.new_style = true;
    a.list = {1, 2, 3};
    a.c = {};
    a.c.faces = 7;
    a.draws = {2, 4};
    a.next = 0;
    look.race_changed(a);
    CHECK(a.f.heritage == 3);
    CHECK(a.f.face == 4);
    CHECK(look.character().heritage == 3);
    CHECK(look.character().face == 4);

    FacialFeatures accepted;
    accepted.face = 6;
    look.copy_appearance_from(accepted);
    CHECK(look.character().face == 6);
}

TEST_CASE("moddat.ini: the creation camera's CCCDis and CCCHt") {
    HeightTables t = parse_height_tables("", "[DRM]\nROffset=3.5\nCCCDis=2.5\nCCCHt=-.5\n[DRF]\nCCCDis=x\n");
    CHECK(t.ccc_distance("drm") == 2.5f);
    CHECK(t.ccc_height("DRM") == -0.5f);
    CHECK(t.ccc_distance("DRF") == kDefaultCccDistance); // does not parse
    CHECK(t.ccc_height("HUM") == -1.0f);
    CHECK(t.roffset("DRM") == 3.5f);
}
