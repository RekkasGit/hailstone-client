// The weather state (game/weather_state.h) against the original's vectors (recorded from eqgame.exe):
// Weather_HandleWeatherMessage (600), Weather_StartEffect (1500) and Weather_RequestRainSnowToggle (300).
//
// Weather_StartEffect's records hold the precipitation manager's virtual calls in order ("virtual_calls": raw 32-bit
// arguments, object 1 the existing manager, 2 one created now, 3 the resource manager, 4 the camera) and the mocked
// CDisplay::StopWeather and __EQGetTime calls. Fields the original did not write keep the recorder's random bytes, so
// the next strike / fade times, the thunder byte and the fog offsets are compared where the port writes them.
#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "game/weather_state.h"
#include "vector_files.h"

using namespace hailstone::game;
using hailstone::test::Json;
using hailstone::test::f32_bits;

namespace {

WeatherState state_of(const Json& in) {
    WeatherState w;
    w.kind = in["kind"].i32();
    w.pending = static_cast<std::uint8_t>(in["pending"].i64());
    w.active = static_cast<std::uint8_t>(in["active"].i64());
    w.on = static_cast<std::uint8_t>(in["on"].i64());
    w.pending_kind = in["pending_kind"].i32();
    w.pending_intensity = in["pending_intensity"].i32();
    w.fade = in["fade"].i32();
    return w;
}

std::string cmp_state(const Json& out, const WeatherState& w) {
    auto field = [&](const char* key, std::int64_t got) -> std::string {
        if (out[key].i64() != got)
            return std::string(key) + ": " + std::to_string(got) + " instead of " + std::to_string(out[key].i64());
        return {};
    };
    for (auto [key, got] : {std::pair<const char*, std::int64_t>{"kind", w.kind}, {"pending", w.pending},
                            {"active", w.active}, {"on", w.on}, {"pending_kind", w.pending_kind},
                            {"pending_intensity", w.pending_intensity}, {"fade", w.fade}})
        if (auto why = field(key, got); !why.empty()) return why;
    return {};
}

struct Call {
    std::string fn;
    std::int32_t object = 0, a = 0, b = 0, c = 0;
};

// Records the manager's calls as the vectors do.
class RecordingSink : public PrecipitationSink {
public:
    RecordingSink(bool has_manager, std::uint32_t camera_60, std::uint32_t camera_64)
        : object_(has_manager ? 1 : 2), camera_60_(camera_60), camera_64_(camera_64) {}
    std::vector<Call> calls;
    void create() override {
        calls.push_back({"create", 3, 7500, 0, 0});
        calls.push_back({"definition", 2, 0, 0, 0}); // a: the address of "default"
    }
    void set_kind(std::int32_t kind) override { calls.push_back({"set_kind", object_, kind, 0, 0}); }
    void wind(const std::uint32_t (&v)[3]) override {
        calls.push_back({"wind", object_, static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1]),
                         static_cast<std::int32_t>(v[2])});
    }
    std::uint32_t camera_60() override {
        calls.push_back({"camera_60", 4, 0, 0, 0});
        return camera_60_;
    }
    void camera_value_1c(std::uint32_t v) override { calls.push_back({"camera_value_1c", object_, static_cast<std::int32_t>(v), 0, 0}); }
    std::uint32_t camera_64() override {
        calls.push_back({"camera_64", 4, 0, 0, 0});
        return camera_64_;
    }
    void camera_value_28(std::uint32_t v) override { calls.push_back({"camera_value_28", object_, static_cast<std::int32_t>(v), 0, 0}); }
    void stop() override { calls.push_back({"stop", object_, 1, 1, 0}); }
    void set_intensity(std::uint32_t target, std::uint32_t time, std::uint32_t immediate) override {
        calls.push_back({"set_intensity", object_, static_cast<std::int32_t>(target), static_cast<std::int32_t>(time),
                         static_cast<std::int32_t>(immediate)});
    }

private:
    std::int32_t object_;
    std::uint32_t camera_60_, camera_64_;
};

} // namespace

TEST_CASE("Weather_HandleWeatherMessage vectors") {
    hailstone::test::each_vector("Weather_HandleWeatherMessage.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        WeatherState w = state_of(in);
        weather_handle_message(w, in["message_kind"].i32(), in["message_intensity"].i32());
        return cmp_state(r["out"], w);
    });
}

