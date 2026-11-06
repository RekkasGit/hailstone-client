#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <map>
#include <string>

#include "audio/zone_emitters.h"
#include "game_files.h"

using namespace nmsc;
using namespace nmsc::audio;

namespace {

struct FakeSink : EmitterSink {
    struct Play {
        std::string file;
        SoundControl control;
        std::uint32_t id;
    };
    std::vector<Play> plays;
    std::map<std::uint32_t, std::int32_t> released; // voice -> fade
    std::vector<std::pair<std::string, std::int32_t>> music;   // file, track
    std::vector<float> music_volumes;
    std::uint32_t next = 1;
    std::uint32_t play_wave(const std::string& file, const SoundControl& c) override {
        plays.push_back({file, c, next});
        return next++;
    }
    void release(std::uint32_t voice, std::int32_t fade) override { released[voice] = fade; }
    bool alive(std::uint32_t voice) override { return voice && !released.contains(voice); }
    std::uint32_t play_music(const MusicEntry& e, float volume) override {
        music.emplace_back(e.file, e.track);
        music_volumes.push_back(volume);
        return next++;
    }
};

assets::EffSoundRecord record(std::uint8_t kind, std::int32_t sound_day, std::int32_t sound_night, float radius) {
    assets::EffSoundRecord r;
    r.kind[0] = r.kind[1] = kind;
    r.sound[0] = sound_day;
    r.sound[1] = sound_night;
    r.radius = radius;
    return r;
}

auto all_exist = [](const std::string&) { return true; };

} // namespace

TEST_CASE("Classic emitter base gain (CreateOldEmitter)") {
    CHECK(classic_level_gain(0, 0) == doctest::Approx(0.2));
    CHECK(classic_level_gain(-50, 0) == doctest::Approx(0.2));
    CHECK(classic_level_gain(0, 2) == 1.0f);
    CHECK(classic_level_gain(2000, 0) == doctest::Approx(0.1));
    CHECK(classic_level_gain(10000, 3) == doctest::Approx(0.00001));
    CHECK(classic_level_gain(10001, 0) == 0.0f);
    CHECK(classic_level_gain(INT32_MIN, 0) == 0.0f);
}

TEST_CASE("Classic EFF records become emitters as CreateOldEmitter makes them") {
    std::unordered_map<std::int32_t, std::string> global = {{130, "Punch1.WAV"}};
    WaveTable waves;
    waves.global = &global;
    waves.zone = {{1, "a.wav"}, {162, "wind.wav"}, {163, "night.wav"}};

    // Day and night differ: two emitters with periods 1 and 2.
    auto r0 = record(0, 162, 163, 100.0f);
    // Same on both sides: one always-on emitter; cooldown 1000 + random 100 -> [1500, 1600).
    auto r1 = record(0, 1, 1, 50.0f);
    r1.cooldown[0] = r1.cooldown[1] = 1000;
    r1.random_delay = 100;
    // Kind 2 is always one emitter; full-volume distance at least 3, max 100.
    auto r2 = record(2, 130, 1, 0.0f);
    r2.param[0] = 2;
    // Kind 3: random places param + 150 around the listener; negative radius = environment 2.
    auto r3 = record(3, 1, 1, -2.0f);
    r3.param[0] = r3.param[1] = 10;
    // Music: -2 = the second mp3index entry; the first kind and level are the loop count.
    auto r4 = record(1, -2, -2, 300.0f);
    r4.param[0] = r4.param[1] = 750;
    r4.fade_out[0] = r4.fade_out[1] = 50000;
    // Unknown sound: no emitter.
    auto r5 = record(0, 99, 99, 10.0f);

    std::vector<std::string> mp3 = {"one.mp3", "two.mp3"};
    auto set = emitters_from_eff({r0, r1, r2, r3, r4, r5}, waves, mp3, "zone.xmi", all_exist, "z");
    REQUIRE(set.emitters.size() == 6);
    CHECK(set.emitters[0].wave == "wind.wav");
    CHECK(set.emitters[0].period == 1);
    CHECK(set.emitters[1].wave == "night.wav");
    CHECK(set.emitters[1].period == 2);
    CHECK(set.emitters[0].gain == doctest::Approx(0.2));
    CHECK(set.emitters[0].loop_count == 0); // continuous

    const Emitter& one = set.emitters[2];
    CHECK(one.period == 0);
    CHECK(one.loop_count == 1);
    CHECK(one.delay_min == 1500);
    CHECK(one.delay_max == 1600);

    const Emitter& k2 = set.emitters[3];
    CHECK(k2.wave == "Punch1.WAV");
    CHECK(k2.min_distance == 3.0f);
    CHECK(k2.max_distance == 100.0f);
    CHECK(k2.gain == 1.0f);

    const Emitter& k3 = set.emitters[4];
    CHECK(k3.random_distance == 160.0f);
    CHECK(k3.min_distance == 8.0f);
    CHECK(k3.environment == 2);
    CHECK(k3.range == 10000.0f);

    const Emitter& m = set.emitters[5];
    REQUIRE(m.music == 0);
    REQUIRE(set.music.size() == 1);
    CHECK(set.music[0].file == "two.mp3");
    CHECK(set.music[0].priority == 10300);
    CHECK(set.music[0].fade_in_ms == 750);
    CHECK(set.music[0].fade_out_ms == 20000); // capped
    CHECK(set.music[0].crossfade_ms == 1500);
}

