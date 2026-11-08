// texdump ARCHIVE [NAME OUT.png]
// Lists every texture in a PFS archive (files named .bmp/.dds/.tga, plus any
// other file whose contents start with DDS or BMP magic) with its stored
// format, size and mip count, or the decode error. With NAME, decodes that
// file to RGBA8 and writes level 0 as a PNG.

#include <cstdio>
#include <exception>
#include <string>

#include "assets/pfs.h"
#include "assets/texture.h"
#include "core/png_write.h"

using namespace hailstone::assets;

namespace {

const char* format_name(PixelFormat f) {
    switch (f) {
    case PixelFormat::RGBA8: return "RGBA8";
    case PixelFormat::BC1: return "BC1";
    case PixelFormat::BC2: return "BC2";
    case PixelFormat::BC3: return "BC3";
    case PixelFormat::BC4: return "BC4";
    case PixelFormat::BC5: return "BC5";
    case PixelFormat::BC7: return "BC7";
    }
    return "?";
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "usage: texdump ARCHIVE [NAME OUT.png]\n");
        return 2;
    }
    try {
        auto archive = PfsArchive::open(argv[1]);
        if (argc == 2) {
            int failures = 0;
            for (const auto& name : archive.names()) {
                auto data = archive.read(name);
                if (!data) continue;
                if (!has_texture_extension(name) && !has_texture_magic(*data)) continue;
                std::string error;
                auto image = decode_texture(*data, {}, &error);
                std::string stored = describe_texture(*data);
                if (image) {
                    std::printf("ok\t%s\t%s\t%ux%u\tmips=%zu\t%s\n", name.c_str(),
                                format_name(image->format), image->width, image->height, image->mips.size(),
                                stored.c_str());
                } else {
                    ++failures;
                    std::printf("FAIL\t%s\t%s\t%s\n", name.c_str(), error.c_str(), stored.c_str());
                }
            }
            return failures ? 1 : 0;
        }
        auto data = archive.read(argv[2]);
        if (!data) {
            std::fprintf(stderr, "%s: no file %s\n", argv[1], argv[2]);
            return 1;
        }
        std::string error;
        auto image = decode_texture(*data, {}, &error);
        if (!image) {
            std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
            return 1;
        }
        auto rgba = to_rgba8(*image);
        if (rgba.mips.empty() || !hailstone::write_png_rgba(argv[3], rgba.mips[0], rgba.width, rgba.height)) {
            std::fprintf(stderr, "texdump: cannot write %s\n", argv[3]);
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "texdump: %s\n", e.what());
        return 1;
    }
}