TEST_CASE("Weather_StartEffect vectors") {
    hailstone::test::each_vector("Weather_StartEffect.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        using hailstone::game::mechanics::Fpu;
        using hailstone::game::mechanics::FpuPrecision;
        const Fpu fpu(r["precision"].text == "x87-53" ? FpuPrecision::Double53 : FpuPrecision::Single24);
        WeatherState w = state_of(in);
        // Sentinels for the fields only a start writes.
        w.next_strike = static_cast<std::uint32_t>(out["next_strike_time"].i32()) ^ 0x5a5a5a5au;
        w.next_fade = static_cast<std::uint32_t>(out["next_fade_time"].i32()) ^ 0x5a5a5a5au;
        w.thunder = static_cast<std::uint8_t>(out["thunder"].i64() ^ 0x5a);
        w.fog_near = f32_bits(out["fog_offset_near"]) ^ 0x5a5a5a5au;
        w.fog_far = f32_bits(out["fog_offset_far"]) ^ 0x5a5a5a5au;
        WeatherDisplay display;
        display.frame_time = static_cast<std::uint32_t>(in["display_time"].i32());
        display.raining = static_cast<std::uint8_t>(in["display_raining"].i64());
        RecordingSink sink(in["has_manager"].truthy(), in["camera_60"].u32(), in["camera_64"].u32());
        // The mocked calls, in order.
        std::vector<Call> calls;
        std::uint32_t now = 0;
        for (const Json& c : out["calls"].items)
            if (c["fn"].text == "eq_get_time") now = c["ret"].u32();
        StartEffectEnv env;
        env.has_manager = in["has_manager"].truthy();
        env.now = now;
        env.stop_weather = [&](std::int32_t kind) { calls.push_back({"stop_weather", 0, kind, 0, 0}); };
        weather_start_effect(fpu, w, display, sink, env, in["arg_kind"].i32(), in["arg_intensity"].i32());
        calls.push_back({"eq_get_time", 0, 0, 0, 0});

        if (auto why = cmp_state(out, w); !why.empty()) return why;
        if (out["display_raining"].i64() != display.raining) return "display_raining";
        bool started = w.next_strike != (static_cast<std::uint32_t>(out["next_strike_time"].i32()) ^ 0x5a5a5a5au);
        if (started) {
            if (static_cast<std::uint32_t>(out["next_strike_time"].i32()) != w.next_strike) return "next_strike_time";
            if (static_cast<std::uint32_t>(out["next_fade_time"].i32()) != w.next_fade) return "next_fade_time";
            if (out["thunder"].i64() != w.thunder) return "thunder";
            if (f32_bits(out["fog_offset_near"]) != w.fog_near) return "fog_offset_near";
            if (f32_bits(out["fog_offset_far"]) != w.fog_far) return "fog_offset_far";
        }
        // CDisplay::StopWeather and __EQGetTime.
        const auto& want_calls = out["calls"].items;
        if (want_calls.size() != calls.size()) return "calls: " + std::to_string(calls.size()) + " instead of " +
                                                      std::to_string(want_calls.size());
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (want_calls[i]["fn"].text != calls[i].fn) return "call " + std::to_string(i) + ": " + calls[i].fn;
            if (calls[i].fn == "stop_weather" &&
                (want_calls[i]["kind"].i32() != calls[i].a || want_calls[i]["quiet"].i32() != 0))
                return "stop_weather arguments";
        }
        // The manager's calls.
        const auto& want = out["virtual_calls"].items;
        if (want.size() != sink.calls.size())
            return "virtual calls: " + std::to_string(sink.calls.size()) + " instead of " + std::to_string(want.size());
        for (std::size_t i = 0; i < want.size(); ++i) {
            const Call& c = sink.calls[i];
            const Json& j = want[i];
            if (j["fn"].text != c.fn || j["object"].i32() != c.object)
                return "virtual call " + std::to_string(i) + ": " + c.fn + " instead of " + j["fn"].text;
            if (c.fn == "definition") continue; // the string's address
            if (j["a"].i32() != c.a || j["b"].i32() != c.b || j["c"].i32() != c.c)
                return "virtual call " + std::to_string(i) + " (" + c.fn + ") arguments: " + std::to_string(c.a) +
                       " instead of " + std::to_string(j["a"].i32());
        }
        for (const Call& c : sink.calls)
            if (c.fn == "set_intensity") {
                if (f32_bits(out["precipitation_target"]) != static_cast<std::uint32_t>(c.a)) return "precipitation_target";
                if (out["precipitation_immediate"].i32() != c.c) return "precipitation_immediate";
            }
        return {};
    });
}

TEST_CASE("Weather_RequestRainSnowToggle vectors") {
    hailstone::test::each_vector("Weather_RequestRainSnowToggle.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        RainSnowRequest q = rain_snow_request(in["kind"].i32(), in["on"].i32(), static_cast<std::uint8_t>(in["world_hour"].i64()));
        if (out["opcode"].i64() != kRainSnowOpcode) return "opcode";
        if (out["rain"].u32() != q.rain) return "rain";
        if (out["snow"].u32() != q.snow) return "snow";
        if (out["on"].i32() != q.on) return "on";
        if (out["hours"].u32() != q.hours) return "hours: " + std::to_string(q.hours);
        if (out["kind"].i32() != q.kind) return "kind";
        const auto& calls = out["calls"].items;
        if (calls.size() != 2 || calls[0]["fn"].text != "get_string" || calls[0]["id"].u32() != q.message)
            return "get_string";
        if (calls[1]["fn"].text != "send" || calls[1]["length"].u32() != kRainSnowSize) return "send";
        return {};
    });
}