TEST_CASE("Emitter runtime: range, continuous voice, release fade, day/night") {
    WaveTable waves;
    waves.zone = {{162, "day.wav"}, {163, "night.wav"}};
    auto r = record(0, 162, 163, 100.0f);
    r.fade_out[0] = 400;
    ZoneEmitters z(emitters_from_eff({r}, waves, {}, "", all_exist));
    z.set_state(false, 1, false);
    FakeSink sink;
    z.update({500, 0, 0}, 1000, 1.0f, 1.0f, sink);
    CHECK(sink.plays.empty()); // out of range
    z.update({50, 0, 0}, 1010, 1.0f, 1.0f, sink);
    REQUIRE(sink.plays.size() == 1);
    CHECK(sink.plays[0].file == "day.wav");
    CHECK(sink.plays[0].control.loops == 0);
    CHECK_FALSE(sink.plays[0].control.positional());
    CHECK(sink.plays[0].control.volume == doctest::Approx(0.2));
    z.update({60, 0, 0}, 1020, 1.0f, 1.0f, sink);
    CHECK(sink.plays.size() == 1); // kept, not restarted
    z.update({150, 0, 0}, 1030, 1.0f, 1.0f, sink);
    CHECK(sink.released.at(1) == 400); // left the radius: fades over its fade-out
    // Night: the day emitter is off and the night one plays.
    z.set_state(true, 1, false);
    z.update({0, 0, 0}, 1040, 1.0f, 1.0f, sink);
    REQUIRE(sink.plays.size() == 2);
    CHECK(sink.plays[1].file == "night.wav");
    // The user switch turns emitters off.
    z.set_state(true, 1, true);
    z.update({0, 0, 0}, 1050, 1.0f, 1.0f, sink);
    CHECK(sink.released.contains(2));
}

TEST_CASE("Emitter runtime: one-shots repeat after strictly more than the delay") {
    WaveTable waves;
    waves.zone = {{1, "drip.wav"}};
    auto r = record(2, 1, 1, 0.0f);
    r.cooldown[0] = r.cooldown[1] = 1000;
    ZoneEmitters z(emitters_from_eff({r}, waves, {}, "", all_exist));
    FakeSink sink;
    z.update({0, 0, 0}, 1000, 1.0f, 1.0f, sink); // the timer starts here
    CHECK(sink.plays.empty());
    z.update({0, 0, 0}, 2000, 1.0f, 1.0f, sink);
    CHECK(sink.plays.empty());                    // equal is not enough
    z.update({0, 0, 0}, 2001, 1.0f, 1.0f, sink);
    REQUIRE(sink.plays.size() == 1);
    CHECK(sink.plays[0].control.loops == 1);
    CHECK(sink.plays[0].control.positional());
    z.update({0, 0, 0}, 3001, 1.0f, 1.0f, sink);
    CHECK(sink.plays.size() == 1);
    z.update({0, 0, 0}, 3002, 1.0f, 1.0f, sink);
    CHECK(sink.plays.size() == 2);
    // Kind 2 reaches at most its max distance 100, whatever the radius.
    z.update({150, 0, 0}, 9000, 1.0f, 1.0f, sink);
    CHECK(sink.plays.size() == 2);
}

