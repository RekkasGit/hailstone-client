#include "audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "audio/sound_control.h"

namespace hailstone::audio {

namespace {
// A level, volume or send as the mixer keeps it: a NaN or infinity (an EMT gain of "nan" read with strtof) is 0, so
// it never reaches the mix, where it would stay in the reverb's feedback lines for good.
float sane_level(float x) { return std::isfinite(x) ? std::max(0.0f, x) : 0.0f; }
// The most a wet sample may be: far past any mix of full-scale voices, short of overflowing the comb sums.
constexpr float kMaxWet = 64.0f;
} // namespace

Mixer::Mixer(std::uint32_t rate, std::size_t max_voices) : rate_(rate), max_voices_(max_voices) {
    voices_.reserve(max_voices);
    // The audio thread never allocates: render() mixes in blocks these hold (and each source's own, play_source).
    pending_.reserve(max_voices);
    wet_.resize(kBlockFrames);
}

Mixer::Voice* Mixer::find(std::uint32_t id) {
    for (auto& v : voices_)
        if (v.id == id && !v.done) return &v;
    return nullptr;
}

const Mixer::Voice* Mixer::find(std::uint32_t id) const {
    for (const auto& v : voices_)
        if (v.id == id && !v.done) return &v;
    return nullptr;
}

void Mixer::ramp(Voice& v, float volume, std::int32_t fade_ms) const {
    v.target = sane_level(volume);
    if (fade_ms <= 0) {
        v.volume = v.target;
        v.step = 0.0f;
        if (v.target <= 0.0f) v.done = true;
        return;
    }
    float frames = static_cast<float>(fade_ms) * static_cast<float>(rate_) / 1000.0f;
    v.step = (v.target - v.volume) / std::max(frames, 1.0f);
}

void Mixer::reap(std::vector<Voice>& out) {
    // One pass, the voices kept in their order (an erase per finished voice moved the rest each time, under the
    // lock the audio thread waits for).
    std::size_t kept = 0;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        Voice& v = voices_[i];
        if (v.done && !v.rendering) {
            out.push_back(std::move(v));
        } else {
            if (kept != i) voices_[kept] = std::move(v);
            ++kept;
        }
    }
    voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(kept), voices_.end());
}

