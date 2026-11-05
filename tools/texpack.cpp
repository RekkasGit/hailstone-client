// texpack: builds a texture pack (docs/graphics.md, "Texture packs") from the game's archives.
//
//   texpack export ASSETDIR WORKDIR [--only PREFIX,...]
//     Every texture in the .s3d/.eqg archives (those whose names start with a PREFIX), once per content:
//     WORKDIR/src/<hash>.png upright as in an image viewer, <hash>.m.png the masked variant of an 8-bit BMP (palette
//     index 0 transparent) when it has such texels, <hash>.k.png the variant without its colour key of a texture
//     painted on one (assets::remove_color_key), and WORKDIR/manifest.jsonl, one line per texture.
//   texpack build WORKDIR PACKDIR [--up DIR]
//     WORKDIR/up/ (or DIR)<hash>[.m|.k|.n|.r].png (tools/texpack/upscale.py's output) encoded with their mips into
//     PACKDIR: colour BC7, normals BC5, roughness BC4, as DX10 DDS files PACKDIR/by-hash/<hash>[.m|.k|.n|.r].dds: keyed by the
//     game file's content, never its name (w1 is a different image in each city), so a server's changed file keeps
//     its own look.
//
// <hash> is texture_content_hash of the game's file (assets/texture_overrides.h).

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <bc7enc.h>
#include <rgbcx.h>

#include "assets/pfs.h"
#include "assets/texture.h"
#include "assets/texture_overrides.h"
#include "core/byte_io.h"
#include "core/game_dir.h"
#include "core/png_write.h"

using namespace nmsc::assets;
namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex16(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

bool has_texture_extension(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    return ext == "bmp" || ext == "dds" || ext == "tga";
}

bool has_texture_magic(const std::vector<std::byte>& d) {
    auto at = [&](std::size_t i) { return std::to_integer<char>(d[i]); };
    return (d.size() >= 4 && at(0) == 'D' && at(1) == 'D' && at(2) == 'S' && at(3) == ' ') ||
           (d.size() >= 2 && at(0) == 'B' && at(1) == 'M');
}

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out + "\"";
}

template <typename Set> std::string json_list(const Set& items) {
    std::string out = "[";
    for (const auto& s : items) out += (out.size() > 1 ? "," : "") + json_string(s);
    return out + "]";
}

// One texture, by content.
struct Entry {
    std::uint32_t width = 0, height = 0;
    std::string stored; // describe_texture
    bool bmp = false;   // a real BMP: its rows are turned for the WLD meshes
    std::string alpha;  // none, cutout, blend
    bool masked_variant = false;
    bool keyed_variant = false;
    std::set<std::string> names, archives;
};

// none: every texel opaque; cutout: only 0 and 255; blend: anything between.
std::string alpha_kind(const Image& rgba) {
    bool clear = false, partial = false;
    const auto& px = rgba.mips[0];
    for (std::size_t i = 3; i < px.size(); i += 4) {
        auto a = std::to_integer<std::uint8_t>(px[i]);
        if (a == 0) clear = true;
        else if (a != 255) partial = true;
    }
    return partial ? "blend" : clear ? "cutout" : "none";
}