TEST_CASE("Music: the active entry with the lowest priority plays; others cross-fade") {
    std::vector<std::string> mp3 = {"big.mp3", "small.mp3"};
    auto big = record(1, -1, -1, 1000.0f);
    auto small = record(1, -2, -2, 100.0f);
    small.fade_out[0] = small.fade_out[1] = 3000;
    ZoneEmitters z(emitters_from_eff({big, small}, {}, mp3, "", all_exist));
    FakeSink sink;
    z.update({500, 0, 0}, 100, 1.0f, 1.0f, sink); // only in the big region
    REQUIRE(sink.music.size() == 1);
    CHECK(sink.music[0].first == "big.mp3");
    z.update({50, 0, 0}, 200, 1.0f, 1.0f, sink); // both: the smaller region wins
    REQUIRE(sink.music.size() == 2);
    CHECK(sink.music[1].first == "small.mp3");
    CHECK(sink.released.at(1) == 1500); // the cross-fade
    z.update({5000, 0, 0}, 300, 1.0f, 1.0f, sink); // out of both: fade to silence
    CHECK(sink.released.at(2) == 3000);
    CHECK(z.current_music() == -1);
    // Music volume 0: entries switch but nothing starts.
    z.update({50, 0, 0}, 400, 1.0f, 0.0f, sink);
    CHECK(sink.music.size() == 2);
}

TEST_CASE("Music starts at the entry's volume: the music setting gates it, the mixer's bus scales it") {
    FakeSink sink;
    MusicManager m;
    MusicEntry e;
    e.id = 1;
    e.file = "a.mp3";
    e.volume = 0.8f;
    m.set(e);
    m.set_volume(0.5f); // the music setting: the music bus applies it (AudioSystem::update)
    m.play(1, sink);
    REQUIRE(sink.music_volumes.size() == 1);
    CHECK(sink.music_volumes[0] == 0.8f); // not 0.4: the setting is not applied twice
    // At 0 nothing starts (FadeIn needs MusicManager +0x08 above 0).
    m.release_all(sink);
    m.set_volume(0.0f);
    m.play(1, sink);
    CHECK(sink.music_volumes.size() == 1);
}

TEST_CASE("MusicManager::Set puts the entry first; a once-only mp3 that ended is finished") {
    FakeSink sink;
    MusicManager m;
    auto entry = [](std::int32_t id, std::string file, std::int32_t priority) {
        MusicEntry e;
        e.id = id;
        e.file = std::move(file);
        e.priority = priority;
        return e;
    };
    m.set(entry(1, "a.mp3", 5));
    m.set(entry(2, "b.mp3", 5));
    REQUIRE(m.entries().size() == 2);
    CHECK(m.entries()[0].id == 2); // the head of the list
    // A tie between two new entries: the first on the list (the later Set) wins.
    m.play(1, sink);
    CHECK(m.playing()->id == 1);
    m.stop(1, sink);
    m.play(1, sink);
    m.play(2, sink);
    CHECK(m.playing()->id == 1); // the current one keeps a tie
    // Set again: cleared (not playing any more) and first again, with its flags reset.
    m.set(entry(1, "a.mp3", 5));
    CHECK(m.playing() == nullptr);
    CHECK(m.entries()[0].id == 1);
    CHECK_FALSE(m.entries()[0].active);

    // finish_when_done (Set's +0x31): its mp3 voice ending marks every entry of the file finished.
    MusicManager f;
    MusicEntry once = entry(3, "c.mp3", 1);
    once.finish_when_done = true;
    f.set(once);
    f.set(entry(4, "c.mp3", 2));
    f.play(3, sink);
    REQUIRE(f.playing());
    std::uint32_t voice = sink.next - 1;
    f.reap(sink);
    CHECK_FALSE(f.entries()[1].finished);
    sink.released[voice] = 0; // the voice ended
    f.reap(sink);
    CHECK(f.entries()[0].finished);
    CHECK(f.entries()[1].finished);
    f.play(4, sink);
    CHECK(f.playing() == nullptr);
}

