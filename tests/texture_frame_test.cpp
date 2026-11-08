// render/material_state.h texture_frame: which frame of an animated texture (a classic SIMPLESPRITEDEF) the zone
// and object renderers draw at a time.
#include <doctest/doctest.h>

#include "assets/eqg.h"
#include "render/material_state.h"

using namespace hailstone::render;

TEST_CASE("texture_frame steps a flame's frames one every delay and wraps") {
    // FIRE1_SPRITE: fire1..fire4 at 200 ms (ecommons_obj.wld, gequip.wld IT78).
    CHECK(texture_frame(0, 200, 4) == 0);
    CHECK(texture_frame(199, 200, 4) == 0);
    CHECK(texture_frame(200, 200, 4) == 1);
    CHECK(texture_frame(450, 200, 4) == 2);
    CHECK(texture_frame(799, 200, 4) == 3);
    CHECK(texture_frame(800, 200, 4) == 0);
    CHECK(texture_frame(1000, 200, 4) == 1);
    // Days into a session the clock still lands on a frame.
    CHECK(texture_frame(86'400'000ull * 3 + 600, 200, 4) == 3);
}

TEST_CASE("texture_frame keeps still textures on their only frame") {
    CHECK(texture_frame(12345, 0, 4) == 0);   // no delay: not animated
    CHECK(texture_frame(12345, 200, 1) == 0); // one frame
    CHECK(texture_frame(12345, 200, 0) == 0); // no frames at all (the caller's guard)
}

TEST_CASE("alpha_unused: which materials never read their texture's alpha") {
    // An EQG material as eqg_character::append_pieces builds it from its shader name.
    auto eqg = [](const char* shader) {
        hailstone::assets::Material m;
        m.blend = hailstone::assets::eqg::shader_blend(shader);
        m.eqg_shader = hailstone::assets::eqg::shader_class(shader);
        return material_state(m);
    };
    // it26526.mod (genericplateblue.eqg, the Iksar male's hero's forge chest 10801): its plate is drawn opaque from
    // generic_plate_blue_c.dds, whose alpha is 0 under the plate; its cloth by Chroma from the same texture.
    CHECK(eqg("Opaque_MaxCBSGE1.fx").alpha_unused());
    CHECK_FALSE(eqg("Chroma_MaxCBSGE1.fx").alpha_unused());
    CHECK_FALSE(eqg("Alpha_MaxCBS1.fx").alpha_unused());
    CHECK_FALSE(eqg("Opaque_AddAlphaC1Max.fx").alpha_unused());
    hailstone::assets::Material classic;
    CHECK(material_state(classic).alpha_unused());
    classic.blend = hailstone::assets::BlendMode::Masked;
    CHECK_FALSE(material_state(classic).alpha_unused());
    classic.blend = hailstone::assets::BlendMode::Transparent50;
    CHECK_FALSE(material_state(classic).alpha_unused());
}