int run_export(const fs::path& assets, const fs::path& work, const std::vector<std::string>& only) {
    std::vector<fs::path> archives;
    for (const auto& e : fs::directory_iterator(assets)) {
        if (!e.is_regular_file()) continue;
        std::string name = lower(e.path().filename().string());
        if (!name.ends_with(".s3d") && !name.ends_with(".eqg")) continue;
        if (!only.empty() && std::none_of(only.begin(), only.end(), [&](const std::string& p) { return name.starts_with(p); }))
            continue;
        archives.push_back(e.path());
    }
    std::sort(archives.begin(), archives.end());
    fs::create_directories(work / "src");

    std::mutex mutex;
    std::map<std::uint64_t, Entry> entries;
    std::atomic<std::size_t> next{0}, failures{0};
    auto worker = [&] {
        for (std::size_t i; (i = next++) < archives.size();) {
            const fs::path& path = archives[i];
            const std::string label = lower(path.filename().string());
            try {
                auto archive = PfsArchive::open(path);
                for (const auto& name : archive.names()) {
                    auto data = archive.read(name);
                    if (!data || (!has_texture_extension(name) && !has_texture_magic(*data))) continue;
                    const std::uint64_t hash = texture_content_hash(*data);
                    {
                        std::lock_guard lock(mutex);
                        auto [it, fresh] = entries.try_emplace(hash);
                        it->second.names.insert(name);
                        it->second.archives.insert(label);
                        if (!fresh) continue;
                    }
                    std::string error;
                    auto image = decode_texture(*data, {}, &error);
                    if (!image) {
                        ++failures;
                        std::fprintf(stderr, "%s/%s: %s\n", label.c_str(), name.c_str(), error.c_str());
                        std::lock_guard lock(mutex);
                        entries.erase(hash);
                        continue;
                    }
                    Image rgba = to_rgba8(*image);
                    rgba.mips.resize(1);
                    Entry e;
                    e.width = rgba.width;
                    e.height = rgba.height;
                    e.stored = describe_texture(*data);
                    e.bmp = std::to_integer<char>((*data)[0]) == 'B';
                    e.alpha = alpha_kind(rgba);
                    const std::string base = hex16(hash);
                    nmsc::write_png_rgba(work / "src" / (base + ".png"), rgba.mips[0], rgba.width, rgba.height);
                    if (e.stored.starts_with("BMP 8-bit")) {
                        if (auto keyed = decode_texture(*data, {.palette_index0_transparent = true})) {
                            Image m = to_rgba8(*keyed);
                            if (alpha_kind(m) != "none") {
                                e.masked_variant = true;
                                nmsc::write_png_rgba(work / "src" / (base + ".m.png"), m.mips[0], m.width, m.height);
                            }
                        }
                    }
                    // Painted on a colour key: the variant for materials that read the alpha, the key taken out
                    // before the upscaler can smear it into the edges (the plain file keeps the colour under alpha 0
                    // for opaque materials).
                    if (!e.stored.starts_with("BMP 8-bit") && e.alpha != "none") {
                        Image k = rgba;
                        if (remove_color_key(k)) {
                            e.keyed_variant = true;
                            nmsc::write_png_rgba(work / "src" / (base + ".k.png"), k.mips[0], k.width, k.height);
                        }
                    }
                    std::lock_guard lock(mutex);
                    Entry& slot = entries[hash];
                    e.names = std::move(slot.names);
                    e.archives = std::move(slot.archives);
                    slot = std::move(e);
                }
            } catch (const std::exception& ex) {
                ++failures;
                std::fprintf(stderr, "%s: %s\n", label.c_str(), ex.what());
            }
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    FILE* out = std::fopen((work / "manifest.jsonl").string().c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "texpack: cannot write %s\n", (work / "manifest.jsonl").string().c_str());
        return 1;
    }
    for (const auto& [hash, e] : entries)
        std::fprintf(out,
                     "{\"hash\":\"%s\",\"w\":%u,\"h\":%u,\"stored\":%s,\"bmp\":%s,\"alpha\":\"%s\",\"masked_variant\":%s,"
                     "\"keyed_variant\":%s,\"names\":%s,\"archives\":%s}\n",
                     hex16(hash).c_str(), e.width, e.height, json_string(e.stored).c_str(), e.bmp ? "true" : "false",
                     e.alpha.c_str(), e.masked_variant ? "true" : "false", e.keyed_variant ? "true" : "false", json_list(e.names).c_str(),
                     json_list(e.archives).c_str());
    std::fclose(out);
    std::printf("texpack: %zu archives, %zu textures, %zu failures\n", archives.size(), entries.size(), failures.load());
    return failures ? 1 : 0;
}

// ---- build ----------------------------------------------------------------

constexpr std::uint32_t kDxgiBc4 = 80, kDxgiBc5 = 83, kDxgiBc7 = 98;

std::uint32_t level_dim(std::uint32_t d, std::size_t level) { return std::max<std::uint32_t>(1, d >> level); }

// A DX10 DDS of the given block-compressed levels.
std::vector<std::byte> dds_file(std::uint32_t w, std::uint32_t h, std::uint32_t dxgi,
                                const std::vector<std::vector<std::byte>>& levels) {
    nmsc::ByteWriter out;
    for (char c : std::string_view("DDS ")) out.le(static_cast<std::uint8_t>(c));
    out.le<std::uint32_t>(124);
    out.le<std::uint32_t>(0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000); // caps, height, width, pixel format, mips, linear size
    out.le<std::uint32_t>(h);
    out.le<std::uint32_t>(w);
    out.le<std::uint32_t>(static_cast<std::uint32_t>(levels[0].size()));
    out.le<std::uint32_t>(0);
    out.le<std::uint32_t>(static_cast<std::uint32_t>(levels.size()));
    out.zeros(44);
    out.le<std::uint32_t>(32);
    out.le<std::uint32_t>(0x4); // FourCC
    for (char c : std::string_view("DX10")) out.le(static_cast<std::uint8_t>(c));
    out.zeros(20);
    out.le<std::uint32_t>(0x1000 | 0x400000 | 0x8); // texture, mipmap, complex
    out.zeros(16);
    out.le<std::uint32_t>(dxgi);
    out.le<std::uint32_t>(3); // TEXTURE2D
    out.le<std::uint32_t>(0);
    out.le<std::uint32_t>(1);
    out.le<std::uint32_t>(0);
    for (const auto& level : levels)
        for (std::byte b : level) out.le(std::to_integer<std::uint8_t>(b));
    return out.take();
}

enum class Codec { Bc7, Bc5, Bc4 };

// One RGBA8 level in 4x4 blocks, edge texels repeated into partial blocks.
std::vector<std::byte> encode_level(std::span<const std::byte> rgba, std::uint32_t w, std::uint32_t h, Codec codec) {
    const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    const std::size_t block_bytes = codec == Codec::Bc4 ? 8 : 16;
    std::vector<std::byte> out(std::size_t(bw) * bh * block_bytes);
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    params.m_max_partitions = 16;
    std::uint8_t px[64];
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            for (std::uint32_t t = 0; t < 16; ++t) {
                std::uint32_t x = std::min(bx * 4 + t % 4, w - 1), y = std::min(by * 4 + t / 4, h - 1);
                for (int c = 0; c < 4; ++c) px[t * 4 + c] = std::to_integer<std::uint8_t>(rgba[(std::size_t(y) * w + x) * 4 + c]);
            }
            void* dst = out.data() + (std::size_t(by) * bw + bx) * block_bytes;
            switch (codec) {
            case Codec::Bc7: bc7enc_compress_block(dst, px, &params); break;
            case Codec::Bc5: rgbcx::encode_bc5_hq(dst, px); break;
            case Codec::Bc4: rgbcx::encode_bc4_hq(dst, px); break;
            }
        }
    return out;
}

