#pragma once

// One zone's navigation mesh as it comes to be: read from the cache when the zone's geometry and the build's
// parameters match a file there, otherwise built tile by tile on worker threads, nearest the player first, so the
// ground under the player is usable within moments of the zone-in. A finished build is written to the cache.

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "nav/nav_mesh.h"

namespace hailstone::nav {

class ZoneNav {
public:
    struct Config {
        int threads = 2;                  // tile builders
        std::filesystem::path cache_file; // empty: no cache
        float area_lift = 2.0f;           // how far above a polygon its area is looked up (a standing body's middle)
    };
    // `classify` gives a world point's area (from the zone's regions); main thread only.
    ZoneNav(std::shared_ptr<const InputGeometry> geo, Config cfg, std::function<Area(const glm::vec3&)> classify);
    ~ZoneNav();
    ZoneNav(const ZoneNav&) = delete;
    ZoneNav& operator=(const ZoneNav&) = delete;

    // Main thread, once a frame: adds the tiles built since, and moves the builders' attention to `focus` (world).
    void update(const glm::vec3& focus);
    // Waits until every tile is built and added (tests and tools).
    void finish();

    NavMesh& mesh() { return mesh_; }
    const NavMesh& mesh() const { return mesh_; }
    std::size_t tiles_done() const { return done_.load(); }
    std::size_t tiles_total() const { return total_; }
    bool complete() const { return done_.load() == total_ && ready_empty(); }
    bool from_cache() const { return from_cache_; }

    // The cache file's name for a zone (its short name made safe for a file name).
    static std::filesystem::path cache_name(std::string_view zone);

private:
    struct Job {
        int tx = 0, ty = 0;
    };
    void work();
    void add(const CachedTile& t);
    bool ready_empty() const;
    void save_cache();

    std::shared_ptr<const InputGeometry> geo_;
    Config cfg_;
    std::function<Area(const glm::vec3&)> classify_;
    NavMesh mesh_;
    CacheHeader header_;
    bool from_cache_ = false;
    std::size_t total_ = 0;

    mutable std::mutex mutex_;
    std::vector<Job> jobs_;               // not started
    std::vector<CachedTile> ready_;       // built, not yet added
    std::vector<CachedTile> built_;       // every non-empty tile, for the cache
    glm::vec2 focus_{0.0f};               // in tiles
    std::atomic<std::size_t> done_{0};    // built (empty ones too)
    std::atomic<bool> stop_{false};
    std::vector<std::thread> workers_;
};

} // namespace hailstone::nav
