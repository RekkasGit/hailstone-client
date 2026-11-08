// Vehicle_WorldToLocal.jsonl, Vehicle_LocalToWorld.jsonl and
// Ladder_MountClickedLadder.jsonl (rof2decomp batch 252, recorded from
// eqgame.exe) against game/mechanics/vehicle.h and ladder.h; floats bit for
// bit at the record's x87 precision.
#include <doctest/doctest.h>

#include <bit>
#include <format>
#include <string>
#include <vector>

#include "game/mechanics/ladder.h"
#include "game/mechanics/vehicle.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using hailstone::test::Json;
using hailstone::test::f32_bits;
using hailstone::test::f32_of;

namespace {

Fpu precision_of(const Json& rec) {
    return Fpu(rec["precision"].text == "x87-53" ? FpuPrecision::Double53 : FpuPrecision::Single24);
}

std::uint32_t bits(float v) { return std::bit_cast<std::uint32_t>(v); }

std::string check_float(const char* what, float got, const Json& want) {
    if (bits(got) == f32_bits(want)) return {};
    return std::format("{}: got {} ({:08x}) want {:08x}", what, got, bits(got), f32_bits(want));
}

std::string check_vehicle(const Json& rec, bool to_local) {
    const Json& in = rec["in"];
    const Json& v = in["vehicle"];
    const Json& o = in["object"];
    VehiclePose pose{f32_of(v["x"]), f32_of(v["y"]), f32_of(v["z"]), f32_of(v["heading"]), f32_of(v["pitch"])};
    VehicleBody b{f32_of(o["x"]), f32_of(o["y"]), f32_of(o["z"]), f32_of(o["speed_x"]), f32_of(o["speed_y"]),
                  f32_of(o["speed_z"]), f32_of(o["heading"]), f32_of(o["pitch"])};
    VehicleFrame m;
    for (std::size_t i = 0; i < 9; ++i) m[i] = f32_of(in["matrix"][i]);
    Fpu fpu = precision_of(rec);
    if (to_local) vehicle_world_to_local(pose, b, m, fpu);
    else vehicle_local_to_world(pose, b, m, fpu);
    const Json& out = rec["out"];
    std::string why;
    for (auto [name, value] : {std::pair{"x", b.x}, {"y", b.y}, {"z", b.z}, {"speed_x", b.speed_x},
                               {"speed_y", b.speed_y}, {"speed_z", b.speed_z}, {"heading", b.heading},
                               {"pitch", b.pitch}}) {
        why += check_float(name, value, out[name]);
        if (!why.empty()) return why;
    }
    return {};
}

// The callees' answers come from the record's call list, in order; each call
// the port makes is checked against the next recorded one.
class RecordedLadder final : public LadderQueries {
public:
    RecordedLadder(const Json& calls, const Json& fits) : calls_(calls), fits_(fits) {
        // The first three (type, position, angles) are the ladder's fields.
        while (at_ < calls_.items.size()) {
            const std::string& fn = calls_[at_]["fn"].text;
            if (fn != "ladder_type" && fn != "ladder_position" && fn != "ladder_angles") break;
            ++at_;
        }
    }
    std::string error;
    bool done() const { return at_ == calls_.items.size(); }

