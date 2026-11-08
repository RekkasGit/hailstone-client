#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "render/spirv_file.h"
#include "scratch_dir.h"

using hailstone::render::read_spirv_file;

namespace {
void write(const std::filesystem::path& p, std::size_t size) {
    std::ofstream f(p, std::ios::binary);
    f << std::string(size, '\x07');
}
} // namespace

TEST_CASE("read_spirv_file: whole words only; a truncated file is refused, not read past its buffer") {
    hailstone::test::ScratchDir dir("spirv");
    write(dir.path() / "good.spv", 8);
    auto words = read_spirv_file(dir.path() / "good.spv");
    CHECK(words == std::vector<std::uint32_t>{0x07070707u, 0x07070707u});
    // 1001 bytes used to be read into a 1000-byte buffer.
    write(dir.path() / "cut.spv", 1001);
    CHECK_THROWS_WITH(read_spirv_file(dir.path() / "cut.spv"), doctest::Contains("bad SPIR-V size"));
    write(dir.path() / "empty.spv", 0);
    CHECK_THROWS_WITH(read_spirv_file(dir.path() / "empty.spv"), doctest::Contains("bad SPIR-V size"));
    CHECK_THROWS_WITH(read_spirv_file(dir.path() / "missing.spv"), doctest::Contains("cannot open shader"));
}