// Box-filtered levels below level 0 (`normal`: as unit vectors in rgb, renormalised), down to 1x1.
void box_mips(Image& img, bool normal) {
    while (img.mips.size() < static_cast<std::size_t>(std::bit_width(std::max(img.width, img.height)))) {
        const std::size_t l = img.mips.size() - 1;
        const std::uint32_t sw = level_dim(img.width, l), sh = level_dim(img.height, l);
        const std::uint32_t dw = level_dim(img.width, l + 1), dh = level_dim(img.height, l + 1);
        const auto& src = img.mips[l];
        std::vector<std::byte> dst(std::size_t(dw) * dh * 4);
        for (std::uint32_t y = 0; y < dh; ++y)
            for (std::uint32_t x = 0; x < dw; ++x) {
                float sum[4] = {}, n = 0.0f;
                for (std::uint32_t yy = y * 2; yy < std::min(y * 2 + 2, sh); ++yy)
                    for (std::uint32_t xx = x * 2; xx < std::min(x * 2 + 2, sw); ++xx, n += 1.0f)
                        for (int c = 0; c < 4; ++c) {
                            float v = std::to_integer<std::uint8_t>(src[(std::size_t(yy) * sw + xx) * 4 + c]) / 255.0f;
                            sum[c] += normal && c < 3 ? v * 2.0f - 1.0f : v;
                        }
                if (normal) {
                    float len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                    for (int c = 0; c < 3; ++c) sum[c] = len > 0 ? (sum[c] / len * 0.5f + 0.5f) * n : 0.5f * n;
                }
                for (int c = 0; c < 4; ++c)
                    dst[(std::size_t(y) * dw + x) * 4 + c] = std::byte(static_cast<std::uint8_t>(std::lround(std::clamp(sum[c] / n, 0.0f, 1.0f) * 255.0f)));
            }
        img.mips.push_back(std::move(dst));
    }
}

void flip_rows(Image& img) {
    const std::size_t row = std::size_t(img.width) * 4;
    auto& level = img.mips[0];
    for (std::uint32_t y = 0; y < img.height / 2; ++y)
        std::swap_ranges(level.begin() + static_cast<std::ptrdiff_t>(y * row), level.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         level.begin() + static_cast<std::ptrdiff_t>((img.height - 1 - y) * row));
}

