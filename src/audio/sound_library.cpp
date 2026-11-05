#include "audio/sound_library.h"

#include <algorithm>
#include <cctype>

#include "assets/pfs.h"
#include "core/log.h"
#include "platform/crash_log.h"

namespace nmsc::audio {

namespace {
constexpr std::size_t kOpenArchives = 2;
}

std::string lower_case(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

SoundLibrary::SoundLibrary(const GameDir& game) : game_(game) {
    // snd<N>.pfs in the game directory, by number, through every layer (a server's assets folder may replace one or
    // add its own).
    std::vector<std::pair<int, std::filesystem::path>> found;
    for (const GameDir::Entry& entry : game_.list("")) {
        std::string name = lower_case(entry.name);
        if (name.size() > 7 && name.starts_with("snd") && name.ends_with(".pfs")) {
            std::string digits = name.substr(3, name.size() - 7);
            if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                found.emplace_back(std::stoi(digits), entry.path);
        }
    }
    std::sort(found.begin(), found.end());
    for (auto& [n, p] : found) archive_paths_.push_back(p);
    thread_ = std::thread([this] { worker(); });
}

SoundLibrary::~SoundLibrary() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void SoundLibrary::build_index() {
    std::unordered_map<std::string, int> index;
    std::size_t files = 0;
    for (std::size_t i = 0; i < archive_paths_.size(); ++i) {
        {
            std::lock_guard lock(mutex_);
            if (stop_) return;
        }
        try {
            auto a = assets::PfsArchive::open(archive_paths_[i]);
            for (const auto& n : a.names()) {
                index[lower_case(n)] = static_cast<int>(i); // higher numbers override
                ++files;
            }
        } catch (const std::exception& e) {
            log::warn("audio: {}: {}", archive_paths_[i].string(), e.what());
        }
    }
    std::lock_guard lock(mutex_);
    archive_index_ = std::move(index);
    index_ready_ = true;
    log::debug("audio: {} sound archives, {} files", archive_paths_.size(), files);
    cv_.notify_all();
}

std::size_t SoundLibrary::archive_files() const {
    std::lock_guard lock(mutex_);
    return archive_index_.size();
}

std::optional<SoundLibrary::Location> SoundLibrary::locate(const std::string& lower) {
    // Only plain names: no directories, drives or traversal.
    if (lower.empty() || lower.find_first_of("/\\:") != std::string::npos) return std::nullopt;
    if (auto p = game_.find("sounds/" + lower)) return Location{-1, *p};
    if (auto p = game_.find("voice/default/" + lower)) return Location{-1, *p};
    if (auto p = game_.find(lower)) return Location{-1, *p};
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return index_ready_ || stop_; });
    if (auto it = archive_index_.find(lower); it != archive_index_.end()) return Location{it->second, {}};
    return std::nullopt;
}

bool SoundLibrary::exists(const std::string& name) { return locate(lower_case(name)).has_value(); }

std::shared_ptr<const assets::PfsArchive> SoundLibrary::archive(int index) {
    std::lock_guard lock(archive_mutex_);
    for (auto it = open_.begin(); it != open_.end(); ++it)
        if (it->first == index) {
            auto a = it->second;
            open_.erase(it);
            open_.emplace_back(index, a);
            return a;
        }
    auto a = std::make_shared<const assets::PfsArchive>(
        assets::PfsArchive::open(archive_paths_[static_cast<std::size_t>(index)]));
    open_.emplace_back(index, a);
    if (open_.size() > kOpenArchives) open_.erase(open_.begin());
    return a;
}

std::optional<std::vector<std::byte>> SoundLibrary::read(const std::string& name) {
    auto loc = locate(lower_case(name));
    if (!loc) return std::nullopt;
    try {
        if (loc->archive < 0) return read_file(loc->path);
        return archive(loc->archive)->read(lower_case(name));
    } catch (const std::exception& e) {
        log::warn("audio: cannot read {}: {}", name, e.what());
        return std::nullopt;
    }
}

std::shared_ptr<const Pcm> SoundLibrary::wave(const std::string& name) {
    std::string key = lower_case(name);
    std::lock_guard lock(mutex_);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    if (failed_.contains(key) || queued_.contains(key)) return nullptr;
    queued_.insert(key);
    queue_.push_back(key);
    cv_.notify_all();
    return nullptr;
}

void SoundLibrary::preload(const std::vector<std::string>& names) {
    for (const auto& n : names) wave(n);
}

void SoundLibrary::wait_idle() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return stop_ || (index_ready_ && queue_.empty() && !busy_); });
}

void SoundLibrary::worker() {
    platform::CrashStack crash_stack;
    build_index();
    for (;;) {
        std::string key;
        {
            std::unique_lock lock(mutex_);
            busy_ = false;
            cv_.notify_all();
            cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            key = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }
        std::shared_ptr<const Pcm> pcm;
        if (auto bytes = read(key)) {
            if (auto decoded = decode_sound(*bytes)) pcm = std::make_shared<const Pcm>(std::move(*decoded));
        }
        std::lock_guard lock(mutex_);
        queued_.erase(key);
        if (pcm) {
            cache_[key] = std::move(pcm);
        } else {
            failed_.insert(key);
            log::debug("audio: no sound {}", key);
        }
    }
}

} // namespace nmsc::audio
