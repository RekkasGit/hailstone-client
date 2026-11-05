#pragma once

// Finds and decodes the game's sound files. EqSoundManager::GetAsset @0x4aa2a0
// looks a name up as "sounds\<name>", then "voice\default\<name>", then
// through PackFS (the loose game directory and the opened archives; the sounds
// are in snd1.pfs .. snd17.pfs). Which archive wins when several hold the same
// name is not known (docs/unknowns.md); here the higher-numbered one does.
//
// Decoding runs on a worker thread: wave() returns the sound when it is ready
// and otherwise queues it, so the game never waits on a decode.

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

namespace nmsc::assets {
class PfsArchive;
}

namespace nmsc::audio {

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
    void preload(const std::vector<std::string>& names);
    // Blocks until the queue is empty (tests and tools).
    void wait_idle();

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
    std::deque<std::string> queue_;
    std::unordered_set<std::string> queued_, failed_;
    std::unordered_map<std::string, std::shared_ptr<const Pcm>> cache_;
    bool busy_ = false;

    std::mutex archive_mutex_;
    std::vector<std::pair<int, std::shared_ptr<const assets::PfsArchive>>> open_; // most recent last

    std::thread thread_;
};

std::string lower_case(std::string_view s);

} // namespace nmsc::audio
