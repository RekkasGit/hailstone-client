#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <limits>

#include "assets/character.h"

using namespace hailstone::assets;

namespace {
std::array<std::uint32_t, 10> words(const BonePose& p) {
    return {std::bit_cast<std::uint32_t>(p.rotation.w), std::bit_cast<std::uint32_t>(p.rotation.x),
            std::bit_cast<std::uint32_t>(p.rotation.y), std::bit_cast<std::uint32_t>(p.rotation.z),
            std::bit_cast<std::uint32_t>(p.translation.x), std::bit_cast<std::uint32_t>(p.translation.y),
            std::bit_cast<std::uint32_t>(p.translation.z), std::bit_cast<std::uint32_t>(p.scale.x),
            std::bit_cast<std::uint32_t>(p.scale.y), std::bit_cast<std::uint32_t>(p.scale.z)};
}
}

TEST_CASE("animation key copies share storage and detach on builder mutation") {
    BonePose a, b;
    b.translation.x = 5;
    AnimationTrack original{a, b};
    auto copy = original;
    CHECK(copy.storage_id() == original.storage_id());
    copy.push_back(copy.front()); // argument aliases the storage being detached
    CHECK(copy.storage_id() != original.storage_id());
    CHECK(copy.size() == 3);
    CHECK(original.size() == 2);
    CHECK(words(copy.back()) == words(a));
    copy.clear();
    CHECK(copy.empty());
    CHECK(original.size() == 2);
    auto resized = original;
    resized.reserve(8);
    resized.resize(4);
    CHECK(resized.size() == 4);
    CHECK(original.size() == 2);
    CHECK(words(resized[1]) == words(b));
    CHECK(words(resized[2]) == words(a));
    auto moved = std::move(original);
    CHECK(original.empty());
    CHECK(moved.size() == 2);
    original.push_back(a);
    CHECK(original.size() == 1);
}

TEST_CASE("animation aliases and identical tracks share keys without losing timing metadata") {
    CharacterModel m;
    AnimationClip take;
    take.frame_ms = 75;
    take.frame_count = 2;
    take.borrowed = true;
    BonePose a, b;
    b.translation.x = 3;
    take.tracks = {{a, b}};
    m.clips["C01A"] = take;
    m.clips["C01"] = take;
    AnimationClip separate = take;
    separate.frame_ms = 50;
    separate.tracks[0] = AnimationTrack{a, b};
    m.clips["C01B"] = separate;
    m.held_aliases["C01"] = "C01B";
    CHECK(m.clips.at("C01").tracks[0].storage_id() == m.clips.at("C01A").tracks[0].storage_id());
    CHECK(m.clips.at("C01B").tracks[0].storage_id() != m.clips.at("C01A").tracks[0].storage_id());
    m.compact_animations();
    CHECK(m.clip("C01")->tracks[0].storage_id() == m.clip("C01B")->tracks[0].storage_id());
    REQUIRE(m.clip("C01", true));
    CHECK(m.clip("C01", true) == &m.clips.at("C01B"));
    CHECK(m.clip("C01")->frame_ms == 75);
    CHECK(m.clip("C01")->frame_count == 2);
    CHECK(m.clip("C01")->borrowed);
    CHECK(m.clip("C01")->duration_seconds() == doctest::Approx(0.15));
    CHECK(m.clip("C01", true)->frame_ms == 50);
    CHECK(m.clip("C01", true)->duration_seconds() == doctest::Approx(0.10));
}

