#pragma once

// Finds and decodes the game's sound files. EqSoundManager::GetAsset @0x4aa2a0
// looks a name up as "sounds\<name>", then "voice\default\<name>", then
// through PackFS (the loose game directory and the opened archives; the sounds
// are in snd1.pfs .. snd17.pfs). Which archive wins when several hold the same
// name is not known (docs/unknowns.md); here the higher-numbered one does.
//
// Decoding runs on a worker thread: wave() returns the sound when it is ready
// and otherwise queues it, so the game never waits on a decode. The music
// files (whole MP3s and XMIs, possibly in a snd*.pfs) are read there too, ahead
// of their first play (set_resident_files). The worker takes a wave asked for
// by wave() first, then a music file, then a wave named by preload(): a zone's
// bulk preload does not hold up the music that starts on zone entry.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "audio/pcm.h"
#include "core/game_dir.h"

namespace hailstone::assets {
class PfsArchive;
}

namespace hailstone::audio {

class SoundLibrary {
public:
    explicit SoundLibrary(const GameDir& game);
    ~SoundLibrary();
    SoundLibrary(const SoundLibrary&) = delete;
    SoundLibrary& operator=(const SoundLibrary&) = delete;

    // Whether the name can be loaded (waits for the archive index on first use).
    bool exists(const std::string& name);
    // The file's bytes (synchronous).
    std::optional<std::vector<std::byte>> read(const std::string& name);
    // The decoded sound if it is ready; otherwise queues the decode and returns nullptr.
    std::shared_ptr<const Pcm> wave(const std::string& name);
    // Queues the decodes behind every wave() and music file; a later wave() of one moves it to the front.
    void preload(const std::vector<std::string>& names);
    // At a zone change: the decoded waves not asked for (wave() or preload()) since the zone change before this one
    // are dropped, unless `pinned` names them or a voice still plays one; one asked for again is decoded again. The
    // cache otherwise kept every wave decoded in the session.
    void drop_idle_waves(const std::vector<std::string>& pinned);
    // How many decoded waves are held (tests).
    std::size_t cached_waves() const;
    // The files to hold as bytes (the zone's and the global music): each one not held yet is read on the worker,
    // and one held that is not named is dropped.
    void set_resident_files(const std::vector<std::string>& names);
    // A resident file's bytes once the worker has read them, else nullptr (not named, not read yet, or missing).
    std::shared_ptr<const std::vector<std::byte>> resident_file(const std::string& name) const;
    // The file's bytes now: a resident one's; one the worker is reading is waited for, one still queued is read here
    // (and kept resident) instead of on the worker; any other is read here. nullptr when it cannot be read, and
    // from then on (it is not read again).
    std::shared_ptr<const std::vector<std::byte>> file(const std::string& name);
    // Blocks until the queue is empty (tests and tools).
    void wait_idle();
    // Holds the worker before its next job, or lets it go on (tests).
    void hold_worker(bool held);
    // When set, the worker appends each job's lower-case name before running it (tests); nullptr stops it.
    void set_job_log(std::vector<std::string>* log);

    std::size_t archive_files() const;

private:
    struct Location {
        int archive = -1; // index into archives_, -1 = loose file
        std::filesystem::path path;
    };
    void worker();
    void build_index();
    std::optional<Location> locate(const std::string& lower);
    std::shared_ptr<const assets::PfsArchive> archive(int index);

    const GameDir& game_;
    std::vector<std::filesystem::path> archive_paths_;
    std::unordered_map<std::string, int> archive_index_; // lower-case name -> archive

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool index_ready_ = false;
    bool stop_ = false;
    std::deque<std::string> queue_, preload_queue_; // wanted now; preload(), behind the music files
    std::unordered_set<std::string> queued_, failed_; // queued_: in either queue or decoding
    struct Cached {
        std::shared_ptr<const Pcm> pcm;
        std::uint32_t used = 0; // generation_ when last asked for
    };
    std::unordered_map<std::string, Cached> cache_;
    std::uint32_t generation_ = 0; // drop_idle_waves calls
    std::deque<std::string> file_queue_;
    std::unordered_set<std::string> resident_names_, missing_files_; // lower case
    std::unordered_map<std::string, std::shared_ptr<const std::vector<std::byte>>> resident_;
    std::string file_in_flight_; // the music file the worker is reading, else empty
    bool busy_ = false;
    bool held_ = false;
    std::vector<std::string>* job_log_ = nullptr;

    std::mutex archive_mutex_;
    std::vector<std::pair<int, std::shared_ptr<const assets::PfsArchive>>> open_; // most recent last

    std::thread thread_;
};

std::string lower_case(std::string_view s);

} // namespace hailstone::audio