    double actor_height() override { return answer("player_actor_height"); }
    double ladder_height() override { return answer("ladder_height"); }
    double sine(float h) override { return answer("sine", &h); }
    double cosine(float h) override { return answer("cosine", &h); }
    double root(float v) override { return answer("true_distance_root", &v); }
    void too_far() override { too_far_ = true; }
    bool can_i_fit_here(float x, float y, float z) override {
        const Json* c = next("can_i_fit_here_xy");
        if (c) expect_bits("fit x", x, (*c)["x_bits"]), expect_bits("fit y", y, (*c)["y_bits"]);
        c = next("can_i_fit_here_z");
        if (c) expect_bits("fit z", z, (*c)["z_bits"]);
        bool r = fits_[fit_calls_ < 2 ? fit_calls_ : 1].truthy();
        ++fit_calls_;
        return r;
    }
    void update_player_actor() override { ++actor_updates_; }
    double floor_height(float x, float y, float z, std::uint32_t flag) override {
        const Json* c = next("floor_height_xy");
        if (c) expect_bits("floor x", x, (*c)["x_bits"]), expect_bits("floor y", y, (*c)["y_bits"]);
        c = next("floor_height_z");
        if (!c) return 0;
        expect_bits("floor z", z, (*c)["z_bits"]);
        if ((*c)["flag"].u32() != flag) error += "floor flag; ";
        return static_cast<double>(std::bit_cast<float>((*c)["ret"].u32()));
    }
    bool too_far_ = false;
    int actor_updates_ = 0;

private:
    const Json* next(const char* fn) {
        if (at_ >= calls_.items.size() || calls_[at_]["fn"].text != fn) {
            error += std::format("call {}: {} expected, record has {}; ", at_, fn,
                                 at_ < calls_.items.size() ? calls_[at_]["fn"].text : "nothing");
            return nullptr;
        }
        return &calls_[at_++];
    }
    void expect_bits(const char* what, float got, const Json& want) {
        if (bits(got) != want.u32()) error += std::format("{} {:08x} want {:08x}; ", what, bits(got), want.u32());
    }
    double answer(const char* fn, const float* arg = nullptr) {
        const Json* c = next(fn);
        if (!c) return 0;
        if (arg) expect_bits(fn, *arg, (*c)["arg_bits"]);
        return static_cast<double>(std::bit_cast<float>((*c)["ret"].u32()));
    }
    const Json& calls_;
    const Json& fits_;
    std::size_t at_ = 0;
    int fit_calls_ = 0;
};

LadderPlayer ladder_player(const Json& j, float avatar) {
    LadderPlayer p;
    p.x = f32_of(j["x"]);
    p.y = f32_of(j["y"]);
    p.z = f32_of(j["z"]);
    p.heading = f32_of(j["heading"]);
    p.movement_mode = j["movement_mode"].i32();
    p.speed_x = f32_of(j["speed_x"]);
    p.speed_y = f32_of(j["speed_y"]);
    p.speed_z = f32_of(j["speed_z"]);
    p.speed_run = f32_of(j["speed_run"]);
    p.pitch = f32_of(j["pitch"]);
    p.floor_height = f32_of(j["floor_height"]);
    p.standing_on_vehicle = j["standing_on_set"].truthy();
    p.field_2d4 = j["field_2d4"].i32();
    p.avatar_height = avatar;
    return p;
}

std::string check_ladder(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    const Json& l = in["ladder"];
    LadderSwitch ladder{l["type"].i32(), f32_of(l["x"]), f32_of(l["y"]), f32_of(l["z"]), f32_of(l["heading"]),
                        f32_of(l["height"])};
    LadderPlayer p = ladder_player(in["player"], f32_of(in["avatar_height"]));
    RecordedLadder q(out["calls"], in["fits"]);
    mount_clicked_ladder(ladder, p, q, precision_of(rec));
    if (!q.error.empty()) return q.error;
    if (!q.done()) return "calls left over";
    // The mocked calls: chat (too far), the fit answers and the actor update.
    bool chat = false;
    int updates = 0;
    for (const Json& c : out["mocked_calls"].items) {
        if (c["fn"].text == "chat") chat = true;
        if (c["fn"].text == "update_player_actor") ++updates;
    }
    if (chat != q.too_far_) return "too-far message";
    if (updates != q.actor_updates_) return "actor updates";
    LadderPlayer w = ladder_player(out["player"], p.avatar_height);
    std::string why;
    for (auto [name, got, want] :
         {std::tuple{"x", p.x, w.x}, {"y", p.y, w.y}, {"z", p.z, w.z}, {"heading", p.heading, w.heading},
          {"speed_x", p.speed_x, w.speed_x}, {"speed_y", p.speed_y, w.speed_y}, {"speed_z", p.speed_z, w.speed_z},
          {"speed_run", p.speed_run, w.speed_run}, {"pitch", p.pitch, w.pitch},
          {"floor_height", p.floor_height, w.floor_height}}) {
        if (bits(got) != bits(want)) return std::format("{}: got {:08x} want {:08x}", name, bits(got), bits(want));
    }
    if (p.movement_mode != w.movement_mode) return "movement_mode";
    if (p.standing_on_vehicle != w.standing_on_vehicle) return "standing_on";
    if (p.field_2d4 != w.field_2d4) return "field_2d4";
    return {};
}

} // namespace

TEST_CASE("vectors: Vehicle_WorldToLocal (a passenger into the vehicle's frame)") {
    hailstone::test::each_vector("Vehicle_WorldToLocal.jsonl", [](const Json& r) { return check_vehicle(r, true); });
}

TEST_CASE("vectors: Vehicle_LocalToWorld (a passenger back into the world)") {
    hailstone::test::each_vector("Vehicle_LocalToWorld.jsonl", [](const Json& r) { return check_vehicle(r, false); });
}

TEST_CASE("vectors: Ladder_MountClickedLadder") {
    hailstone::test::each_vector("Ladder_MountClickedLadder.jsonl", check_ladder);
}

TEST_CASE("vehicle frame: to local and back keeps a passenger's place") {
    VehiclePose boat{100.0f, -50.0f, 10.0f, 128.0f, 0.0f};
    VehicleBody b{110.0f, -40.0f, 12.0f, 0, 0, 0, 200.0f, 0.0f};
    VehicleBody local = b;
    vehicle_to_local(boat, local);
    VehicleBody back = local;
    vehicle_to_world(boat, back);
    CHECK(back.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(back.y == doctest::Approx(b.y).epsilon(1e-4));
    CHECK(back.z == doctest::Approx(b.z).epsilon(1e-4));
    CHECK(back.heading == doctest::Approx(b.heading));
}