// A field of one manifest line: "key":value, the value up to the next comma (enough for hash and bmp).
std::string field(const std::string& line, const std::string& key) {
    auto at = line.find("\"" + key + "\":");
    if (at == std::string::npos) return {};
    at += key.size() + 3;
    std::size_t end = line.find_first_of(",}", at);
    std::string v = line.substr(at, end - at);
    if (!v.empty() && v.front() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

int run_build(const fs::path& work, const fs::path& pack, const std::string& up_name) {
    struct Job {
        std::string hash;
        bool bmp = false;
    };
    std::vector<Job> jobs;
    std::ifstream manifest(work / "manifest.jsonl");
    for (std::string line; std::getline(manifest, line);)
        if (auto h = field(line, "hash"); !h.empty()) jobs.push_back({h, field(line, "bmp") == "true"});
    const fs::path up = work / up_name, out_dir = pack / "by-hash";
    fs::create_directories(out_dir);
    rgbcx::init();
    bc7enc_compress_block_init();

    std::atomic<std::size_t> next{0}, written{0}, failures{0};
    auto worker = [&] {
        for (std::size_t i; (i = next++) < jobs.size();) {
            const Job& job = jobs[i];
            for (const char* suffix : {"", ".m", ".k", ".n", ".r"}) {
                const fs::path src = up / (job.hash + suffix + ".png"), dst = out_dir / (job.hash + suffix + ".dds");
                std::error_code ec;
                if (!fs::exists(src, ec)) continue;
                if (fs::exists(dst, ec) && fs::last_write_time(dst, ec) >= fs::last_write_time(src, ec)) continue;
                auto bytes = nmsc::read_file(src);
                std::string why;
                auto decoded = bytes ? decode_texture(*bytes, {}, &why) : std::nullopt;
                if (!decoded) {
                    ++failures;
                    std::fprintf(stderr, "%s: %s\n", src.string().c_str(), bytes ? why.c_str() : "cannot be read");
                    continue;
                }
                Image img = to_rgba8(*decoded);
                img.mips.resize(1);
                const std::string_view kind = suffix;
                // Stored as the WLD meshes read the game's file: a BMP's rows bottom first. A normal map's green
                // (PBRify's, DirectX's way: down the picture, toward +v in a DDS's top-first rows) then points the
                // other way along v: turned back so every pack normal map agrees (green toward +v).
                if (job.bmp) {
                    flip_rows(img);
                    if (kind == ".n")
                        for (std::size_t p = 1; p < img.mips[0].size(); p += 4) img.mips[0][p] = std::byte(255 - std::to_integer<std::uint8_t>(img.mips[0][p]));
                }
                Codec codec = Codec::Bc7;
                std::uint32_t dxgi = kDxgiBc7;
                if (kind == ".n") {
                    box_mips(img, true);
                    codec = Codec::Bc5;
                    dxgi = kDxgiBc5;
                } else if (kind == ".r") {
                    box_mips(img, false);
                    codec = Codec::Bc4;
                    dxgi = kDxgiBc4;
                } else {
                    // Only the masked and keyed variants' transparent texels are a colour key; a DDS's alpha 0 keeps
                    // the colour an opaque material draws (generic_plate_blue_c.dds, the hero's forge plate).
                    build_color_mips(img, 0.5f, kind == ".m" || kind == ".k");
                }
                std::vector<std::vector<std::byte>> levels;
                for (std::size_t l = 0; l < img.mips.size(); ++l)
                    levels.push_back(encode_level(img.mips[l], level_dim(img.width, l), level_dim(img.height, l), codec));
                auto file = dds_file(img.width, img.height, dxgi, levels);
                const fs::path tmp = dst.string() + ".tmp";
                std::ofstream(tmp, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
                fs::rename(tmp, dst, ec);
                ++written;
            }
            if ((i + 1) % 1000 == 0) std::printf("%zu/%zu textures\n", i + 1, jobs.size()), std::fflush(stdout);
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    std::printf("texpack: %zu textures, %zu files written, %zu failures\n", jobs.size(), written.load(), failures.load());
    return failures ? 1 : 0;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t p = 0; p <= s.size();) {
        std::size_t q = std::min(s.find(',', p), s.size());
        if (q > p) out.push_back(lower(s.substr(p, q - p)));
        p = q + 1;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        if (args.size() >= 3 && args[0] == "export") {
            std::vector<std::string> only;
            if (args.size() == 5 && args[3] == "--only") only = split(args[4]);
            else if (args.size() != 3) goto usage;
            return run_export(args[1], args[2], only);
        }
        if (args.size() >= 3 && args[0] == "build") {
            std::string up = "up";
            if (args.size() == 5 && args[3] == "--up") up = args[4];
            else if (args.size() != 3) goto usage;
            return run_build(args[1], args[2], up);
        }
    usage:
        std::fprintf(stderr, "usage: texpack export ASSETDIR WORKDIR [--only PREFIX,...]\n"
                             "       texpack build WORKDIR PACKDIR [--up DIR]\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "texpack: %s\n", e.what());
        return 1;
    }
}
