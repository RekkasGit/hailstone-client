#include "audio/pcm.h"

#include <algorithm>
#include <cstring>

#include <dr_mp3.h>
#include <dr_wav.h>

namespace nmsc::audio {

std::optional<Pcm> decode_wav(std::span<const std::byte> data) {
    unsigned int channels = 0, rate = 0;
    drwav_uint64 frames = 0;
    float* p = drwav_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &channels, &rate, &frames, nullptr);
    if (!p) return std::nullopt;
    std::optional<Pcm> out;
    if ((channels == 1 || channels == 2) && rate > 0) {
        out.emplace();
        out->rate = rate;
        out->channels = channels;
        out->samples.assign(p, p + frames * channels);
    }
    drwav_free(p, nullptr);
    return out;
}

std::optional<Pcm> decode_mp3(std::span<const std::byte> data) {
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    float* p = drmp3_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &config, &frames, nullptr);
    if (!p) return std::nullopt;
    std::optional<Pcm> out;
    if ((config.channels == 1 || config.channels == 2) && config.sampleRate > 0) {
        out.emplace();
        out->rate = config.sampleRate;
        out->channels = config.channels;
        out->samples.assign(p, p + frames * config.channels);
    }
    drmp3_free(p, nullptr);
    return out;
}

std::optional<Pcm> decode_sound(std::span<const std::byte> data) {
    if (data.size() >= 4 && std::memcmp(data.data(), "RIFF", 4) == 0) return decode_wav(data);
    return decode_mp3(data);
}

namespace {

class Mp3Source final : public Source {
public:
    Mp3Source(std::vector<std::byte> file, std::int32_t loops) : file_(std::move(file)), loops_(loops) {
        ok_ = drmp3_init_memory(&mp3_, file_.data(), file_.size(), nullptr) && mp3_.channels >= 1 &&
              mp3_.channels <= 2 && mp3_.sampleRate > 0;
    }
    ~Mp3Source() override {
        if (ok_) drmp3_uninit(&mp3_);
    }

    std::size_t render(float* out, std::size_t frames, std::uint32_t rate) override {
        if (!ok_) return 0;
        const double step = static_cast<double>(mp3_.sampleRate) / rate;
        std::size_t done = 0;
        while (done < frames) {
            // Linear interpolation between buffered source frames buf_[i] and buf_[i + 1].
            while (pos_ + 1.0 >= static_cast<double>(have_)) {
                if (!refill()) return done;
            }
            auto i = static_cast<std::size_t>(pos_);
            float t = static_cast<float>(pos_ - static_cast<double>(i));
            for (int c = 0; c < 2; ++c) {
                float a = buf_[i * 2 + static_cast<std::size_t>(c)], b = buf_[(i + 1) * 2 + static_cast<std::size_t>(c)];
                out[done * 2 + static_cast<std::size_t>(c)] = a + (b - a) * t;
            }
            ++done;
            pos_ += step;
        }
        return done;
    }

private:
    // Keeps the last frame and appends newly decoded ones (as stereo).
    bool refill() {
        constexpr std::size_t kChunk = 2048;
        std::size_t keep = have_ > 0 ? 1 : 0;
        if (keep) {
            buf_[0] = buf_[(have_ - 1) * 2];
            buf_[1] = buf_[(have_ - 1) * 2 + 1];
            pos_ -= static_cast<double>(have_ - 1);
        }
        have_ = keep;
        tmp_.resize(kChunk * mp3_.channels);
        drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3_, kChunk, tmp_.data());
        if (got == 0) {
            if (loops_ == 1 || !drmp3_seek_to_pcm_frame(&mp3_, 0)) return false;
            if (loops_ > 1) --loops_;
            got = drmp3_read_pcm_frames_f32(&mp3_, kChunk, tmp_.data());
            if (got == 0) return false;
        }
        buf_.resize((keep + got) * 2);
        for (std::size_t f = 0; f < got; ++f) {
            float l = tmp_[f * mp3_.channels];
            float r = mp3_.channels == 2 ? tmp_[f * 2 + 1] : l;
            buf_[(keep + f) * 2] = l;
            buf_[(keep + f) * 2 + 1] = r;
        }
        have_ = keep + static_cast<std::size_t>(got);
        return true;
    }

    std::vector<std::byte> file_;
    drmp3 mp3_{};
    bool ok_ = false;
    std::int32_t loops_;
    std::vector<float> tmp_, buf_{0.0f, 0.0f};
    std::size_t have_ = 0;
    double pos_ = 0.0;
};

} // namespace

std::unique_ptr<Source> make_mp3_source(std::vector<std::byte> file, std::int32_t loops) {
    return std::make_unique<Mp3Source>(std::move(file), loops);
}

} // namespace nmsc::audio