std::uint32_t Mixer::add(Voice v, float volume, std::int32_t fade_ms) {
    std::vector<Voice> finished; // declared first: destroyed after the lock is released (freeing a stream's bytes)
    std::lock_guard lock(mutex_);
    reap(finished);
    volume = sane_level(volume);
    if (voices_.size() >= max_voices_ || volume <= 0.0f) return 0;
    v.id = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    if (fade_ms > 0) {
        v.volume = 0.0f;
        ramp(v, volume, fade_ms);
    } else {
        v.volume = v.target = volume;
    }
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

std::uint32_t Mixer::play(std::shared_ptr<const Pcm> pcm, float volume, std::int32_t fade_ms, std::int32_t loops,
                          Bus bus, const float* levels) {
    if (!pcm || pcm->frames() == 0) return 0;
    Voice v;
    v.pcm = std::move(pcm);
    v.loops = loops;
    v.bus = bus;
    if (levels) {
        v.positional = true;
        v.left = sane_level(levels[0]);
        v.right = sane_level(levels[1]);
    }
    return add(std::move(v), volume, fade_ms);
}

std::uint32_t Mixer::play_source(std::unique_ptr<Source> source, float volume, std::int32_t fade_ms, Bus bus) {
    if (!source) return 0;
    Voice v;
    v.source = std::move(source);
    v.rendered.resize(kBlockFrames * 2);
    v.bus = bus;
    return add(std::move(v), volume, fade_ms);
}

void Mixer::set_volume(std::uint32_t voice, float volume, std::int32_t fade_ms) {
    std::lock_guard lock(mutex_);
    if (Voice* v = find(voice)) ramp(*v, volume, fade_ms);
}

void Mixer::set_levels(std::uint32_t voice, float left, float right) {
    std::lock_guard lock(mutex_);
    if (Voice* v = find(voice)) {
        v->positional = true;
        v->left = sane_level(left);
        v->right = sane_level(right);
    }
}

void Mixer::set_send(std::uint32_t voice, float send) {
    std::lock_guard lock(mutex_);
    if (Voice* v = find(voice)) v->send = sane_level(send);
}

void Mixer::set_reverb(float level, float decay_s, float damping, float predelay_s) {
    level = sane_level(level);
    // The delay lines are made here, outside the lock (the first room, or a new pre-delay), and swapped in under it:
    // the audio thread does not wait on their allocation. The old ones are freed after the lock is released.
    Reverb::Lines lines; // declared first: destroyed after the lock
    bool sized = true;
    if (level > 0.0f) {
        std::lock_guard lock(mutex_);
        sized = reverb_.sized_for(rate_, predelay_s);
    }
    if (!sized) lines = Reverb::make_lines(rate_, predelay_s);
    std::lock_guard lock(mutex_);
    reverb_.level = level;
    if (reverb_.level <= 0.0f) return;
    if (!sized && !reverb_.sized_for(rate_, predelay_s)) reverb_.adopt(lines);
    reverb_.configure(rate_, decay_s, damping, predelay_s);
}

namespace {
// Freeverb's comb and all-pass lengths (at 44.1 kHz; the right side 23 samples longer).
constexpr int kCombs[4] = {1116, 1188, 1277, 1356};
constexpr int kAllPasses[2] = {556, 441};
std::size_t comb_length(std::uint32_t rate, int c, int k) {
    return static_cast<std::size_t>((kCombs[k] + c * 23) * (static_cast<double>(rate) / 44100.0));
}
std::size_t allpass_length(std::uint32_t rate, int c, int k) {
    return static_cast<std::size_t>((kAllPasses[k] + c * 23) * (static_cast<double>(rate) / 44100.0));
}
// A NaN or infinity would make the pre-delay's length undefined.
float clean_predelay(float predelay_s) { return std::isfinite(predelay_s) ? std::clamp(predelay_s, 0.0f, 1.0f) : 0.0f; }
std::size_t predelay_length(std::uint32_t rate, float predelay_s) {
    return static_cast<std::size_t>(clean_predelay(predelay_s) * static_cast<float>(rate)) + 1;
}
} // namespace

bool Mixer::Reverb::sized_for(std::uint32_t rate, float predelay_s) const {
    if (predelay.size() != predelay_length(rate, predelay_s)) return false;
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < 4; ++k)
            if (combs[c][k].line.size() != comb_length(rate, c, k)) return false;
        for (int k = 0; k < 2; ++k)
            if (allpasses[c][k].line.size() != allpass_length(rate, c, k)) return false;
    }
    return true;
}

Mixer::Reverb::Lines Mixer::Reverb::make_lines(std::uint32_t rate, float predelay_s) {
    Lines l;
    l.predelay.assign(predelay_length(rate, predelay_s), 0.0f);
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < 4; ++k) l.combs[c][k].assign(comb_length(rate, c, k), 0.0f);
        for (int k = 0; k < 2; ++k) l.allpasses[c][k].assign(allpass_length(rate, c, k), 0.0f);
    }
    return l;
}

void Mixer::Reverb::adopt(Lines& l) {
    // A line of its length already keeps what it holds (the room changed, not the rate); another is swapped in,
    // empty, and its read position starts over. `l` is left with the old lines, to be freed by the caller.
    auto take = [](std::vector<float>& mine, std::vector<float>& fresh, std::size_t& at) {
        if (mine.size() == fresh.size()) return;
        mine.swap(fresh);
        at = 0;
    };
    take(predelay, l.predelay, predelay_at);
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < 4; ++k) {
            const bool fresh = combs[c][k].line.size() != l.combs[c][k].size();
            take(combs[c][k].line, l.combs[c][k], combs[c][k].at);
            if (fresh) combs[c][k].store = 0.0f;
        }
        for (int k = 0; k < 2; ++k) take(allpasses[c][k].line, l.allpasses[c][k], allpasses[c][k].at);
    }
}

