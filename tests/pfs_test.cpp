#include <doctest/doctest.h>

#include "assets/pfs.h"
#include "core/byte_io.h"
#include "fuzz_support.h"
#include "game_files.h"
#include "pfs_fixtures.h"

using nmsc::assets::PfsArchive;

TEST_CASE("PfsArchive rejects data that is not PFS") {
    std::vector<std::byte> junk(64, std::byte{0x11});
    CHECK_THROWS(PfsArchive::from_memory(junk));
}

TEST_CASE("PfsArchive reads a classic zone archive" * doctest::skip(!nmsc::test::game_dir())) {
    auto archive = PfsArchive::open(nmsc::test::game_dir()->find("qeynos2.s3d").value());
    CHECK(archive.contains("qeynos2.wld"));
    CHECK(archive.contains("QEYNOS2.WLD"));
    CHECK(archive.contains("objects.wld"));
    CHECK(archive.contains("lights.wld"));
    auto wld = archive.read("qeynos2.wld");
    REQUIRE(wld);
    // WLD magic 0x54503d02
    CHECK(std::to_integer<int>((*wld)[0]) == 0x02);
    CHECK(std::to_integer<int>((*wld)[1]) == 0x3d);
}

TEST_CASE("pfs_name_crc names every file of a classic archive" * doctest::skip(!nmsc::test::game_dir())) {
    // qeynos2.s3d has 152 directory entries: 151 files plus the name table.
    auto archive = PfsArchive::open(nmsc::test::game_dir()->find("qeynos2.s3d").value());
    CHECK(archive.names().size() == 151);
}

TEST_CASE("PfsArchive handles archives with stale names and a 0xffffffff name table" *
          doctest::skip(!nmsc::test::game_dir())) {
    auto gd = nmsc::test::game_dir();
    auto velketor = PfsArchive::open(gd->find("velketor_chr.s3d").value());
    CHECK_FALSE(velketor.contains("kael_chr.wld"));
    CHECK(velketor.contains("velketor_chr.wld"));
    auto gequip = PfsArchive::open(gd->find("gequip_nms.s3d").value());
    CHECK(gequip.contains("bat04.dds"));
}

namespace {
// The archive's directory entry for its first file: {crc, offset, size} after the directory's count.
std::size_t first_entry_at(const std::vector<std::byte>& archive) {
    nmsc::ByteReader r(archive);
    return r.le<std::uint32_t>() + 4;
}
void put_u32(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(v >> (8 * i));
}
} // namespace

TEST_CASE("PfsArchive refuses a file size its compressed blocks cannot back, before allocating it") {
    using namespace nmsc::test::pfs_fixtures;
    std::vector<std::byte> text(100, std::byte{'a'});
    auto archive = make_pfs({{"a.txt", text}});
    REQUIRE(PfsArchive::from_memory(archive).read("a.txt").value() == text);

    // A 2 GB size on a 100-byte file: the guard throws HugeAllocation for any request above 64 MB, so a throw of
    // anything else shows the size was refused before the buffer was made.
    auto huge = archive;
    put_u32(huge, first_entry_at(huge) + 8, 0x7fffffffu);
    auto a = PfsArchive::from_memory(huge);
    nmsc::fuzz::arm_allocation_guard(true);
    bool refused = false, allocated = false;
    try {
        (void)a.read("a.txt");
    } catch (const nmsc::fuzz::HugeAllocation&) {
        allocated = true;
    } catch (const std::exception&) {
        refused = true;
    }
    nmsc::fuzz::arm_allocation_guard(false);
    CHECK(refused);
    CHECK_FALSE(allocated);

    // A block claiming more than 1032 bytes per compressed byte.
    nmsc::ByteReader r(archive);
    r.seek(first_entry_at(archive) + 4);
    const std::uint32_t data_at = r.le<std::uint32_t>();
    auto bomb = archive;
    put_u32(bomb, first_entry_at(bomb) + 8, 0x10000000u);
    put_u32(bomb, data_at + 4, 0x10000000u);
    CHECK_THROWS_WITH(PfsArchive::from_memory(bomb).read("a.txt"),
                      doctest::Contains("larger than its compressed bytes"));
}
