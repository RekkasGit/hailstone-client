// rof2decomp batch 254's vectors (recorded from the original eqgame.exe): the /who class text, the target types
// that need a target, the chat garbler and bad word filter, GetPlayerFromPartialName and /follow's step.

#include <doctest/doctest.h>

#include <bit>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

#include "game/chat_text.h"
#include "game/eq_strings.h"
#include "game/follow.h"
#include "game/social.h"
#include "game/spells.h"
#include "vector_files.h"

using nmsc::test::each_vector;
using nmsc::test::Json;

namespace {

// The vectors' mocked random helpers, replayed in call order (in.calls); a call of another helper than the one
// the port makes is a mismatch.
class ReplayRandom : public nmsc::game::ChatRandom {
public:
    explicit ReplayRandom(const Json& calls) : calls_(calls) {}
    std::int32_t random() override { return next("Random"); }
    std::int32_t percent_roll() override { return next("RandomPercentRoll"); }
    char random_vowel() override { return static_cast<char>(next("RandomVowel")); }
    std::int32_t random_between(std::int32_t, std::int32_t) override { return next("RandomNumberLowHigh"); }
    std::string error;
    bool all_used() const { return at_ == calls_.items.size(); }
    std::size_t used() const { return at_; }

private:
    std::int32_t next(const char* fn) {
        if (at_ >= calls_.items.size()) {
            if (error.empty()) error = std::format("extra call {} after {}", fn, at_);
            return 1;
        }
        const Json& c = calls_[at_++];
        if (c["fn"].text != fn && error.empty()) error = std::format("call {} is {}, the port made {}", at_, c["fn"].text, fn);
        return c["ret"].i32();
    }
    const Json& calls_;
    std::size_t at_ = 0;
};

std::string printable(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\x{:02x}", static_cast<unsigned char>(c));
        else out += c;
    }
    return out;
}

} // namespace

TEST_CASE("WhoClassText vectors") {
    nmsc::game::EqStrings strings;
    for (std::uint32_t id = 0x5c0; id < 0x630; ++id) strings.set(id, "#" + std::to_string(id));
    for (std::uint32_t id = 0x1670; id < 0x1730; ++id) strings.set(id, "#" + std::to_string(id));
    for (std::uint32_t id = 0x1f60; id < 0x1f80; ++id) strings.set(id, "#" + std::to_string(id));
    each_vector("WhoClassText.jsonl", [&](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::string got = nmsc::game::who_class_text(strings, in["class"].i32(), in["level"].i32());
        if (got != r["out"]["text"].text) return std::format("class {} level {}: {}, original {}", in["class"].i32(),
                                                             in["level"].i32(), got, r["out"]["text"].text);
        return "";
    });
}

TEST_CASE("EQ_Spell_TargetTypeNeedsTarget vectors") {
    each_vector("EQ_Spell_TargetTypeNeedsTarget.jsonl", [](const Json& r) -> std::string {
        auto t = static_cast<std::uint8_t>(r["in"]["target_type"].i64());
        if (nmsc::game::target_type_needs_target(t) != r["out"]["value"].truthy())
            return std::format("target type {}", t);
        return "";
    });
}

TEST_CASE("ChatText_GarbleByLanguageSkill vectors") {
    each_vector("ChatText_GarbleByLanguageSkill.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        ReplayRandom rng(in["calls"]);
        bool changed = false;
        std::string got = nmsc::game::garble_by_language_skill(in["text"].text, static_cast<std::uint8_t>(in["skill"].i64()),
                                                               in["garbling_on"].i64() != 0, rng, &changed);
        if (!rng.error.empty()) return rng.error;
        if (!rng.all_used()) return std::format("{} of the calls made", rng.used());
        if (got != r["out"]["text"].text) return std::format("'{}': '{}', original '{}'", printable(in["text"].text),
                                                             printable(got), printable(r["out"]["text"].text));
        if (changed != r["out"]["value"].truthy()) return "changed flag differs";
        return "";
    });
}

TEST_CASE("ChatText_FilterProfanityAndSlur vectors") {
    each_vector("ChatText_FilterProfanityAndSlur.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        ReplayRandom rng(in["calls"]);
        std::string got = nmsc::game::filter_profanity_and_slur(in["text"].text, static_cast<std::uint8_t>(in["skill"].i64()),
                                                                in["filter_mode"].i32(), rng);
        if (!rng.error.empty()) return rng.error + " ('" + printable(in["text"].text) + "')";
        if (!rng.all_used()) return std::format("{} of the calls made", rng.used());
        if (got != r["out"]["text"].text) return std::format("'{}': '{}', original '{}'", printable(in["text"].text),
                                                             printable(got), printable(r["out"]["text"].text));
        return "";
    });
}

