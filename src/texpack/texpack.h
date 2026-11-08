#pragma once

// Texture packs built from the player's own archives (docs/graphics.md "Built packs", docs/texstudio.md): every
// distinct texture of the .s3d/.eqg files, keyed by texture_content_hash of its file, and the pack's DDS files.
// Shared by tools/texpack (the command line) and the texture studio (tools/texstudio). Nothing here writes into the
// archives' folders.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/texture.h"

namespace hailstone::texpack {

// none: every texel opaque; cutout: only 0 and 255; blend: anything between.
enum class AlphaKind : std::uint8_t { None, Cutout, Blend };
std::string_view alpha_name(AlphaKind kind);
std::optional<AlphaKind> alpha_from_name(std::string_view name);
AlphaKind alpha_kind(const assets::Image& rgba); // level 0 of an RGBA8 image

// One texture, by content.
struct TextureInfo {
    std::uint64_t hash = 0;
    std::uint32_t width = 0, height = 0;
    std::string stored; // assets::describe_texture
    bool bmp = false;   // a real BMP: its rows are turned for the WLD meshes
    AlphaKind alpha = AlphaKind::None;
    bool masked_variant = false; // an 8-bit BMP whose palette index 0 is used: <hash>.m
    bool keyed_variant = false;  // painted on a colour key (assets::remove_color_key): <hash>.k
    std::set<std::string> names, archives; // the file's names (lower case) and the archives holding it
    // Where it is read back from (read_sources): the first archive found holding it, and its name there.
    std::filesystem::path source_archive;
    std::string source_name;
};

// The 16 lower-case hex digits by-hash/ files are named by.
std::string hash_name(std::uint64_t hash);
std::optional<std::uint64_t> parse_hash_name(std::string_view text);

// The .s3d and .eqg files directly in each folder (any case), whose names start with one of `only` when it is not
// empty, sorted by path. A folder that is missing gives none.
std::vector<std::filesystem::path> list_archives(const std::vector<std::filesystem::path>& folders,
                                                 const std::vector<std::string>& only = {});

struct ScanOptions {
    unsigned threads = 0;                                      // 0: the hardware's
    std::function<bool()> cancelled;                           // polled between archives
    std::function<void(std::size_t done, std::size_t total)> progress; // archives done, from any thread
};
struct ScanResult {
    std::vector<TextureInfo> textures; // by hash
    std::size_t archives = 0;
    std::vector<std::string> errors; // "<archive>/<file>: why" for each that could not be read
    bool cancelled = false;
};
// Every texture of the archives, once per content (its alpha and variants found by decoding it).
ScanResult scan(const std::vector<std::filesystem::path>& archives, const ScanOptions& options = {});

// The images a pack file is made from, level 0 only, RGBA8, upright as in an image viewer.
struct Sources {
    assets::Image color;
    std::optional<assets::Image> masked; // palette index 0 transparent (masked_variant)
    std::optional<assets::Image> keyed;  // the colour key taken out (keyed_variant)
};
// Decodes a game texture file as scan() saw it.
std::optional<Sources> decode_sources(std::span<const std::byte> data, std::string* error = nullptr);
// Reads the texture back from its source archive. `archive_cache` keeps the last archive open across calls (a
// caller going through textures in source order opens each archive once); it may be null.
struct ArchiveCache;
std::optional<Sources> read_sources(const TextureInfo& info, ArchiveCache* archive_cache, std::string* error = nullptr);
struct ArchiveCacheDeleter { void operator()(ArchiveCache* c) const; };
std::unique_ptr<ArchiveCache, ArchiveCacheDeleter> make_archive_cache();

// The pack's files of one texture: <hash><suffix>.dds.
enum class FileKind : std::uint8_t { Color, Masked, Keyed, Normal, Rough };
std::string_view suffix(FileKind kind); // "", ".m", ".k", ".n", ".r"
std::optional<FileKind> kind_from_suffix(std::string_view suffix);

// Whether a texture is data rather than a picture (the game's own normal maps, EQG terrain coverage, palette and
// mask maps): the upscaler models would only damage it, so it is scaled with Lanczos and gets no maps.
bool is_data(const TextureInfo& info);
// Character skins and equipment wrap round a model, not across a tiled surface: their edges are reflected before a
// model, not wrapped.
bool reflects_edges(const TextureInfo& info);
// Textures of this side or less are only scaled with Lanczos and get no maps.
inline constexpr std::uint32_t kTinySide = 16;
// Whether the models run on it (not data, not tiny).
bool uses_models(const TextureInfo& info);

// The files a texture gets in a pack, with or without its normal and roughness maps.
std::vector<FileKind> pack_files(const TextureInfo& info, bool maps);

// A pack file: `rgba` (level 0, upright) with its full mip chain as a DX10 DDS: colour BC7 (mips by
// assets::build_color_mips, the transparent texels grown over only in the masked and keyed variants), normals BC5
// (averaged and renormalised), roughness BC4. Stored in the rows the WLD meshes read: a BMP source's are flipped, and
// its normal map's green inverted with them, so every pack normal map has green toward +v (PBRify's normals are
// DirectX's way, green down the picture).
std::vector<std::byte> encode_pack_file(assets::Image rgba, FileKind kind, bool bmp);
// Once per process, before the first encode_pack_file (thread-safe; later calls do nothing).
void init_encoders();

// The bytes a pack file takes per texel of level 0 (with its mips), for estimates.
double bytes_per_texel(FileKind kind);

// texpack export's manifest.jsonl line, and back (names and archives as lists; the source is not kept).
std::string manifest_line(const TextureInfo& info);
std::optional<TextureInfo> parse_manifest_line(std::string_view line);

// A scan kept between runs (the texture studio's cache): the archives' paths, sizes and times, then a line per
// texture. load_scan returns nullopt when the file is missing or its archives changed.
bool save_scan(const std::filesystem::path& file, const std::vector<std::filesystem::path>& archives,
               const std::vector<TextureInfo>& textures);
std::optional<std::vector<TextureInfo>> load_scan(const std::filesystem::path& file,
                                                  const std::vector<std::filesystem::path>& archives);

} // namespace hailstone::texpack
