// pfsdump ARCHIVE [FILE [OUT]]
// Lists the files in a PFS archive (.s3d/.eqg), or extracts one.

#include <cstdio>
#include <exception>
#include <fstream>

#include "assets/pfs.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pfsdump ARCHIVE [FILE [OUT]]\n");
        return 2;
    }
    try {
        auto archive = hailstone::assets::PfsArchive::open(argv[1]);
        if (argc == 2) {
            for (const auto& name : archive.names()) {
                auto data = archive.read(name);
                std::printf("%10zu  %s\n", data ? data->size() : 0, name.c_str());
            }
            return 0;
        }
        auto data = archive.read(argv[2]);
        if (!data) {
            std::fprintf(stderr, "%s: no file %s\n", argv[1], argv[2]);
            return 1;
        }
        std::ofstream out(argc > 3 ? argv[3] : argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
        return out ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "pfsdump: %s\n", e.what());
        return 1;
    }
}
