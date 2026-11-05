#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "assets/character.h"
#include "game_files.h"
#include "render/actor_renderer.h"

using namespace nmsc;
using namespace nmsc::assets;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

// A root and one child bone `name` at (1, 0, 2), at rest.
CharacterModel model_with(std::string code, std::vector<std::string> names) {
    CharacterModel m;
    m.code = std::move(code);
    m.bone_names = std::move(names);
    m.parents.assign(m.bone_names.size(), 0);
    m.parents[0] = -1;
    for (std::uint32_t i = 0; i < m.bone_names.size(); ++i) m.order.push_back(i);
    AnimationClip rest;
    rest.tracks.push_back({BonePose{}});
    for (std::size_t i = 1; i < m.bone_names.size(); ++i)
        rest.tracks.push_back({BonePose{glm::quat(1, 0, 0, 0), glm::vec3(1, 0, 2), glm::vec3(1.0f)}});
    m.clips.emplace("", rest);
    return m;
}

} // namespace

TEST_CASE("a mount's rider dag: <rider>_MOUNT01, else CHEST03_MOUNT01, else <code>RIDER") {
    // SetBone (EQGraphicsDX9 @0x10041140) keeps the dag's bone when the name is not found, so CHEST03_MOUNT01
    // (set first by PlayerClient_AttachRiderToMount) stays when there is no "<rider>_MOUNT01".
    CHECK(model_with("HRS", {"ROOT_BONE", "CHEST03_MOUNT01", "HUM_MOUNT01"}).mount_point_bone("HUM") == 2);
    CHECK(model_with("HRS", {"ROOT_BONE", "CHEST03_MOUNT01", "HUM_MOUNT01"}).mount_point_bone("OGM") == 1);
    CHECK(model_with("HSM", {"HSM_TRACK", "HSMRIDER_TRACK"}).mount_point_bone("OGM") == 1);
    CHECK(model_with("HSM", {"HSM_TRACK", "HSMNECK_TRACK"}).mount_point_bone("OGM") == -1);
}

TEST_CASE("a rider is drawn in its mount's bone frame, without its own scale") {
    auto horse = model_with("HRS", {"ROOT_BONE", "CHEST03_MOUNT01"});
    auto wld_rider = model_with("OGM", {"OGM_TRACK"});
    auto eqg_rider = model_with("DKM", {"ROOT_BONE"});
    eqg_rider.eqg = true;

    render::ActorDraw mount;
    mount.model = &horse;
    mount.clip = horse.clip("");
    mount.position = glm::vec3(10.0f, 0.0f, 0.0f);
    mount.scale = 2.0f;
    glm::mat4 dag;
    REQUIRE(render::ActorRenderer::bone_world(mount, 1, dag));
    CHECK(near(glm::vec3(dag[3]), {12.0f, 0.0f, 4.0f}));

    // The bone's world matrix becomes the rider model's world matrix (CHierarchicalActor @0x10041680, model
    // vf+0x10 @0x1004a6a0): the mount's scale 2 applies, the rider's own 1.76 does not.
    render::ActorDraw rider;
    rider.model = &wld_rider;
    rider.clip = wld_rider.clip("");
    rider.scale = 1.76f;
    REQUIRE(render::ActorRenderer::seat_rider(mount, rider, nullptr));
    CHECK(rider.attached);
    glm::mat4 root;
    REQUIRE(render::ActorRenderer::bone_world(rider, 0, root));
    for (int c = 0; c < 4; ++c) CHECK(near(glm::vec3(root[c]), glm::vec3(dag[c])));

    // An EQG rider's ROOT_BONE keys come down by its ROffset (3.125 without moddat.ini) in its model units, which
    // the mount's scale then sizes.
    render::ActorDraw eqg;
    eqg.model = &eqg_rider;
    eqg.clip = eqg_rider.clip("");
    eqg.scale = 1.2f;
    REQUIRE(render::ActorRenderer::seat_rider(mount, eqg, nullptr));
    REQUIRE(render::ActorRenderer::bone_world(eqg, 0, root));
    CHECK(near(glm::vec3(root[3]), {12.0f, 0.0f, 4.0f - 3.125f * 2.0f}));

    // A mount without the dag leaves the rider unseated.
    auto plain = model_with("HSM", {"HSM_TRACK", "HSMNECK_TRACK"});
    render::ActorDraw other = mount;
    other.model = &plain;
    render::ActorDraw standing;
    standing.model = &wld_rider;
    standing.clip = wld_rider.clip("");
    CHECK_FALSE(render::ActorRenderer::seat_rider(other, standing, nullptr));
    CHECK_FALSE(standing.attached);
}

TEST_CASE("the shipped mounts' rider dags" * doctest::skip(!nmsc::test::game_dir())) {
    auto lib = CharacterLibrary::load(*nmsc::test::game_dir(), "");
    // The Luclin horse names its rider dag after itself; the EQG mounts have CHEST03_MOUNT01.
    for (const char* code : {"HSM", "HSN"}) {
        auto m = lib.model(code);
        REQUIRE(m);
        int b = m->mount_point_bone("HUM");
        REQUIRE(b >= 0);
        CHECK(m->bone_names[static_cast<std::size_t>(b)] == std::string(code) + "RIDER_TRACK");
    }
    for (const char* code : {"HRS", "CWR", "UNM", "MWR"}) {
        auto m = lib.model(code);
        REQUIRE(m);
        int b = m->mount_point_bone("HUM");
        REQUIRE(b >= 0);
        CHECK(m->bone_names[static_cast<std::size_t>(b)] == "CHEST03_MOUNT01");
    }
}
