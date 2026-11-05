#include "game/weather.h"

namespace nmsc::game {

namespace {

// What StartWeather asks of the display, the sound manager and the chat. The weather's fog band (SetFog) is not
// applied: the client's fog has no weather yet (docs/unknowns.md, Fog).
class Effects : public WeatherEffects {
public:
    Effects(const std::function<std::int32_t()>& random, std::int32_t zone_id, bool& raining, bool& windy,
            std::int32_t& thunder, std::uint32_t& message)
        : random_(random), zone_id_(zone_id), raining_(raining), windy_(windy), thunder_(thunder), message_(message) {}
    void set_fog(std::uint32_t, std::uint32_t) override {}
    float fog_near() override { return 0.0f; }
    float fog_far() override { return 0.0f; }
    void update_sky_weather_and_fog() override {}
    void play_wave(std::int32_t id) override { thunder_ = id; }
    void emitter_raining(bool on) override { raining_ = on; }
    void emitter_wind(bool on) override { windy_ = on; }
    void chat(std::uint32_t eqstr) override { message_ = eqstr; }
    std::int32_t zone_id() override { return zone_id_; }
    std::int32_t random() override { return random_(); }
    std::int32_t rand() override { return random_(); }

private:
    const std::function<std::int32_t()>& random_;
    std::int32_t zone_id_;
    bool& raining_;
    bool& windy_;
    std::int32_t& thunder_;
    std::uint32_t& message_;
};

// __EQGetTime is never 0 in the original; ApplyWeatherState's next step time (the frame time - 1) would wrap at 0.
std::uint32_t frame_ms(double now) { return static_cast<std::uint32_t>(now * 1000.0) + 1; }

} // namespace

void Weather::handle_message(std::int32_t kind, std::int32_t intensity) {
    weather_handle_message(state_, kind, intensity);
}

WeatherKind Weather::kind() const {
    return state_.kind == 0xff && precipitation_.target() <= 0.0f && precipitation() <= 0.0f
               ? WeatherKind::None
               : static_cast<WeatherKind>(precipitation_.kind());
}

void Weather::update(double now, bool outdoor, const std::function<std::int32_t()>& random, std::int32_t zone_id) {
    const std::uint32_t ms = frame_ms(now);
    display_.frame_time = ms;
    thunder_ = 0;
    message_ = 0;
    Effects fx(random, zone_id, raining_, windy_, thunder_, message_);

    // RealRender_World (@0x49ca10) clears +9, in zone time types 1, 2 and 5 with bRain set runs StartWeather, and
    // later in the frame Weather_Update: so a message starts at the end of its frame and fades from the next.
    state_.lightning = 0;
    if (outdoor && display_.raining) weather_start(state_, display_, fx);

    // Weather_Update: the pending message, then the precipitation manager's update.
    if (state_.pending) {
        StartEffectEnv env;
        env.now = ms;
        env.stop_weather = [&](std::int32_t old_kind) { weather_stop(state_, display_, fx, old_kind, false); };
        weather_start_effect(mechanics::Fpu{}, state_, display_, precipitation_, env, state_.pending_kind,
                             state_.pending_intensity);
        state_.pending = 0;
    }
    precipitation_.update(ms);
}

} // namespace nmsc::game