TEST_CASE("GetPlayerFromPartialName vectors") {
    each_vector("GetPlayerFromPartialName.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::vector<std::string> names;
        for (const Json& n : in["spawns"].items) names.push_back(n.text);
        std::optional<std::size_t> exclude;
        if (!in["exclude"].is_null()) exclude = static_cast<std::size_t>(in["exclude"].i64());
        auto got = nmsc::game::player_from_partial_name(in["word"].text, names, exclude);
        const Json& want = r["out"]["index"];
        if (want.is_null() ? got.has_value() : (!got || *got != static_cast<std::size_t>(want.i64())))
            return std::format("'{}': {}, original {}", in["word"].text, got ? std::to_string(*got) : "none",
                               want.is_null() ? "none" : want.text);
        return "";
    });
}

TEST_CASE("FollowPlayerAI vectors") {
    using nmsc::game::mechanics::Fpu;
    using nmsc::game::mechanics::FpuPrecision;
    each_vector("FollowPlayerAI.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        nmsc::game::FollowInput f;
        f.y = in["y"].f32();
        f.x = in["x"].f32();
        f.z = in["z"].f32();
        f.heading = in["heading"].f32();
        f.height = in["height"].f32();
        f.speed = in["speed"].f32();
        f.target_y = in["target_y"].f32();
        f.target_x = in["target_x"].f32();
        f.target_z = in["target_z"].f32();
        f.target_height = in["target_height"].f32();
        f.target_speed = in["target_speed"].f32();
        f.target_animation = static_cast<std::uint8_t>(in["target_animation"].i64());
        f.distance = in["distance"].f32();
        std::optional<std::uint32_t> message;
        bool cast_ray_called = false;
        for (const Json& c : in["calls"].items) {
            if (c["fn"].text == "cast_ray") {
                cast_ray_called = true;
                f.clear_line = c["ret"].i64() != 0;
            }
            if (c["fn"].text == "message") message = c["eqstr"].u32();
        }
        Fpu fpu(r["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53);
        auto s = nmsc::game::follow_step(f, fpu);
        auto bits = [](float v) { return std::bit_cast<std::uint32_t>(v); };
        if (s.stop_message != message) return "stop message differs";
        if (!message && !cast_ray_called) return "the original did not reach the line test";
        if (bits(s.heading) != nmsc::test::f32_bits(out["heading"]))
            return std::format("heading {:.9g}, original {:.9g} ({})", s.heading, out["heading"].f32(), r["precision"].text);
        if (bits(s.bearing) != nmsc::test::f32_bits(out["bearing"]))
            return std::format("bearing {:.9g}, original {:.9g} ({})", s.bearing, out["bearing"].f32(), r["precision"].text);
        if (bits(s.distance) != nmsc::test::f32_bits(out["distance"])) return "distance differs";
        if (bits(s.speed) != nmsc::test::f32_bits(out["speed"])) return "speed differs";
        if (static_cast<int>(s.move) != out["follow_move"].i32()) return "follow_move differs";
        if (s.move && static_cast<int>(s.run) != out["follow_run"].i32()) return "follow_run differs";
        if (!s.move && out["follow_run"].i32() != 0) return "follow_run set without move";
        return "";
    });
}

TEST_CASE("msg_new_text's filters") {
    nmsc::game::ChatFilters filters;
    CHECK(filters.mode(nmsc::game::kFilterBadWord) == 1); // on by default
    CHECK(filters.mode(nmsc::game::kFilterSpam) == 1);
    CHECK(filters.mode(5) == 0);                          // ooc shown
    nmsc::game::ChatLineFacts f;
    f.channel = 5;
    CHECK(nmsc::game::chat_line_shown(f, filters));
    filters.set(5, 0);
    CHECK_FALSE(nmsc::game::chat_line_shown(f, filters));
    f.channel = 8;
    f.spam = true;
    CHECK_FALSE(nmsc::game::chat_line_shown(f, filters));
    f.spam = false;
    f.sender_ignored = true;
    CHECK_FALSE(nmsc::game::chat_line_shown(f, filters));
    f.sender_gm = true;
    CHECK(nmsc::game::chat_line_shown(f, filters));
    f = {};
    f.channel = 11;
    CHECK_FALSE(nmsc::game::chat_line_shown(f, filters));
    f.listener_gm = true;
    CHECK(nmsc::game::chat_line_shown(f, filters));
    f = {};
    f.channel = 0;
    f.guild_permission = false;
    CHECK_FALSE(nmsc::game::chat_line_shown(f, filters));
    CHECK(nmsc::game::language_prefix_eqstr(8, 24, false) == 1412);
    CHECK(nmsc::game::language_prefix_eqstr(8, 25, false) == 1411);
    CHECK(nmsc::game::language_prefix_eqstr(14, 0, false) == 1411);
}