void Mixer::Reverb::configure(std::uint32_t rate, float decay_s, float damping, float predelay_s) {
    // A NaN or infinity would make the feedback undefined.
    decay_s = std::isfinite(decay_s) ? std::clamp(decay_s, 0.05f, 60.0f) : 0.05f;
    damping = std::isfinite(damping) ? std::max(damping, 0.0f) : 0.0f;
    // A one-pole low-pass in each comb's loop: more damping, faster high-frequency decay.
    float damp = std::clamp(damping / (1.0f + damping), 0.0f, 0.95f);
    // The lines are sized (adopt) before this is called; one that is not (a caller that skipped it) is made here.
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < 4; ++k) {
            const std::size_t len = comb_length(rate, c, k);
            Comb& comb = combs[c][k];
            if (comb.line.size() != len) {
                comb.line.assign(len, 0.0f);
                comb.at = 0;
            }
            // -60 dB after decay_s: g^(decay_s * rate / len) = 0.001.
            comb.feedback = static_cast<float>(std::pow(10.0, -3.0 * static_cast<double>(len) / (static_cast<double>(decay_s) * static_cast<double>(rate))));
            comb.damp = damp;
        }
        for (int k = 0; k < 2; ++k) {
            const std::size_t len = allpass_length(rate, c, k);
            if (allpasses[c][k].line.size() != len) {
                allpasses[c][k].line.assign(len, 0.0f);
                allpasses[c][k].at = 0;
            }
        }
    }
    if (const std::size_t pre = predelay_length(rate, predelay_s); predelay.size() != pre) {
        predelay.assign(pre, 0.0f);
        predelay_at = 0;
    }
}

void Mixer::Reverb::process(const float* in, float* out, std::size_t frames) {
    for (std::size_t f = 0; f < frames; ++f) {
        float x = predelay[predelay_at];
        // What goes into the feedback lines is finite and bounded (a NaN sample of a float WAV, or a huge one,
        // would otherwise stay there and silence every later block).
        predelay[predelay_at] = std::isfinite(in[f]) ? std::clamp(in[f], -kMaxWet, kMaxWet) : 0.0f;
        predelay_at = (predelay_at + 1) % predelay.size();
        for (int c = 0; c < 2; ++c) {
            float y = 0.0f;
            for (Comb& comb : combs[c]) {
                float o = comb.line[comb.at];
                comb.store = o * (1.0f - comb.damp) + comb.store * comb.damp;
                comb.line[comb.at] = x + comb.store * comb.feedback;
                comb.at = (comb.at + 1) % comb.line.size();
                y += o;
            }
            for (AllPass& a : allpasses[c]) {
                float b = a.line[a.at];
                a.line[a.at] = y + b * 0.5f;
                a.at = (a.at + 1) % a.line.size();
                y = b - y;
            }
            out[f * 2 + static_cast<std::size_t>(c)] += y * 0.25f * level;
        }
    }
}

bool Mixer::alive(std::uint32_t voice) const {
    std::lock_guard lock(mutex_);
    return voice != 0 && find(voice) != nullptr;
}

void Mixer::stop_all() {
    std::vector<Voice> finished; // destroyed after the lock is released
    std::lock_guard lock(mutex_);
    for (Voice& v : voices_) v.done = true;
    reap(finished); // a source rendering now goes with the next add
}

void Mixer::set_bus_volume(Bus bus, float volume) {
    std::lock_guard lock(mutex_);
    bus_[static_cast<int>(bus)] = std::clamp(volume, 0.0f, 1.0f);
}

void Mixer::set_master(float volume) {
    std::lock_guard lock(mutex_);
    master_ = std::clamp(volume, 0.0f, 1.0f);
}

void Mixer::set_swap_channels(bool swap) {
    std::lock_guard lock(mutex_);
    swap_ = swap;
}

std::size_t Mixer::voice_count() const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return !v.done; }));
}

void Mixer::render(float* out, std::size_t frames) {
    for (std::size_t done = 0; done < frames;) {
        const std::size_t n = std::min(frames - done, kBlockFrames);
        render_block(out + done * 2, n);
        done += n;
    }
}

