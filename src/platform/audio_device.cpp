#include "platform/audio_device.h"

#include <algorithm>
#include <string>

#include <SDL3/SDL.h>

#include "core/log.h"

namespace nmsc::platform {

std::unique_ptr<AudioDevice> AudioDevice::open(Render render, std::uint32_t rate, std::uint32_t buffer_frames) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("audio: no audio subsystem ({}); running without sound", SDL_GetError());
        return nullptr;
    }
    std::unique_ptr<AudioDevice> dev(new AudioDevice(std::move(render), rate));
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = static_cast<int>(rate);
    // Read when the device is opened (a later open with another size reopens it with that size).
    if (buffer_frames > 0) SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, std::to_string(buffer_frames).c_str());
    else SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
    dev->stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::callback, dev.get());
    if (!dev->stream_) {
        log::warn("audio: cannot open the playback device ({}); running without sound", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return nullptr;
    }
    SDL_ResumeAudioStreamDevice(dev->stream_);
    SDL_AudioSpec got{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(dev->stream_), &got, &frames);
    log::info("audio: {} at {} Hz, {} frame buffer ({} asked)",
              SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?", rate, frames, buffer_frames);
    return dev;
}

AudioDevice::~AudioDevice() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void AudioDevice::callback(void* self, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* dev = static_cast<AudioDevice*>(self);
    auto frames = static_cast<std::size_t>(additional) / (sizeof(float) * 2);
    while (frames > 0) {
        std::size_t n = std::min<std::size_t>(frames, 4096);
        dev->render_(dev->buffer_, n);
        SDL_PutAudioStreamData(stream, dev->buffer_, static_cast<int>(n * sizeof(float) * 2));
        frames -= n;
    }
}

} // namespace nmsc::platform
