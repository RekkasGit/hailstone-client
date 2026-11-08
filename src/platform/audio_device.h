#pragma once

// The sound output: SDL3's default playback device, stereo float. open()
// returns nullptr when there is no audio subsystem or device (headless runs,
// SDL_AUDIODRIVER=dummy is fine too); the client then runs silent.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

struct SDL_AudioStream;

namespace hailstone::platform {

class AudioDevice {
public:
    // `render(out, frames)` is called on SDL's audio thread for stereo frames at rate(). `buffer_frames` > 0 asks
    // for a device buffer of that many frames (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES; the device may round it).
    using Render = std::function<void(float* out, std::size_t frames)>;
    static std::unique_ptr<AudioDevice> open(Render render, std::uint32_t rate = 48000, std::uint32_t buffer_frames = 0);
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    std::uint32_t rate() const { return rate_; }

private:
    AudioDevice(Render render, std::uint32_t rate) : render_(std::move(render)), rate_(rate) {}
    static void callback(void* self, SDL_AudioStream* stream, int additional, int total);

    Render render_;
    std::uint32_t rate_;
    SDL_AudioStream* stream_ = nullptr;
    float buffer_[4096 * 2];
};

} // namespace hailstone::platform