TEST_CASE("constant key compaction preserves sampled poses and shorter track loop periods") {
    CharacterModel m;
    AnimationClip clip;
    clip.frame_ms = 70;
    clip.frame_count = 9;
    BonePose a, b, constant;
    b.translation = {3, 4, 5};
    b.rotation = glm::quat(0.8f, 0.2f, 0.1f, 0.3f);
    // More than one identical non-unit quaternion still takes blend()/normalize in the original sampler.
    constant.rotation = glm::quat(2, 0, 0, 0);
    constant.translation = {1, 2, 3};
    AnimationTrack four, seven;
    for (int i = 0; i < 4; ++i) four.push_back(constant);
    for (int i = 0; i < 7; ++i) seven.push_back(constant);
    clip.tracks = {{a, b}, four, seven};
    m.clips["L01"] = clip;
    const auto bytes_before = m.retained_bytes();
    auto before = m;
    m.compact_animations();
    CHECK(m.retained_bytes() < bytes_before);
    const auto& compacted = m.clips.at("L01");
    CHECK(compacted.tracks[1].stored_keys() == 1);
    CHECK(compacted.tracks[1].size() == 4);
    CHECK(compacted.tracks[2].size() == 7);
    CHECK(compacted.tracks[1].storage_id() == compacted.tracks[2].storage_id());
    CHECK(compacted.frame_count == 9);
    for (bool loop : {false, true})
        for (float t : {-1.0f, 0.0f, 0.001f, 0.035f, 0.07f, 0.135f, 0.28f, 0.49f, 0.63f, 1.5f,
                        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            std::vector<BonePose> expected, actual;
            before.sample_local(before.clips.at("L01"), t, loop, expected);
            m.sample_local(compacted, t, loop, actual);
            REQUIRE(actual.size() == expected.size());
            for (std::size_t i = 0; i < actual.size(); ++i) CHECK(words(actual[i]) == words(expected[i]));
        }
    auto mutated = compacted.tracks[1];
    mutated.push_back(b);
    CHECK(mutated.size() == 5);
    CHECK(mutated.stored_keys() == 5);
    for (std::size_t i = 0; i < 4; ++i) CHECK(words(mutated[i]) == words(constant));
    CHECK(words(mutated.back()) == words(b));
    CHECK(compacted.tracks[1].stored_keys() == 1);
    CHECK(compacted.tracks[1].size() == 4);
}

TEST_CASE("animation compaction keeps signed zeros and quaternion signs distinct") {
    CharacterModel m;
    BonePose positive, negative_zero, negative_quaternion;
    negative_zero.translation.x = -0.0f;
    negative_quaternion.rotation.w = -1;
    AnimationClip clip;
    clip.frame_count = 2;
    clip.tracks = {{positive, negative_zero}, {positive}, {negative_zero}, {negative_quaternion}};
    m.clips["P01"] = clip;
    m.compact_animations();
    const auto& t = m.clips.at("P01").tracks;
    CHECK(t[0].stored_keys() == 2);
    CHECK(t[1].storage_id() != t[2].storage_id());
    CHECK(t[1].storage_id() != t[3].storage_id());
    CHECK(words(t[2].front()) == words(negative_zero));
}

TEST_CASE("model cache diagnostics count copied animation storage only once") {
    CharacterLibrary lib;
    auto shared = std::make_shared<SharedCharacterCache>();
    lib.set_shared_cache(shared);
    auto model = std::make_shared<CharacterModel>();
    BonePose a, b;
    b.translation.x = 5;
    model->clips["L01"].tracks = {{a, b}};
    auto copy = std::make_shared<CharacterModel>(*model);
    shared->models["A"] = {model, {}, 1};
    shared->models["B"] = {copy, {}, 1};
    AnimationTrack::StorageSet seen;
    const auto shared_bytes = model->clips.at("L01").tracks[0].storage_bytes(seen);
    const auto stats = lib.cache_memory();
    CHECK(stats.models == 2);
    CHECK(stats.model_bytes == model->retained_bytes() + copy->retained_bytes() - shared_bytes);
    CHECK(stats.model_animation_bytes == 2 * model->memory_breakdown().animation_bytes - shared_bytes);
    CHECK(stats.model_bytes == stats.model_animation_bytes + stats.model_geometry_bytes + stats.model_other_bytes);
}
