#pragma once

// Software mixer: decoded sounds and streamed sources into stereo float at the
// device rate. Each voice has a volume that ramps linearly over a time (the
// original's sound instances fade this way: their updates @0x5be1d0 (2D) and
// @0x5be380 (3D) interpolate from the old to the new volume over the fade's ms, and
// a voice that reaches volume 0 ends, as AIL_end_sample there). A 2D voice plays
// its volume on both channels (AIL_set_sample_volume_levels(v, v)); a 3D voice
// plays volume^(5/3) times the two channel levels the Miles provider computes
// every frame (audio/sound_control.h, miles_fast2d_levels). Thread-safe: the
// device callback renders while the game thread adds and changes voices. A
// streamed source (an MP3's decoder, an XMI's synthesizer) renders with the
// lock released, so the game thread's calls never wait on a decode, and a
// finished voice is destroyed outside the lock.
//
// Reverb: the listener's room (Miles' digital master room type, which the
// "Fast 2D Positional Audio" provider the original opens sets from
// AIL_set_3D_room_type) as Miles' own parameters for that room (level, decay
// time, damping, predelay; audio/listener.h), fed by each 3D voice's reverb
// send (its SoundControl +0x24, AIL_set_sample_reverb_levels). Miles' reverb
// DSP itself is not in the exe; this one is a plain comb / all-pass reverb
// with those parameters (docs/unknowns.md).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/pcm.h"

namespace hailstone::audio {

enum class Bus : std::uint8_t { Effects, Music };

class Mixer {
public:
    explicit Mixer(std::uint32_t rate, std::size_t max_voices = 64);

    std::uint32_t rate() const { return rate_; }

    // Starts a sound: `loops` plays (0 = forever), ramping up from 0 over
    // fade_ms when it is positive. `levels`: a 3D voice's first channel levels
    // (set_levels). Returns 0 when every voice is busy.
    std::uint32_t play(std::shared_ptr<const Pcm> pcm, float volume, std::int32_t fade_ms, std::int32_t loops, Bus bus,
                       const float* levels = nullptr);
    std::uint32_t play_source(std::unique_ptr<Source> source, float volume, std::int32_t fade_ms, Bus bus);

    // Ramps the volume to `volume` over fade_ms (at once when 0); a voice ends
    // when its volume reaches 0.
    void set_volume(std::uint32_t voice, float volume, std::int32_t fade_ms);
    // The channel levels of a 3D voice (miles_fast2d_levels); from the first call its volume goes through
    // miles_volume_curve.
    void set_levels(std::uint32_t voice, float left, float right);
    void stop(std::uint32_t voice) { set_volume(voice, 0.0f, 0); }
    // The voice's reverb send (0 = dry).
    void set_send(std::uint32_t voice, float send);
    // The room: level 0 turns the reverb off.
    void set_reverb(float level, float decay_s, float damping, float predelay_s);
    bool alive(std::uint32_t voice) const;
    void stop_all();

    void set_bus_volume(Bus bus, float volume);
    void set_master(float volume);
    // The output's left and right channels swapped (/reversesound, audio.reverse_sound): the whole mix, effects,
    // music and streams alike.
    void set_swap_channels(bool swap);

    // Mixes `frames` stereo frames into `out` (overwritten), in blocks of at most kBlockFrames. Allocates nothing
    // (the device's callback calls it on the audio thread).
    void render(float* out, std::size_t frames);
    static constexpr std::size_t kBlockFrames = 4096;
    std::size_t voice_count() const;

private:
    struct Voice {
        std::uint32_t id = 0;
        std::shared_ptr<const Pcm> pcm;
        std::unique_ptr<Source> source;
        double pos = 0.0;
        std::int32_t loops = 1;
        float volume = 0.0f, target = 0.0f, step = 0.0f;
        bool positional = false;          // a 3D voice: volume^(5/3) x the levels
        float left = 1.0f, right = 1.0f;  // channel levels
        float send = 0.0f;
        Bus bus = Bus::Effects;
        bool done = false;
        bool rendering = false;      // its source renders unlocked: not destroyed until the block is mixed
        std::vector<float> rendered; // a source's block, kBlockFrames stereo frames (made by play_source)
    };
    // A source the audio thread renders with the lock released (render_block).
    struct Pending {
        std::uint32_t id = 0;
        Source* source = nullptr;
        float* out = nullptr;
        std::size_t got = 0;
    };
    Voice* find(std::uint32_t id);
    const Voice* find(std::uint32_t id) const;
    std::uint32_t add(Voice v, float volume, std::int32_t fade_ms);
    void ramp(Voice& v, float volume, std::int32_t fade_ms) const;
    void render_block(float* out, std::size_t frames); // frames <= kBlockFrames
    // Moves the finished voices not rendering into `out`, to be destroyed once the lock is released.
    void reap(std::vector<Voice>& out);

    mutable std::mutex mutex_;
    std::uint32_t rate_;
    std::size_t max_voices_;
    std::vector<Voice> voices_;
    std::uint32_t next_id_ = 1;
    float master_ = 1.0f;
    bool swap_ = false;
    float bus_[2] = {1.0f, 1.0f};
    std::vector<Pending> pending_; // the audio thread's; max_voices_ reserved

    struct Reverb {
        float level = 0.0f;
        std::vector<float> predelay;
        std::size_t predelay_at = 0;
        struct Comb {
            std::vector<float> line;
            std::size_t at = 0;
            float feedback = 0.0f, damp = 0.0f, store = 0.0f;
        };
        struct AllPass {
            std::vector<float> line;
            std::size_t at = 0;
        };
        Comb combs[2][4];
        AllPass allpasses[2][2];
        void configure(std::uint32_t rate, float decay_s, float damping, float predelay_s);
        void process(const float* in, float* out, std::size_t frames);
    };
    Reverb reverb_;
    std::vector<float> wet_;
};

} // namespace hailstone::audio
