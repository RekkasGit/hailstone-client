#include "audio/music_manager.h"

#include <algorithm>
#include <cctype>

#include "audio/zone_emitters.h"

namespace hailstone::audio {

MusicSelectResult select_active_entry(std::span<const MusicSelectEntry> entries, int current, bool music_enabled,
                                      float volume) {
    MusicSelectResult r;
    auto valid = [&](int i) {
        const MusicSelectEntry& e = entries[static_cast<std::size_t>(i)];
        return e.selected && !e.finished;
    };
    int best = current >= 0 && valid(current) ? current : -1;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        if (valid(i) && (best < 0 || entries[static_cast<std::size_t>(i)].priority <
                                         entries[static_cast<std::size_t>(best)].priority))
            best = i;
    if (best != current && current >= 0) {
        r.fade_out = current;
        r.fade_out_nothing_replaces = best < 0;
    }
    r.playing = best;
    if (best < 0) return r;
    if (music_enabled) r.fade_in = entries[static_cast<std::size_t>(best)].start_requested && 0.0f < volume;
    else r.fade_instances = true;
    return r;
}

std::vector<MusicSetCall> load_global_music(const std::function<bool(const std::string&)>& found,
                                            std::int32_t rand_value) {
    auto has = [&](const char* f) { return found(f); };
    const std::string gl = has("gl.xmi") ? "gl.xmi" : "";
    std::vector<MusicSetCall> out;
    auto add = [&](std::int32_t entry, std::string asset, std::int32_t track, std::int32_t priority, std::int32_t in,
                   std::int32_t replaced, std::int32_t stopped, bool mp3, std::int32_t unused = 0) {
        MusicSetCall c;
        c.entry = entry;
        c.pool = mp3 && !asset.empty() ? 1 : 0;
        c.asset = std::move(asset);
        c.track = track;
        c.unused = unused;
        c.priority = priority;
        c.fade_in_ms = in;
        c.fade_out_replaced_ms = replaced;
        c.fade_out_stopped_ms = stopped;
        out.push_back(std::move(c));
    };
    // 1 and 4: the theme, eqtheme.mp3 or the opener sequences.
    if (has("eqtheme.mp3")) {
        add(1, "eqtheme.mp3", 0, 10, 0, 1500, 5000, true);
        add(4, "eqtheme.mp3", 0, 10, 0, 1500, 5000, true);
    } else {
        std::string opener = has("opener4.xmi") ? "opener4.xmi" : "";
        add(1, opener, 0, 10, 0, 1500, 5000, false);
        add(4, opener, 1, 9, 0, 1500, 5000, false);
    }
    // 2: combat, one of the two combat themes by rand()'s parity, else gl.xmi's sequence 0.
    const char* combat = (rand_value & 1) != 0 ? "combattheme1.mp3" : "combattheme2.mp3";
    if (has(combat)) add(2, combat, 0, 3, 1000, 3000, 4000, true);
    else add(2, gl, 0, 3, 1500, 3000, 4000, false);
    // 8: death.
    if (has("deaththeme.mp3")) add(8, "deaththeme.mp3", 0, 2, 1500, 1500, 2500, true, 1);
    else add(8, gl, 3, 2, 1500, 1500, 2500, false, 1);
    // gl.xmi's other sequences.
    add(3, gl, 21, 5, 1500, 2500, 3500, false);
    add(6, gl, 21, 5, 1500, 2500, 3500, false);
    add(7, gl, 23, 5, 1500, 2500, 3500, false);
    add(15, gl, 19, 5, 1500, 2500, 3500, false);
    add(9, gl, 20, 5, 1500, 2500, 3500, false);
    add(10, gl, 7, 6, 1500, 2500, 3500, false);
    add(11, gl, 8, 5, 1500, 1500, 2500, false);
    add(12, gl, 9, 5, 1500, 1500, 2500, false);
    add(13, gl, 10, 5, 1500, 1500, 2500, false);
    add(14, gl, 11, 5, 1500, 1500, 2500, false);
    // 5: no asset.
    add(5, "", 0, 0, 1600, 1400, 10000, false);
    return out;
}

void MusicManager::set(MusicEntry entry) {
    // Clear: unlinked, and no entry is playing if it was the one.
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const MusicEntry& e) { return e.id == entry.id; });
    if (it != entries_.end()) {
        int index = static_cast<int>(it - entries_.begin());
        entries_.erase(it);
        if (current_ == index) current_ = -1;
        else if (current_ > index) --current_;
    }
    entries_.insert(entries_.begin(), std::move(entry));
    if (current_ >= 0) ++current_;
}

