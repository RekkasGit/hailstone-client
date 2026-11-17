#include "app/distant_copies.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>

#include "app/world_atlas.h"
#include "assets/zone.h"
#include "core/atomic_file.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "platform/crash_log.h"
#include "platform/paths.h"
#include "platform/process.h"
#include "render/zone_proxy.h"

namespace hailstone::app {

namespace {

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

std::optional<std::string> read_whole(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

std::vector<DistantCopies::Placed> copy_placements(const AtlasLand& atlas,
                                                   const std::vector<NeighbourZones::CopyTarget>& neighbours) {
    std::vector<DistantCopies::Placed> out;
    for (const auto& p : atlas.placed_zones) {
        DistantCopies::Placed c{p.zone, p.offset, {}};
        if (auto it = atlas.cuts.find(p.zone); it != atlas.cuts.end()) c.keep = it->second;
        out.push_back(std::move(c));
    }
    for (const auto& n : neighbours) {
        if (std::any_of(out.begin(), out.end(), [&](const DistantCopies::Placed& c) { return c.zone == n.name; }))
            continue;
        out.push_back({n.name, n.offset, {n.keep}});
    }
    return out;
}

std::filesystem::path zone_proxy_path(std::string_view zone) {
    return platform::cache_dir() / "proxies" / (std::string(zone) + ".bin");
}

bool zone_proxy_cached(const GameDir& game, std::string_view zone) {
    std::ifstream in(zone_proxy_path(zone), std::ios::binary);
    if (!in) return false;
    std::string head(128, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));
    const std::uint64_t key = atlas_zone_key(game, zone);
    const auto header = render::read_zone_proxy_header(std::as_bytes(std::span(head.data(), head.size())));
    return key != 0 && header && header->first == zone && header->second == key;
}

render::HorizonSources without_copied_trees(const GameDir& game, const render::HorizonSources& sources) {
    render::HorizonSources out;
    for (const auto& s : sources) {
        if (s->trees.empty() || !zone_proxy_cached(game, s->name)) {
            out.push_back(s);
            continue;
        }
        auto copy = std::make_shared<render::HorizonSource>(*s);
        copy->trees.clear();
        out.push_back(std::move(copy));
    }
    return out;
}

struct DistantCopies::Job {
    // In.
    DistantCopies::Placed placed;
    std::shared_ptr<const render::HorizonBuild> horizon;
    std::uint32_t max_images = 0;
    // Out.
    render::ZoneRenderer::PreparedPtr prepared;
    std::vector<std::int64_t> footprint;
    std::size_t triangles = 0, trees = 0;
    bool from_cache = false;
    double bake_ms = 0.0, ms = 0.0;
    std::string error;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
};

DistantCopies::DistantCopies(render::VkContext& vk, const GameDir& game, std::filesystem::path shader_dir)
    : vk_(vk), game_(game), shader_dir_(std::move(shader_dir)) {}

DistantCopies::~DistantCopies() {
    clear();
    for (auto& a : abandoned_)
        if (a.thread.joinable()) a.thread.join();
}

void DistantCopies::enter(std::vector<Placed> zones, std::shared_ptr<const render::HorizonBuild> horizon) {
    clear();
    if (!horizon) return;
    horizon_ = std::move(horizon);
    zones_ = std::move(zones);
    next_ = 0;
}

void DistantCopies::clear() {
    if (job_) {
        job_->cancelled = true;
        abandoned_.push_back({std::move(job_), std::move(thread_)});
        job_.reset();
    }
    copies_.clear(); // the renderers wait for the GPU as they go
    zones_.clear();
    next_ = 0;
    horizon_.reset();
}

void DistantCopies::run(std::shared_ptr<Job> job, const GameDir* game) {
    platform::CrashStack crash_stack;
    platform::lower_current_thread_priority(); // the frame and the network come first
    const auto t0 = Clock::now();
    try {
        const std::string& name = job->placed.zone;
        const std::uint64_t key = atlas_zone_key(*game, name);
        if (key == 0) throw std::runtime_error("its files are not in the asset folder");
        std::optional<render::ZoneProxy> proxy;
        const auto path = zone_proxy_path(name);
        if (auto bytes = read_whole(path)) {
            proxy = render::read_zone_proxy(std::as_bytes(std::span(bytes->data(), bytes->size())));
            if (proxy && (proxy->zone != name || proxy->key != key)) proxy.reset();
            job->from_cache = proxy.has_value();
        }
        if (!proxy) {
            const auto b0 = Clock::now();
            assets::Zone zone;
            try {
                zone = assets::load_zone(*game, name, [&] { return job->cancelled.load(); });
            } catch (const assets::ZoneLoadCancelled&) {
                job->done = true;
                return;
            }
            if (job->cancelled) {
                job->done = true;
                return;
            }
            proxy = render::bake_zone_proxy(zone, key, &job->cancelled);
            if (job->cancelled) {
                job->done = true;
                return;
            }
            proxy->zone = name;
            proxy->key = key;
            const auto bytes = render::write_zone_proxy(*proxy);
            write_file_atomic(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            job->bake_ms = ms_since(b0);
        }
        if (!proxy->valid()) throw std::runtime_error("nothing to draw");
        render::ProxyZone copy = render::proxy_zone(*proxy, job->horizon ? &job->horizon->field : nullptr,
                                                    job->placed.offset, job->placed.keep);
        job->footprint = std::move(copy.footprint);
        job->triangles = copy.triangles;
        job->trees = copy.trees;
        if (copy.zone.terrain.empty()) throw std::runtime_error("nothing far enough out to draw");
        render::ZoneRenderOptions options;
        // Moved by its offset (a zone placed with none still goes the moved way: no land of its own).
        options.offset = job->placed.offset == glm::vec3(0.0f) ? glm::vec3(0.0f, 0.0f, -0.01f) : job->placed.offset;
        options.keep = job->placed.keep;
        options.details = false;
        options.material_maps = false;
        options.land_trees = false;
        options.cancelled = [job] { return job->cancelled.load(); };
        job->prepared = render::ZoneRenderer::prepare(copy.zone, options, job->max_images);
        if (!job->prepared && !job->cancelled) throw std::runtime_error("not prepared");
    } catch (const std::exception& e) {
        job->error = e.what();
    }
    job->ms = ms_since(t0);
    job->done = true;
}

void DistantCopies::finish(Job& job) {
    const std::string& name = job.placed.zone;
    if (!job.error.empty()) {
        log::info("distant copy {}: not drawn: {}", name, job.error);
        return;
    }
    if (!job.prepared) return;
    const auto t0 = Clock::now();
    try {
        Copy c;
        c.zone = name;
        c.renderer = std::make_unique<render::ZoneRenderer>(vk_, std::move(job.prepared), shader_dir_);
        c.footprint = std::move(job.footprint);
        c.id = next_id_++;
        log::info("distant copy {}: {} in {:.0f} ms on the loader{}, uploaded in {:.1f} ms: {} triangles, {} trees, {} "
                  "squares of the land hidden under it",
                  name, job.from_cache ? "from the cache" : "baked", job.ms,
                  job.from_cache ? "" : std::format(" (bake {:.0f} ms)", job.bake_ms), ms_since(t0), job.triangles,
                  job.trees, c.footprint.size());
        copies_.push_back(std::move(c));
    } catch (const std::exception& e) {
        log::warn("distant copy {}: upload failed: {}", name, e.what());
    }
}

void DistantCopies::start_next() {
    if (job_ || !horizon_ || next_ >= zones_.size()) return;
    job_ = std::make_shared<Job>();
    job_->placed = zones_[next_++];
    job_->horizon = horizon_;
    job_->max_images = vk_.caps().max_sampled_images;
    thread_ = std::thread(&DistantCopies::run, job_, &game_);
}

void DistantCopies::update(bool enabled, const std::vector<std::string>& live) {
    std::erase_if(abandoned_, [](Abandoned& a) {
        if (!a.job->done) return false;
        if (a.thread.joinable()) a.thread.join();
        return true;
    });
    if (job_ && job_->done) {
        if (thread_.joinable()) thread_.join();
        finish(*job_);
        job_.reset();
    }
    enabled_ = enabled;
    live_ = live;
    if (enabled_) start_next();
}

void DistantCopies::finish_loading() {
    while (busy()) {
        if (job_) {
            if (thread_.joinable()) thread_.join();
            finish(*job_);
            job_.reset();
        }
        start_next();
    }
}

std::size_t DistantCopies::loaded() const { return copies_.size(); }

void DistantCopies::set_scene_light(const game::SceneLight& light) {
    for (auto& c : copies_) c.renderer->set_scene_light(light);
}

void DistantCopies::set_lighting(const render::SceneLighting& lighting) {
    for (auto& c : copies_) c.renderer->set_lighting(lighting);
}

void DistantCopies::draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
                         const game::ViewDistance& view, double time_seconds, render::ZonePhase phase) {
    if (!enabled_) return;
    for (auto& c : copies_) {
        if (std::find(live_.begin(), live_.end(), c.zone) != live_.end()) continue;
        c.renderer->draw(frame, view_proj, camera_pos, view, time_seconds, phase);
        (void)c.renderer->take_drawn();
    }
}

void DistantCopies::footprint(std::vector<std::int64_t>& out) const {
    if (!enabled_) return;
    for (const auto& c : copies_)
        if (std::find(live_.begin(), live_.end(), c.zone) == live_.end())
            out.insert(out.end(), c.footprint.begin(), c.footprint.end());
}

std::uint64_t DistantCopies::drawn_key() const {
    if (!enabled_) return 0;
    std::uint64_t key = 0;
    for (const auto& c : copies_)
        if (std::find(live_.begin(), live_.end(), c.zone) == live_.end()) key = key * 1000003u + c.id;
    return key;
}

} // namespace hailstone::app
