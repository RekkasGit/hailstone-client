#include "nav/zone_nav.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string_view>

#include "core/atomic_file.h"
#include "core/log.h"
#include "platform/crash_log.h"

namespace hailstone::nav {

ZoneNav::ZoneNav(std::shared_ptr<const InputGeometry> geo, Config cfg, std::function<Area(const glm::vec3&)> classify)
    : geo_(std::move(geo)), cfg_(std::move(cfg)), classify_(std::move(classify)) {
    const BuildParams& p = geo_->params();
    header_.geometry_hash = geo_->hash();
    header_.params_hash = p.hash();
    header_.builder = builder_fingerprint();
    header_.origin = geo_->bmin();
    header_.tile_size = p.tile_size();
    header_.tiles_x = geo_->tiles_x();
    header_.tiles_y = geo_->tiles_y();
    if (geo_->triangle_count() == 0 ||
        !mesh_.init(header_.origin, header_.tile_size, header_.tiles_x, header_.tiles_y, p)) {
        log::warn("nav: no navigation mesh ({} triangles, {} x {} tiles)", geo_->triangle_count(), header_.tiles_x,
                  header_.tiles_y);
        return;
    }
    for (int ty = 0; ty < header_.tiles_y; ++ty)
        for (int tx = 0; tx < header_.tiles_x; ++tx)
            if (!geo_->tile_triangles(tx, ty).empty()) jobs_.push_back({tx, ty});
    total_ = jobs_.size();

    if (!cfg_.cache_file.empty()) {
        if (auto text = read_text_file(cfg_.cache_file)) {
            if (auto tiles = decode_cache(std::as_bytes(std::span(text->data(), text->size())), header_)) {
                std::size_t added = 0;
                for (const CachedTile& t : *tiles) {
                    if (!mesh_.add_tile(t.data)) break;
                    mesh_.mark_areas(t.tx, t.ty, classify_, cfg_.area_lift);
                    ++added;
                }
                if (added == tiles->size()) {
                    from_cache_ = true;
                    jobs_.clear();
                    done_ = total_;
                    log::info("nav: {} tiles read from {}", added, cfg_.cache_file.string());
                    return;
                }
                log::warn("nav: {} holds a tile Detour refuses; building again", cfg_.cache_file.string());
                mesh_.init(header_.origin, header_.tile_size, header_.tiles_x, header_.tiles_y, p);
            }
        }
    }
    log::info("nav: building {} tiles ({} x {}, {} triangles) on {} threads", total_, header_.tiles_x, header_.tiles_y,
              geo_->triangle_count(), cfg_.threads);
    for (int i = 0; i < std::max(1, cfg_.threads); ++i) workers_.emplace_back([this] { work(); });
}

ZoneNav::~ZoneNav() {
    stop_ = true;
    for (std::thread& t : workers_) t.join();
}

std::filesystem::path ZoneNav::cache_name(std::string_view zone) {
    std::string name(zone.empty() ? std::string_view("zone") : zone);
    for (char& c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
    return name + ".navmesh";
}

void ZoneNav::work() {
    platform::CrashStack crash_stack;
    for (;;) {
        Job job;
        {
            std::lock_guard lock(mutex_);
            if (stop_ || jobs_.empty()) return;
            // The tile nearest the player next.
            std::size_t best = 0;
            float best_d = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < jobs_.size(); ++i) {
                glm::vec2 c(static_cast<float>(jobs_[i].tx) + 0.5f, static_cast<float>(jobs_[i].ty) + 0.5f);
                glm::vec2 d = c - focus_;
                float dd = glm::dot(d, d);
                if (dd < best_d) {
                    best_d = dd;
                    best = i;
                }
            }
            job = jobs_[best];
            jobs_[best] = jobs_.back();
            jobs_.pop_back();
        }
        CachedTile t{job.tx, job.ty, build_tile(*geo_, job.tx, job.ty)};
        if (stop_) return;
        {
            std::lock_guard lock(mutex_);
            if (!t.data.empty()) {
                if (!cfg_.cache_file.empty()) built_.push_back(t);
                ready_.push_back(std::move(t));
            }
        }
        if (done_.fetch_add(1) + 1 == total_) save_cache();
    }
}

void ZoneNav::save_cache() {
    if (cfg_.cache_file.empty() || stop_) return;
    std::vector<CachedTile> tiles;
    {
        std::lock_guard lock(mutex_);
        tiles.swap(built_); // built_ is only for this file
    }
    // A stable order, so the same zone gives the same file.
    std::sort(tiles.begin(), tiles.end(), [](const CachedTile& a, const CachedTile& b) {
        return a.ty != b.ty ? a.ty < b.ty : a.tx < b.tx;
    });
    std::vector<std::byte> bytes = encode_cache(header_, tiles);
    if (write_file_atomic(cfg_.cache_file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())))
        log::info("nav: {} tiles ({} KiB) written to {}", tiles.size(), bytes.size() / 1024, cfg_.cache_file.string());
}

bool ZoneNav::ready_empty() const {
    std::lock_guard lock(mutex_);
    return ready_.empty();
}

void ZoneNav::add(const CachedTile& t) {
    if (mesh_.add_tile(t.data)) mesh_.mark_areas(t.tx, t.ty, classify_, cfg_.area_lift);
    else log::warn("nav: Detour refused tile {},{}", t.tx, t.ty);
}

void ZoneNav::update(const glm::vec3& focus) {
    std::vector<CachedTile> ready;
    {
        std::lock_guard lock(mutex_);
        glm::vec3 r = to_recast(focus);
        if (header_.tile_size > 0.0f)
            focus_ = glm::vec2((r.x - header_.origin.x) / header_.tile_size, (r.z - header_.origin.z) / header_.tile_size);
        ready.swap(ready_);
    }
    for (const CachedTile& t : ready) add(t);
}

void ZoneNav::finish() {
    for (std::thread& t : workers_) t.join();
    workers_.clear();
    std::vector<CachedTile> ready;
    {
        std::lock_guard lock(mutex_);
        ready.swap(ready_);
    }
    for (const CachedTile& t : ready) add(t);
}

} // namespace hailstone::nav