void Mixer::render_block(float* out, std::size_t frames) {
    std::fill(out, out + frames * 2, 0.0f);
    // The sources decode or synthesize with the lock released: marked `rendering` under it, so no game-thread call
    // destroys one meanwhile (a Voice may move in voices_, its source and buffer stay where they are).
    std::unique_lock lock(mutex_);
    pending_.clear();
    for (Voice& v : voices_)
        if (!v.done && v.source && v.rendered.size() >= frames * 2) {
            v.rendering = true;
            pending_.push_back({v.id, v.source.get(), v.rendered.data(), 0});
        }
    if (!pending_.empty()) {
        lock.unlock();
        for (Pending& p : pending_) p.got = p.source->render(p.out, frames, rate_);
        lock.lock();
    }
    const bool reverb = reverb_.level > 0.0f;
    if (reverb) std::fill(wet_.begin(), wet_.begin() + static_cast<std::ptrdiff_t>(frames), 0.0f);
    for (Voice& v : voices_) {
        const bool rendered = std::exchange(v.rendering, false); // stopped meanwhile or not, it may go now
        if (v.done) continue;
        float bus = master_ * bus_[static_cast<int>(v.bus)];
        float left = v.left * bus;
        float right = v.right * bus;
        float send = reverb ? v.send * (v.positional ? (v.left + v.right) * 0.5f : 1.0f) * bus * 0.5f : 0.0f;
        // The voice's gain from its volume: a 3D voice's goes through the provider's curve.
        float gain = v.positional ? miles_volume_curve(v.volume) : v.volume;
        if (v.source) {
            if (!rendered) continue; // added while the sources rendered: from the next block
            std::size_t got = 0;
            for (const Pending& p : pending_)
                if (p.id == v.id) got = p.got;
            const float* block = v.rendered.data();
            for (std::size_t f = 0; f < got && !v.done; ++f) {
                out[f * 2] += block[f * 2] * gain * left;
                out[f * 2 + 1] += block[f * 2 + 1] * gain * right;
                if (v.step != 0.0f) {
                    v.volume += v.step;
                    if ((v.step < 0.0f && v.volume <= v.target) || (v.step > 0.0f && v.volume >= v.target)) {
                        v.volume = v.target;
                        v.step = 0.0f;
                        if (v.target <= 0.0f) v.done = true;
                    }
                    gain = v.positional ? miles_volume_curve(v.volume) : v.volume;
                }
            }
            if (got < frames) v.done = true;
            continue;
        }
        const Pcm& p = *v.pcm;
        const std::size_t n = p.frames();
        const double step = static_cast<double>(p.rate) / rate_;
        if (n == 0) {
            v.done = true;
            continue;
        }
        for (std::size_t f = 0; f < frames && !v.done; ++f) {
            // A step longer than the sound (a short sound played at a high
            // rate) can pass the end more than once: each pass is one play.
            if (v.loops <= 0 && v.pos >= static_cast<double>(n)) v.pos = std::fmod(v.pos, static_cast<double>(n));
            while (v.pos >= static_cast<double>(n)) {
                if (v.loops == 1) {
                    v.done = true;
                    break;
                }
                if (v.loops > 1) --v.loops;
                v.pos -= static_cast<double>(n);
            }
            if (v.done) break;
            auto i = static_cast<std::size_t>(v.pos);
            std::size_t j = i + 1 < n ? i + 1 : (v.loops != 1 ? 0 : i);
            float t = static_cast<float>(v.pos - static_cast<double>(i));
            float l, r;
            if (p.channels == 2) {
                l = p.samples[i * 2] + (p.samples[j * 2] - p.samples[i * 2]) * t;
                r = p.samples[i * 2 + 1] + (p.samples[j * 2 + 1] - p.samples[i * 2 + 1]) * t;
            } else {
                l = r = p.samples[i] + (p.samples[j] - p.samples[i]) * t;
            }
            out[f * 2] += l * gain * left;
            out[f * 2 + 1] += r * gain * right;
            if (send > 0.0f) wet_[f] += (l + r) * gain * send;
            v.pos += step;
            if (v.step != 0.0f) {
                v.volume += v.step;
                if ((v.step < 0.0f && v.volume <= v.target) || (v.step > 0.0f && v.volume >= v.target)) {
                    v.volume = v.target;
                    v.step = 0.0f;
                    if (v.target <= 0.0f) {
                        v.done = true;
                        break;
                    }
                }
                gain = v.positional ? miles_volume_curve(v.volume) : v.volume;
            }
        }
    }
    if (reverb) reverb_.process(wet_.data(), out, frames);
    // Keep the sum in range; many emitters at once can add up past full scale.
    // A non-finite sample (a source's) is silence, not passed to the device.
    for (std::size_t i = 0; i < frames * 2; ++i) out[i] = std::isfinite(out[i]) ? std::clamp(out[i], -1.0f, 1.0f) : 0.0f;
    if (swap_)
        for (std::size_t f = 0; f < frames; ++f) std::swap(out[f * 2], out[f * 2 + 1]);
}

} // namespace hailstone::audio