MusicEntry* MusicManager::find(std::int32_t id) {
    for (auto& e : entries_)
        if (e.id == id) return &e;
    return nullptr;
}

MusicManager::Voice* MusicManager::voice_of(const std::string& file) {
    for (auto& v : voices_)
        if (v.file == file) return &v;
    return nullptr;
}

void MusicManager::reap(EmitterSink& sink) {
    for (auto& v : voices_) {
        if (!v.voice || sink.alive(v.voice)) continue;
        v.voice = 0;
        // Only an mp3's instances are 2D samples (type 0); an XMI plays as a sequence (type 2).
        bool mp3 = v.file.size() >= 4 && std::equal(v.file.end() - 4, v.file.end(), ".mp3", [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == b;
                   });
        if (!v.finish_when_done || !mp3) continue;
        for (auto& e : entries_)
            if (e.file == v.file) e.finished = true;
    }
}

void MusicManager::remove(std::int32_t id, EmitterSink& sink) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const MusicEntry& e) { return e.id == id; });
    if (it == entries_.end()) return;
    int index = static_cast<int>(it - entries_.begin());
    std::string file = it->file;
    entries_.erase(it);
    if (current_ == index) current_ = -1;
    else if (current_ > index) --current_;
    // Its asset goes with it (ReleaseZoneSpecificEmitters): a voice still playing it stops at once.
    bool shared = std::any_of(entries_.begin(), entries_.end(), [&](const MusicEntry& e) { return e.file == file; });
    if (Voice* v = voice_of(file); v && v->voice && !shared) {
        sink.release(v->voice, 0);
        v->voice = 0;
    }
}

void MusicManager::play(std::int32_t id, EmitterSink& sink) {
    // No entry with that id: nothing at all.
    MusicEntry* e = find(id);
    if (!e) return;
    e->playable = true;
    e->active = true;
    select(sink);
}

void MusicManager::stop(std::int32_t id, EmitterSink& sink) {
    if (MusicEntry* e = find(id)) e->active = false;
    select(sink);
}

void MusicManager::select(EmitterSink& sink) {
    std::vector<MusicSelectEntry> view;
    view.reserve(entries_.size());
    for (const auto& e : entries_) view.push_back({e.active, e.finished, e.priority, e.playable});
    MusicSelectResult r = select_active_entry(view, current_, enabled_, volume_);
    if (r.fade_out >= 0) {
        // FadeOut: the cross-fade when another entry takes over, else the entry's own fade-out.
        const MusicEntry& old = entries_[static_cast<std::size_t>(r.fade_out)];
        if (Voice* v = voice_of(old.file); v && v->voice) {
            sink.release(v->voice, r.fade_out_nothing_replaces ? old.fade_out_ms : old.crossfade_ms);
        }
    }
    current_ = r.playing;
    if (current_ < 0) return;
    const MusicEntry& m = entries_[static_cast<std::size_t>(current_)];
    Voice* v = voice_of(m.file);
    if (r.fade_instances) {
        if (v && v->voice) sink.release(v->voice, m.crossfade_ms);
        return;
    }
    if (!r.fade_in || m.file.empty()) return;
    // FadeIn: only when the entry's asset is not playing (a fading one included).
    if (v && v->voice && sink.alive(v->voice)) return;
    // The entry's own volume: the music setting (volume_) only gates FadeIn above; the mixer's music bus applies it
    // to every music voice, so it is not multiplied in here a second time.
    std::uint32_t started = sink.play_music(m, m.volume);
    if (!v) v = &voices_.emplace_back(Voice{m.file, 0, false});
    v->voice = started;
    v->finish_when_done = m.finish_when_done;
}

void MusicManager::release_all(EmitterSink& sink) {
    for (auto& v : voices_)
        if (v.voice) sink.release(v.voice, 0);
    voices_.clear();
    current_ = -1;
}

} // namespace hailstone::audio
