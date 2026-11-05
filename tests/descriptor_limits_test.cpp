// render/descriptor_limits.h: how many textures a bindless array may hold, from the device's descriptor limits.
#include <doctest/doctest.h>

#include "render/descriptor_limits.h"

using namespace nmsc::render;

TEST_CASE("descriptor limits: Intel's Windows driver, plain sets: its 64 samplers per stage, less the shadow set's") {
    // An Iris Xe (i5-1135G7) on Intel's Windows driver, as vulkaninfo gave them.
    DescriptorLimits plain{64, 200, 200, 576, 1800};
    CHECK(texture_array_limit(plain) == 64 - kSharedFragmentSamplers);
    // The particle renderer asked for 512 here, which made its pipeline fail (VK_ERROR_INITIALIZATION_FAILED).
    CHECK(texture_array_limit(plain) < 512u);
}

TEST_CASE("descriptor limits: the same driver's update-after-bind limits hold every renderer's array") {
    DescriptorLimits uab{1048576, 1048576, 1048576, 1048576, 1048576};
    CHECK(texture_array_limit(uab) == 1048576 - kSharedFragmentResources);
    CHECK(texture_array_limit(uab) >= 0xffffu); // the actors' cap (render/actor_renderer.cpp)
}

TEST_CASE("descriptor limits: the tightest of samplers, sampled images, resources and the per-set limits") {
    CHECK(texture_array_limit({1000, 300, 1000, 1000, 1000}) == 300 - kSharedFragmentSamplers);
    CHECK(texture_array_limit({1000, 1000, 100, 1000, 1000}) == 100 - kSharedFragmentResources);
    // The per-set limits cover the array's own set alone: the shadow set is another.
    CHECK(texture_array_limit({1000, 1000, 1000, 80, 1000}) == 80);
    CHECK(texture_array_limit({1000, 1000, 1000, 1000, 90}) == 90);
}

TEST_CASE("descriptor limits: never below one, even where the limits leave nothing for an array") {
    CHECK(texture_array_limit({}) == 1);
    CHECK(texture_array_limit({4, 4, 4, 4, 4}) == 1);
}