TEST_CASE("Zone emitters of the installed game") {
    const GameDir* game = test::game_dir();
    if (!game) return;
    auto txt = [&](const char* f) {
        auto b = game->read(f);
        return b ? std::string(reinterpret_cast<const char*>(b->data()), b->size()) : std::string();
    };
    auto global = assets::parse_sound_assets(txt("soundassets.txt"));
    WaveTable waves;
    waves.global = &global;
    waves.zone = assets::parse_sound_bank(txt("gfaydark_sndbnk.eff"));
    auto set = emitters_from_eff(assets::parse_sounds_eff(*game->read("gfaydark_sounds.eff")), waves,
                                 assets::parse_mp3_index(txt("mp3index.txt")), "gfaydark.xmi", all_exist);
    CHECK(set.emitters.size() > 19);
    bool wind = false;
    for (const auto& e : set.emitters) wind = wind || e.wave == "wind_lp2.wav";
    CHECK(wind);
    CHECK_FALSE(set.music.empty());
    for (const auto& m : set.music) CHECK(m.file == "gfaydark.xmi");
}

TEST_CASE("Zone sound files: forged radii, ranges and delays convert and add as the original's 32-bit code") {
    WaveTable waves;
    waves.zone = {{1, "a.wav"}};
    // A NaN radius on a music record, a huge negative one on a wave record (its environment): cvttss2si's INT_MIN.
    auto music = record(1, -1, -1, std::numeric_limits<float>::quiet_NaN());
    auto wave = record(0, 1, 1, -1e30f);
    // Cooldown and random past int32 when added: they wrap.
    wave.cooldown[0] = wave.cooldown[1] = 2147483000;
    wave.random_delay = 1000;
    auto set = emitters_from_eff({music, wave}, waves, {"one.mp3"}, "", all_exist, "z");
    REQUIRE(set.emitters.size() == 2);
    REQUIRE(set.music.size() == 1);
    CHECK(set.music[0].priority == std::numeric_limits<std::int32_t>::min());
    CHECK(set.emitters[1].environment == std::numeric_limits<std::int32_t>::min());
    CHECK(set.emitters[1].delay_min == static_cast<std::int32_t>(2147483000u + 500u));
    CHECK(set.emitters[1].delay_max == static_cast<std::int32_t>(2147483000u + 1500u));

    assets::EmtRow row;
    row.file = "b.mp3";
    row.range = std::numeric_limits<float>::infinity();
    auto emt = emitters_from_emt({row}, all_exist, "z.emt");
    REQUIRE(emt.music.size() == 1);
    CHECK(emt.music[0].priority == std::numeric_limits<std::int32_t>::min());

    // A delay span past int32 (-2e9 .. 2e9) is reduced as the original's wrapped sub, not overflowed.
    assets::EmtRow loud;
    loud.file = "a.wav";
    loud.gain = 1.0f;
    loud.loop_count = 1;
    loud.max_distance = 100.0f;
    loud.range = 100.0f;
    loud.delay_min = -2000000000;
    loud.delay_max = 2000000000;
    ZoneEmitters z(emitters_from_emt({loud}, all_exist, "z.emt"));
    FakeSink sink;
    z.update({0, 0, 0}, 1000, 1.0f, 1.0f, sink); // the timer starts: a UBSan build caught the overflow here
}
