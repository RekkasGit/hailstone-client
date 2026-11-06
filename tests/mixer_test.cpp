#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include "audio/audio_system.h"
#include "audio/mixer.h"
#include "audio/pcm.h"
#include "audio/sound_control.h"
#include "audio/sound_library.h"
#include "core/config.h"
#include "game_files.h"

using namespace nmsc;
using namespace nmsc::audio;

namespace {

std::shared_ptr<const Pcm> constant(std::uint32_t rate, std::size_t frames, float value) {
    auto p = std::make_shared<Pcm>();
    p->rate = rate;
    p->channels = 1;
    p->samples.assign(frames, value);
    return p;
}

// A minimal 16-bit mono WAV.
std::vector<std::byte> wav16(std::uint32_t rate, const std::vector<std::int16_t>& s) {
    std::vector<std::byte> d;
    auto put = [&](const void* p, std::size_t n) {
        auto b = static_cast<const std::byte*>(p);
        d.insert(d.end(), b, b + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    put("RIFF", 4);
    u32(static_cast<std::uint32_t>(36 + s.size() * 2));
    put("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    put("data", 4);
    u32(static_cast<std::uint32_t>(s.size() * 2));
    put(s.data(), s.size() * 2);
    return d;
}

} // namespace

TEST_CASE("Miles Fast 2D: inverse distance, linear pan, behind and mute at maximum") {
    const glm::vec3 face(1, 0, 0), right(0, -1, 0); // angle 0: facing +x, right -y
    // Straight ahead inside min: full gain split evenly.
    auto l = miles_fast2d_levels({1, 0, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left == doctest::Approx(0.5));
    CHECK(l.right == doctest::Approx(0.5));
    CHECK_FALSE(l.muted);
    // Ahead at 10: min / d.
    l = miles_fast2d_levels({10, 0, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left + l.right == doctest::Approx(0.5));
    // Hard right: all on the right; hard left: all on the left.
    l = miles_fast2d_levels({0, -2, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(l.right == doctest::Approx(1.0));
    l = miles_fast2d_levels({0, 2, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left == doctest::Approx(1.0));
    // 45 degrees to the right: acos(cos 45) / pi = 0.25 to the left.
    l = miles_fast2d_levels({1, -1, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left == doctest::Approx(0.25));
    CHECK(l.right == doctest::Approx(0.75));
    // Behind: 0.75.
    l = miles_fast2d_levels({-1, 0, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left + l.right == doctest::Approx(0.75));
    // At the max distance still heard; past it muted.
    l = miles_fast2d_levels({80, 0, 0}, 5.0f, 80.0f, face, right);
    CHECK_FALSE(l.muted);
    CHECK(l.left + l.right == doctest::Approx(5.0 / 80.0));
    CHECK(miles_fast2d_levels({80.5f, 0, 0}, 5.0f, 80.0f, face, right).muted);
    // On the listener: centred.
    l = miles_fast2d_levels({0, 0, 0}, 5.0f, 80.0f, face, right);
    CHECK(l.left == doctest::Approx(0.5));
    CHECK(miles_volume_curve(0.5f) == doctest::Approx(0.31498));
    CHECK(miles_volume_curve(1.0f) == 1.0f);
    auto c = spawn_sound_control({0, 0, 0}, 6.0f, true);
    CHECK(c.min_distance == 3.0f);
    CHECK(c.max_distance == 72.0f);
    CHECK(c.volume == doctest::Approx(0.8));
}

TEST_CASE("WAV decoding: 16-bit mono at 22000 Hz") {
    auto pcm = decode_sound(wav16(22000, {0, 16384, -16384, 32767}));
    REQUIRE(pcm);
    CHECK(pcm->rate == 22000);
    CHECK(pcm->channels == 1);
    REQUIRE(pcm->frames() == 4);
    CHECK(pcm->samples[1] == doctest::Approx(0.5).epsilon(0.001));
    CHECK_FALSE(decode_wav(std::vector<std::byte>(10)));
}

TEST_CASE("Mixer: volume, buses, loops and fades") {
    Mixer m(1000, 4);
    std::vector<float> out(200);
    auto id = m.play(constant(1000, 50, 0.5f), 0.5f, 0, 1, Bus::Effects);
    REQUIRE(id != 0);
    m.render(out.data(), 100);
    CHECK(out[0] == doctest::Approx(0.25));
    CHECK(out[1] == doctest::Approx(0.25));
    CHECK(out[2 * 60] == 0.0f); // past the end
    CHECK_FALSE(m.alive(id));

    // Looping forever, panned hard right, on a bus at half.
    m.set_bus_volume(Bus::Music, 0.5f);
    id = m.play(constant(1000, 10, 1.0f), 1.0f, 0, 0, Bus::Music);
    m.set_levels(id, 0.0f, 1.0f);
    m.render(out.data(), 100);
    CHECK(out[2 * 95] == 0.0f);                       // left silent
    CHECK(out[2 * 95 + 1] == doctest::Approx(0.5));   // right at the bus volume
    CHECK(m.alive(id));
    // A fade to 0 over 50 ms ends the voice; a 3D voice's volume goes through the 5/3 curve.
    m.set_volume(id, 0.0f, 50);
    m.render(out.data(), 100);
    CHECK(out[2 * 25 + 1] == doctest::Approx(0.5 * 0.31498).epsilon(0.05));
    CHECK_FALSE(m.alive(id));
    // Fade in from 0.
    id = m.play(constant(1000, 100, 1.0f), 1.0f, 100, 1, Bus::Effects);
    m.render(out.data(), 100);
    CHECK(out[0] < 0.05f);
    CHECK(out[2 * 50] == doctest::Approx(0.5).epsilon(0.05));
    // Master 0 (mute).
    m.set_master(0.0f);
    id = m.play(constant(1000, 100, 1.0f), 1.0f, 0, 1, Bus::Effects);
    m.render(out.data(), 10);
    CHECK(out[0] == 0.0f);
}

TEST_CASE("Mixer: a step longer than the sound counts every pass and stays in range") {
    // 2 frames at 10000 Hz played at 1000 Hz: each output frame passes the end five times.
    Mixer m(1000, 4);
    std::vector<float> out(20);
    auto id = m.play(constant(10000, 2, 1.0f), 1.0f, 0, 0, Bus::Effects);
    m.render(out.data(), 10);
    CHECK(out[2 * 9] == doctest::Approx(1.0));
    CHECK(m.alive(id));
    // Three plays end within the first output frames.
    id = m.play(constant(10000, 2, 1.0f), 1.0f, 0, 3, Bus::Effects);
    m.render(out.data(), 10);
    CHECK_FALSE(m.alive(id));
}

TEST_CASE("Mixer: /reversesound swaps the output's channels") {
    // RoF2 only stores ReverseSound (0xde0d48: Cmd_reversesound and the ini); the client swaps its whole output.
    Mixer m(1000, 4);
    std::vector<float> out(20);
    auto id = m.play(constant(1000, 100, 1.0f), 1.0f, 0, 0, Bus::Effects);
    m.set_levels(id, 0.0f, 1.0f); // hard right
    m.render(out.data(), 10);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == doctest::Approx(1.0));
    m.set_swap_channels(true);
    m.render(out.data(), 10);
    CHECK(out[0] == doctest::Approx(1.0));
    CHECK(out[1] == 0.0f);
    m.set_swap_channels(false);
    m.render(out.data(), 10);
    CHECK(out[0] == 0.0f);
}

namespace {
// A stream that counts its frames, at most `limit` of them, and the largest block it was asked for.
struct CountingSource final : Source {
    std::size_t at = 0, limit = 0, *largest = nullptr;
    std::size_t render(float* out, std::size_t frames, std::uint32_t) override {
        *largest = std::max(*largest, frames);
        std::size_t n = std::min(frames, limit - at);
        for (std::size_t f = 0; f < n; ++f, ++at) out[f * 2] = out[f * 2 + 1] = static_cast<float>(at % 100) / 200.0f;
        return n;
    }
};
} // namespace

TEST_CASE("Mixer: a long render is mixed in blocks, as several short ones would be") {
    // The device's callback may ask for any count; the mixer's buffers are made once, kBlockFrames long.
    constexpr std::size_t kFrames = Mixer::kBlockFrames * 2 + 123;
    std::size_t largest[2] = {0, 0};
    std::vector<float> whole(kFrames * 2), pieces(kFrames * 2);
    for (int run = 0; run < 2; ++run) {
        Mixer m(48000, 8);
        m.set_reverb(0.5f, 1.0f, 0.5f, 0.01f);
        auto id = m.play(constant(48000, kFrames, 0.25f), 1.0f, 0, 1, Bus::Effects);
        m.set_send(id, 0.5f);
        auto src = std::make_unique<CountingSource>();
        src->limit = kFrames;
        src->largest = &largest[run];
        m.play_source(std::move(src), 1.0f, 0, Bus::Music);
        if (run == 0) {
            m.render(whole.data(), kFrames);
        } else {
            for (std::size_t done = 0; done < kFrames; done += 1000)
                m.render(pieces.data() + done * 2, std::min<std::size_t>(1000, kFrames - done));
        }
    }
    CHECK(largest[0] == Mixer::kBlockFrames);
    bool same = true;
    for (std::size_t i = 0; i < whole.size(); ++i) same = same && whole[i] == doctest::Approx(pieces[i]).epsilon(1e-5);
    CHECK(same);
}

namespace {
// A stream whose render waits (at most two seconds, so a regression fails rather than hangs) for the test to let it
// go: an MP3 decode or a synthesizer taking its time on the audio thread.
struct GateSource final : Source {
    std::mutex* m = nullptr;
    std::condition_variable* cv = nullptr;
    bool* entered = nullptr;
    bool* released = nullptr;
    bool* saw_release = nullptr;
    bool* destroyed = nullptr;
    ~GateSource() override { *destroyed = true; }
    std::size_t render(float* out, std::size_t frames, std::uint32_t) override {
        std::unique_lock lock(*m);
        *entered = true;
        cv->notify_all();
        *saw_release = cv->wait_for(lock, std::chrono::seconds(2), [&] { return *released; });
        std::fill(out, out + frames * 2, 0.5f);
        return frames;
    }
};
} // namespace

TEST_CASE("Mixer: the game thread's calls do not wait for a source rendering on the audio thread") {
    std::mutex gate;
    std::condition_variable cv;
    bool entered = false, released = false, saw_release = false, destroyed = false;
    Mixer m(48000, 8);
    auto src = std::make_unique<GateSource>();
    src->m = &gate;
    src->cv = &cv;
    src->entered = &entered;
    src->released = &released;
    src->saw_release = &saw_release;
    src->destroyed = &destroyed;
    auto stream = m.play_source(std::move(src), 1.0f, 0, Bus::Music);
    REQUIRE(stream != 0);
    auto sound = m.play(constant(48000, 10000, 0.25f), 1.0f, 0, 1, Bus::Effects);
    std::vector<float> out(512 * 2);
    std::thread audio([&] { m.render(out.data(), 512); });
    {
        std::unique_lock lock(gate);
        cv.wait(lock, [&] { return entered; });
    }
    // While the source renders: these used to wait on the mixer's lock until it was done.
    CHECK(m.alive(stream));
    m.set_levels(sound, 0.5f, 0.5f);
    m.set_volume(sound, 0.8f, 0);
    CHECK(m.voice_count() == 2);
    // Stopping everything does not destroy the source under the audio thread.
    m.stop_all();
    CHECK_FALSE(destroyed);
    CHECK(m.play(constant(48000, 100, 0.25f), 1.0f, 0, 1, Bus::Effects) != 0);
    CHECK_FALSE(destroyed);
    {
        std::lock_guard lock(gate);
        released = true;
    }
    cv.notify_all();
    audio.join();
    CHECK(saw_release);
    CHECK_FALSE(m.alive(stream));
    // Stopped before its block was mixed: only the sound played meanwhile (0.25) is in it, not the stream's 0.5.
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v <= 0.25f; }));
    // The next add reaps it, outside the lock.
    CHECK(m.play(constant(48000, 100, 0.25f), 1.0f, 0, 1, Bus::Effects) != 0);
    CHECK(destroyed);
}

TEST_CASE("Mixer: voice limit and resampling") {
    Mixer m(2000, 2);
    CHECK(m.play(constant(1000, 10, 1.0f), 1.0f, 0, 0, Bus::Effects) != 0);
    CHECK(m.play(constant(1000, 10, 1.0f), 1.0f, 0, 0, Bus::Effects) != 0);
    CHECK(m.play(constant(1000, 10, 1.0f), 1.0f, 0, 0, Bus::Effects) == 0);
    m.stop_all();
    // 10 frames at 1000 Hz last 20 frames at 2000 Hz.
    auto id = m.play(constant(1000, 10, 0.5f), 1.0f, 0, 1, Bus::Effects);
    std::vector<float> out(100);
    m.render(out.data(), 19);
    CHECK(m.alive(id));
    m.render(out.data(), 2);
    CHECK_FALSE(m.alive(id));
}

TEST_CASE("Audio system without a device: zone emitters and sounds from the game") {
    const GameDir* game = test::game_dir();
    if (!game) return;
    AudioSystem audio(*game, AudioSettings{}, false);
    CHECK_FALSE(audio.has_device());
    audio.set_zone("gfaydark");
    CHECK(audio.zone().emitters().size() > 19);
    audio.library().wait_idle();
    CHECK(audio.library().archive_files() > 1000);
    // An archive-only ambient sound (snd6.pfs) decodes.
    audio.library().preload({"darkwds1.wav"});
    audio.library().wait_idle();
    auto pcm = audio.library().wave("darkwds1.wav");
    REQUIRE(pcm);
    CHECK(pcm->frames() > 1000);
    // Level up (id 0x8b) resolves through soundassets.txt and plays once decoded.
    audio.play_wave_2d(game::kSoundLevelUp);
    audio.library().wait_idle();
    audio.play_wave_2d(game::kSoundLevelUp);
    CHECK(audio.wave_playing(game::kSoundLevelUp));
    // Updating with the listener in the zone starts some emitters (offline).
    for (std::uint32_t t = 1; t < 50; ++t) {
        audio.set_listener({0, 0, 0}, 0.0f);
        audio.update(t * 20);
        audio.library().wait_idle();
    }
    std::vector<float> out(2 * 512);
    audio.mixer().render(out.data(), 512);
    audio.set_zone("");
}

TEST_CASE("Audio system: voice pools, rain and wind emitters") {
    const GameDir* game = test::game_dir();
    if (!game) return;
    AudioSystem audio(*game, AudioSettings{}, false);
    audio.set_self_advance(false);
    audio.library().preload({"LevelUp.WAV"});
    audio.library().wait_idle();
    // The local player's pool (3) has two 3D voices: a third sound at once does not start.
    for (int i = 0; i < 3; ++i) {
        SoundControl c = spawn_sound_control({0, 0, 0}, 6.0f, true);
        CHECK(c.pool == 3);
        audio.play_wave(game::kSoundLevelUp, c);
    }
    CHECK(audio.mixer().voice_count() == 2);
    // Pool 1 has no 3D voices at all.
    SoundControl p1 = location_sound_control({0, 0, 0});
    p1.pool = 1;
    audio.play_wave(game::kSoundLevelUp, p1);
    CHECK(audio.mixer().voice_count() == 2);
    // The rain emitter is on while raining outdoors, off indoors. (The NMS install's sounds\rainloop.wav and
    // wind_lp1.wav are 80-byte stubs without samples, so nothing is heard there, as in the original.)
    CHECK(audio.weather_emitters().emitters().size() == 2);
    CHECK(audio.weather_emitters().emitters()[0].wave == "rainloop.wav");
    CHECK(audio.weather_emitters().emitters()[1].wave == "wind_lp1.wav");
    CHECK_FALSE(audio.weather_emitters().enabled(0));
    audio.set_weather(true, false);
    CHECK(audio.weather_emitters().enabled(0));
    CHECK_FALSE(audio.weather_emitters().enabled(1));
    audio.update(100);
    audio.set_indoors(true);
    CHECK_FALSE(audio.weather_emitters().enabled(0));
    audio.set_indoors(false);
    audio.set_weather(false, true);
    CHECK(audio.weather_emitters().enabled(1));
}

TEST_CASE("Audio settings: SoundMixAhead (SoundManager::SetMixAhead's 4..96 fragments of 8 ms)") {
    CHECK(mix_ahead_ms(8) == 64);
    CHECK(mix_ahead_ms(1) == 32);   // below 4: 4
    CHECK(mix_ahead_ms(100) == 768); // above 96: 96
    CHECK(mix_ahead_frames(8, 48000) == 3072);
    Config c;
    CHECK(load_audio_settings(c).mix_ahead == 8);
    c.set_int("audio.mix_ahead", 0); // loadOptions: outside 1..100 is 8
    CHECK(load_audio_settings(c).mix_ahead == 8);
    c.set_int("audio.mix_ahead", 20);
    AudioSettings s = load_audio_settings(c);
    CHECK(s.mix_ahead == 20);
    Config out;
    store_audio_settings(s, out);
    CHECK(out.get_int("audio.mix_ahead", 0) == 20);
}
