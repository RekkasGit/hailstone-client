// chrdump [--game-dir DIR] [--zone ZONE] [--luclin]    list the archives and model codes
//                                                      (--luclin: with the Luclin player models)
// chrdump [--game-dir DIR] [--zone ZONE] CODE          skeleton, mesh and clips of one model
// chrdump [--game-dir DIR] [--zone ZONE] --names TEXT  fragments whose name contains TEXT
// chrdump [--game-dir DIR] [--zone ZONE] --sweep       build every model; report failures and
//                                                      how many race ids have a classic model
// chrdump [--game-dir DIR] [--zone ZONE] [--luclin] --textures   load every model's textures as the client draws
//                                                      them (classic, Luclin and EQG); list each one that fails
//                                                      (those models show untextured, grey) and why
// The game directory defaults to $EQ_GAME_DIR.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <string>

#include "assets/character.h"
#include "core/game_dir.h"
#include "game/appearance.h"
#include "game/race_models.h"
#include "render/material_state.h"

using namespace hailstone;
using namespace hailstone::assets;

namespace {

void dump_model(const CharacterModel& m) {
    std::printf("%s from %s%s\n", m.code.c_str(), m.source.c_str(), m.eqg ? " (EQG)" : "");
    std::printf("  %zu bones, %zu vertices, %zu triangles, %zu parts, %zu materials, donor %s\n", m.bone_names.size(),
                m.vertices.size(), m.indices.size() / 3, m.parts.size(), m.materials.size(),
                m.animation_donor.empty() ? "-" : m.animation_donor.c_str());
    std::printf("  rest bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n", m.bounds_min.x, m.bounds_min.y, m.bounds_min.z,
                m.bounds_max.x, m.bounds_max.y, m.bounds_max.z);
    std::vector<glm::mat4> rest;
    m.sample(m.clips.at(""), 0.0f, false, rest);
    for (std::size_t i = 0; i < m.bone_names.size(); ++i) {
        std::printf("  bone %2zu parent %2d %s\n", i, m.parents[i], m.bone_names[i].c_str());
        const std::string& b = m.bone_names[i];
        if (b.find("POINT") != std::string::npos || b.find("WEAP") != std::string::npos ||
            b.find("SHLD") != std::string::npos)
            for (int c = 0; c < 4; ++c)
                std::printf("      rest axis %d: (%.2f %.2f %.2f)\n", c, rest[i][c].x, rest[i][c].y, rest[i][c].z);
    }
    for (const auto& mat : m.materials)
        std::printf("  material %-20s blend %d raw %#x %s\n", mat.name.c_str(), static_cast<int>(mat.blend),
                    mat.raw_render_method, mat.frames.empty() ? "-" : mat.frames.front().c_str());
    for (const auto& [code, clip] : m.clips) {
        if (code.empty()) continue;
        std::printf("  clip %s %3u frames x %3u ms = %.2f s%s\n", code.c_str(), clip.frame_count, clip.frame_ms,
                    clip.duration_seconds(), clip.borrowed ? " (borrowed)" : "");
    }
}

// Every texture of every model, in its default look (dress() with a default appearance: the Luclin layered
// materials too). Returns the number of failures.
int check_textures(const CharacterLibrary& lib) {
    std::vector<std::string> codes = lib.model_codes();
    std::size_t wld_codes = codes.size();
    for (const auto& c : lib.eqg_model_codes())
        if (std::find(codes.begin(), codes.end(), c) == codes.end()) codes.push_back(c);
    std::map<std::string, std::string> failed;            // texture -> reason
    std::map<std::string, std::set<std::string>> users;  // texture -> models
    std::map<std::string, int> by_reason;
    std::set<std::string> checked;
    std::size_t models = 0, broken_models = 0, unbuilt = 0;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const std::string& code = codes[i];
        auto model = lib.model(code);
        if (!model) {
            ++unbuilt;
            continue;
        }
        ++models;
        auto mesh = lib.dress(*model, game::Appearance{});
        bool broken = false;
        auto check = [&](const std::string& name, bool masked) {
            if (name.empty()) return;
            std::string key = name + (masked ? "#m" : "");
            if (checked.insert(key).second) {
                std::string error;
                if (!lib.texture(name, masked, &error)) {
                    failed[name] = error;
                    ++by_reason[error.substr(0, error.find(':'))];
                }
            }
            if (failed.contains(name)) {
                users[name].insert(code + (i >= wld_codes ? " (EQG)" : ""));
                broken = true;
            }
        };
        for (const auto& m : mesh->materials) {
            if (m.blend == BlendMode::Invisible || m.frames.empty()) continue;
            check(m.frames.front(), render::material_state(m).masked_texture);
            check(m.tint_layer, false);
        }
        broken_models += broken;
    }
    for (const auto& [name, reason] : failed) {
        std::string who;
        for (const auto& u : users[name]) who += (who.empty() ? "" : ", ") + u;
        std::printf("  %-40s %s  [%s]\n", name.c_str(), reason.c_str(), who.c_str());
    }
    std::printf("%zu models (%zu could not be built), %zu textures checked, %zu failed, %zu models with an "
                "untextured part\n",
                models, unbuilt, checked.size(), failed.size(), broken_models);
    for (const auto& [reason, n] : by_reason) std::printf("  %4d x %s\n", n, reason.c_str());
    return failed.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string dir = std::getenv("EQ_GAME_DIR") ? std::getenv("EQ_GAME_DIR") : "";
        std::string zone, code;
        bool sweep = false, luclin = false, textures = false;
        std::string names;
        for (int i = 1; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) dir = argv[++i];
            else if (!std::strcmp(argv[i], "--zone") && i + 1 < argc) zone = argv[++i];
            else if (!std::strcmp(argv[i], "--sweep")) sweep = true;
            else if (!std::strcmp(argv[i], "--luclin")) luclin = true;
            else if (!std::strcmp(argv[i], "--textures")) textures = true;
            else if (!std::strcmp(argv[i], "--names") && i + 1 < argc) names = argv[++i];
            else code = argv[i];
        }
        if (dir.empty()) {
            std::fprintf(stderr, "usage: chrdump [--game-dir DIR] [--zone ZONE] [--luclin] [CODE | --sweep | --textures]\n");
            return 2;
        }
        GameDir game(dir);
        auto t0 = std::chrono::steady_clock::now();
        auto lib = CharacterLibrary::load(game, zone, luclin);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (textures) return check_textures(lib);
        if (!names.empty()) {
            // Fragment names containing the text, with their type and file.
            for (const auto& a : lib.archives())
                for (const auto& w : a->wlds)
                    for (const auto& f : w.fragments())
                        if (f.name.find(names) != std::string::npos)
                            std::printf("%02x %s %s\n", f.type, f.name.c_str(), w.label().c_str());
            return 0;
        }
        if (!code.empty()) {
            auto m = lib.model(code);
            if (!m) {
                dump_model(lib.build(code)); // throws with the reason
                return 1;
            }
            dump_model(*m);
            return 0;
        }
        std::printf("%zu archives, %zu models, loaded in %lld ms\n", lib.archives().size(), lib.model_codes().size(),
                    static_cast<long long>(ms));
        if (!sweep) {
            for (const auto& a : lib.archives()) std::printf("  %s\n", a->archive->label().c_str());
            for (const auto& c : lib.model_codes()) std::printf("%s ", c.c_str());
            std::printf("\n");
            return 0;
        }
        std::set<std::string> good;
        std::size_t failed = 0;
        for (const auto& c : lib.model_codes()) {
            try {
                auto m = lib.build(c);
                good.insert(c);
                bool walk = m.clip("L01") || m.clip("L02");
                bool idle = m.clip("P01") || m.clip("O01");
                if (!walk || !idle) std::printf("  %s: no %s clip\n", c.c_str(), !walk ? "walk" : "idle");
            } catch (const std::exception& e) {
                ++failed;
                std::printf("  FAIL %s: %s\n", c.c_str(), e.what());
            }
        }
        std::set<int> races, races_all;
        for (int race = 0; race < 3000; ++race)
            for (std::uint8_t g = 0; g < 3; ++g)
                if (const auto* rm = game::find_race_model(static_cast<std::uint16_t>(race), g)) {
                    races_all.insert(race);
                    if (good.contains(std::string(rm->code))) races.insert(race);
                }
        std::printf("%zu models built, %zu failed; %zu of %zu race ids have a classic model\n", good.size(), failed,
                    races.size(), races_all.size());
        return failed ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "chrdump: %s\n", e.what());
        return 1;
    }
}
