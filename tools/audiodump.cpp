// audiodump: renders the client's sound offline into a 16-bit stereo WAV (no audio device).
//   audiodump [--game-dir D] xmi FILE TRACK SECONDS OUT.wav     one XMI sequence through the SoundFont
//   audiodump [--game-dir D] wave NAME OUT.wav                  one sound file as the game finds it
//   audiodump [--game-dir D] zone ZONE X Y Z HOUR SECONDS OUT.wav
//                                                              the zone's emitters and music heard at X Y Z
//   audiodump [--game-dir D] emitters ZONE                      lists the zone's emitters and music

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "assets/xmi.h"
#include "audio/audio_system.h"
#include "audio/mixer.h"
#include "audio/pcm.h"
#include "audio/sound_library.h"
#include "audio/xmi_player.h"
#include "core/game_dir.h"
#include "core/log.h"

using namespace nmsc;

namespace {

constexpr std::uint32_t kRate = 48000; // the AudioSystem mixer's rate

bool write_wav(const std::string& path, const std::vector<float>& stereo) {
    const std::vector<std::byte> wav = audio::encode_wav16(stereo, kRate, 2);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
    return static_cast<bool>(out);
}

void report(const std::vector<float>& stereo) {
    double sum = 0.0;
    float peak = 0.0f;
    for (float f : stereo) {
        sum += static_cast<double>(f) * f;
        peak = std::max(peak, std::fabs(f));
    }
    double rms = stereo.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(stereo.size()));
    std::printf("%.2f s, rms %.4f, peak %.3f\n", static_cast<double>(stereo.size()) / 2.0 / kRate, rms, peak);
}

int usage() {
    std::fprintf(stderr, "usage: audiodump [--game-dir D] xmi FILE TRACK SECONDS OUT.wav | wave NAME OUT.wav |\n"
                         "       zone ZONE X Y Z HOUR SECONDS OUT.wav | emitters ZONE\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::string game_dir = std::getenv("EQ_GAME_DIR") ? std::getenv("EQ_GAME_DIR") : "";
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--game-dir") == 0 && i + 1 < argc) game_dir = argv[++i];
        else if (std::strcmp(argv[i], "--verbose") == 0) log::set_level(log::Level::Debug);
        else args.emplace_back(argv[i]);
    }
    if (game_dir.empty() || args.empty()) return usage();
    GameDir game(game_dir);
    audio::AudioSystem sys(game, audio::AudioSettings{.master = 100, .music = 100, .effects = 100}, false);
    sys.set_self_advance(false);
    std::vector<float> out;

    if (args[0] == "xmi" && args.size() == 5) {
        auto bytes = sys.library().read(args[1]);
        if (!bytes) return std::fprintf(stderr, "%s not found\n", args[1].c_str()), 1;
        auto seqs = assets::parse_xmi(*bytes);
        int track = std::atoi(args[2].c_str());
        std::printf("%s: %zu sequences\n", args[1].c_str(), seqs.size());
        if (track < 0 || static_cast<std::size_t>(track) >= seqs.size()) return 1;
        auto sf2 = game.read("synthusr.sf2");
        auto font = sf2 ? audio::SoundFont::load(*sf2) : nullptr;
        if (!font) return std::fprintf(stderr, "no SoundFont\n"), 1;
        auto src = audio::make_xmi_source(font, seqs[static_cast<std::size_t>(track)], 1);
        auto frames = static_cast<std::size_t>(std::atof(args[3].c_str()) * kRate);
        out.resize(frames * 2);
        std::size_t got = src->render(out.data(), frames, kRate);
        out.resize(got * 2);
        report(out);
        return write_wav(args[4], out) ? 0 : 1;
    }
    if (args[0] == "wave" && args.size() == 3) {
        sys.library().preload({args[1]});
        sys.library().wait_idle();
        auto pcm = sys.library().wave(args[1]);
        if (!pcm) return std::fprintf(stderr, "%s not found or not decodable\n", args[1].c_str()), 1;
        std::printf("%s: %u Hz, %u channels, %zu frames\n", args[1].c_str(), pcm->rate, pcm->channels, pcm->frames());
        audio::Mixer m(kRate);
        m.play(pcm, 1.0f, 0, 1, audio::Bus::Effects);
        std::size_t frames = static_cast<std::size_t>(static_cast<double>(pcm->frames()) * kRate / pcm->rate) + 1;
        out.resize(frames * 2);
        m.render(out.data(), frames);
        report(out);
        return write_wav(args[2], out) ? 0 : 1;
    }
    if (args[0] == "emitters" && args.size() == 2) {
        sys.set_zone(args[1]);
        for (const auto& e : sys.zone().emitters())
            std::printf("%-28s %-22s pos %.0f %.0f %.0f range %.0f dist %.0f..%.0f gain %.3f loops %d delay %d..%d period %d env %d%s\n",
                        e.source.c_str(), e.music >= 0 ? "(music)" : e.wave.c_str(), e.position.x, e.position.y,
                        e.position.z, e.range, e.min_distance, e.max_distance, e.gain, e.loop_count, e.delay_min,
                        e.delay_max, e.period, e.environment, e.random_distance > 0 ? " random" : "");
        for (const auto& m : sys.zone().music())
            std::printf("music %s track %d priority %d fade in %d out %d\n", m.file.c_str(), m.track, m.priority,
                        m.fade_in_ms, m.fade_out_ms);
        return 0;
    }
    if (args[0] == "zone" && args.size() == 8) {
        sys.set_zone(args[1]);
        sys.set_hour(std::atoi(args[5].c_str()));
        glm::vec3 at(std::strtof(args[2].c_str(), nullptr), std::strtof(args[3].c_str(), nullptr),
                     std::strtof(args[4].c_str(), nullptr));
        sys.set_listener(at, 0.0f);
        sys.library().wait_idle(); // the zone's sounds are decoded before the first frame
        double seconds = std::atof(args[6].c_str());
        // 20 ms frames, as a 50 fps client.
        const std::size_t step = kRate / 50;
        std::vector<float> block(step * 2);
        for (double t = 0.0; t < seconds; t += 0.02) {
            sys.update(static_cast<std::uint32_t>(t * 1000.0) + 1);
            sys.library().wait_idle();
            sys.mixer().render(block.data(), step);
            out.insert(out.end(), block.begin(), block.end());
        }
        std::printf("%zu emitter voices started\n", sys.emitter_voices());
        report(out);
        return write_wav(args[7], out) ? 0 : 1;
    }
    return usage();
}
