#include "game/map_files.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>

#include "core/game_dir.h"
#include "game/map_data.h"

namespace hailstone::game {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A file in `dir` by name in any case (map files come from Windows).
std::optional<fs::path> find_in(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    fs::path exact = dir / name;
    if (fs::is_regular_file(exact, ec)) return exact;
    if (!fs::is_directory(dir, ec)) return std::nullopt;
    std::string want = lower(name);
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && lower(e.path().filename().string()) == want) return e.path();
    return std::nullopt;
}

// A subfolder of `dir` by name in any case, or `dir` itself for "".
std::optional<fs::path> subdir(const fs::path& dir, std::string_view name) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return std::nullopt;
    if (name.empty()) return dir;
    std::string want = lower(std::string(name));
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec) && lower(e.path().filename().string()) == want) return e.path();
    return std::nullopt;
}

// The layers one source has in one maps directory ("" maps itself).
MapFiles look(const MapFolders& f, MapSource s, std::string_view zone, std::string_view dir) {
    MapFiles out;
    out.dir = std::string(dir);
    for (int layer = 0; layer < 4; ++layer) {
        std::string name = map_layer_file_name(zone, layer);
        auto& slot = out.layers[static_cast<std::size_t>(layer)];
        if (s == MapSource::Own || s == MapSource::Global) {
            if (auto d = subdir(s == MapSource::Own ? f.own : f.global, dir)) slot = find_in(*d, name);
        } else if (f.game) {
            std::string rel = dir.empty() ? "maps/" + name : "maps/" + std::string(dir) + "/" + name;
            // The GameDir's lookup comes from its highest layer: the server's when the overlay has the file.
            if (f.game->from_overlay(rel) == (s == MapSource::Server)) slot = f.game->find(rel);
        }
        if (slot) out.source = s;
    }
    return out;
}

} // namespace

const char* map_source_name(MapSource s) {
    switch (s) {
    case MapSource::Own: return "own";
    case MapSource::Server: return "server";
    case MapSource::Global: return "global";
    case MapSource::Game: return "game";
    case MapSource::None: break;
    }
    return "none";
}

MapFiles find_map_files(const MapFolders& f, std::string_view zone, std::string_view skin) {
    if (map_layer_file_name(zone, 0).empty()) return {}; // not a zone short name: no file of it is read
    std::vector<std::string_view> dirs;
    if (!skin.empty()) dirs.push_back(skin);
    dirs.push_back({});
    for (std::string_view dir : dirs)
        for (MapSource s : {MapSource::Own, MapSource::Server, MapSource::Global, MapSource::Game})
            if (MapFiles m = look(f, s, zone, dir); m.source != MapSource::None) return m;
    return {};
}

std::vector<std::string> map_skins(const MapFolders& f) {
    std::map<std::string, std::string> by_lower; // the first source's spelling
    auto add = [&](std::string name) {
        if (name.empty() || name[0] == '.') return;
        by_lower.try_emplace(lower(name), std::move(name));
    };
    std::error_code ec;
    for (const fs::path& root : {f.own, f.global})
        if (!root.empty() && fs::is_directory(root, ec))
            for (const auto& e : fs::directory_iterator(root, ec))
                if (e.is_directory(ec)) add(e.path().filename().string());
    if (f.game)
        for (auto& d : f.game->list_dirs("maps")) add(std::move(d));
    std::vector<std::string> out;
    for (auto& [k, v] : by_lower) out.push_back(std::move(v));
    return out;
}

} // namespace hailstone::game
