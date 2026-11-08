#include <doctest/doctest.h>

#include "assets/pfs.h"
#include "core/byte_io.h"
#include "fuzz_support.h"
#include "game_files.h"
#include "pfs_fixtures.h"

using hailstone::assets::PfsArchive;

TEST_CASE("PfsArchive rejects data that is not PFS") {
    std::vector<std::byte> junk(64, std::byte{0x11});
    CHECK_THROWS(PfsArchive::from_memory(junk));
}

TEST_CASE("PfsArchive reads a classic zone archive" * doctest::skip(!hailstone::test::game_dir())) {
    auto archive = PfsArchive::open(hailstone::test::game_dir()->find("qeynos2.s3d").value());
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

TEST_CASE("pfs_name_crc names every file of a classic archive" * doctest::skip(!hailstone::test::game_dir())) {
    // qeynos2.s3d has 152 directory entries: 151 files plus the name table.
    auto archive = PfsArchive::open(hailstone::test::game_dir()->find("qeynos2.s3d").value());
    CHECK(archive.names().size() == 151);
}

TEST_CASE("PfsArchive handles archives with stale names and a 0xffffffff name table" *
          doctest::skip(!hailstone::test::game_dir())) {
    auto gd = hailstone::test::game_dir();
    auto velketor = PfsArchive::open(gd->find("velketor_chr.s3d").value());
    CHECK_FALSE(velketor.contains("kael_chr.wld"));
    CHECK(velketor.contains("velketor_chr.wld"));
    auto gequip = PfsArchive::open(gd->find("gequip_nms.s3d").value());
    CHECK(gequip.contains("bat04.dds"));
}

namespace {
// The archive's directory entry for its first file: {crc, offset, size} after the directory's count.
std::size_t first_entry_at(const std::vector<std::byte>& archive) {
    hailstone::ByteReader r(archive);
    return r.le<std::uint32_t>() + 4;
}
void put_u32(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(v >> (8 * i));
}
} // namespace

TEST_CASE("PfsArchive refuses a file size its compressed blocks cannot back, before allocating it") {
    using namespace hailstone::test::pfs_fixtures;
    std::vector<std::byte> text(100, std::byte{'a'});
    auto archive = make_pfs({{"a.txt", text}});
    REQUIRE(PfsArchive::from_memory(archive).read("a.txt").value() == text);

    // A 2 GB size on a 100-byte file: the guard throws HugeAllocation for any request above 64 MB, so a throw of
    // anything else shows the size was refused before the buffer was made.
    auto huge = archive;
    put_u32(huge, first_entry_at(huge) + 8, 0x7fffffffu);
    auto a = PfsArchive::from_memory(huge);
    hailstone::fuzz::arm_allocation_guard(true);
    bool refused = false, allocated = false;
    try {
        (void)a.read("a.txt");
    } catch (const hailstone::fuzz::HugeAllocation&) {
        allocated = true;
    } catch (const std::exception&) {
        refused = true;
    }
    hailstone::fuzz::arm_allocation_guard(false);
    CHECK(refused);
    CHECK_FALSE(allocated);

    // A block claiming more than 1032 bytes per compressed byte.
    hailstone::ByteReader r(archive);
    r.seek(first_entry_at(archive) + 4);
    const std::uint32_t data_at = r.le<std::uint32_t>();
    auto bomb = archive;
    put_u32(bomb, first_entry_at(bomb) + 8, 0x10000000u);
    put_u32(bomb, data_at + 4, 0x10000000u);
    CHECK_THROWS_WITH(PfsArchive::from_memory(bomb).read("a.txt"),
                      doctest::Contains("larger than its compressed bytes"));
}

TEST_CASE("PfsArchive: memory follows the bytes zlib writes, not the size a block declares") {
    using namespace hailstone::test::pfs_fixtures;
    // Within the 1032:1 ratio, 100 KB of junk deflate may declare 103 MB; it used to be zero-filled before zlib
    // failed on the first byte. The guard throws HugeAllocation for any request above 64 MB.
    constexpr std::uint32_t kJunk = 100000;
    constexpr std::uint32_t kDeclared = kJunk * 1032;
    hailstone::ByteWriter body;
    body.le<std::uint32_t>(kJunk);
    body.le<std::uint32_t>(kDeclared);
    body.bytes(std::vector<std::byte>(kJunk, std::byte{0x11}));
    hailstone::ByteWriter names;
    names.le<std::uint32_t>(1);
    names.le<std::uint32_t>(6);
    names.fixed_string("a.txt", 6);
    auto table = deflate_blocks(names.data(), 3000);
    hailstone::ByteWriter w;
    w.le<std::uint32_t>(static_cast<std::uint32_t>(12 + body.size() + table.size()));
    w.le<std::uint32_t>(0x20534650);
    w.le<std::uint32_t>(0x20000);
    w.bytes(body.data());
    w.bytes(table);
    w.le<std::uint32_t>(2);
    w.le<std::uint32_t>(hailstone::assets::pfs_name_crc("a.txt"));
    w.le<std::uint32_t>(12);
    w.le<std::uint32_t>(kDeclared);
    w.le<std::uint32_t>(0x61580ac9u);
    w.le<std::uint32_t>(static_cast<std::uint32_t>(12 + body.size()));
    w.le<std::uint32_t>(static_cast<std::uint32_t>(names.size()));
    auto a = PfsArchive::from_memory(w.take());
    REQUIRE(a.contains("a.txt"));
    hailstone::fuzz::arm_allocation_guard(true);
    bool refused = false, allocated = false;
    try {
        (void)a.read("a.txt");
    } catch (const hailstone::fuzz::HugeAllocation&) {
        allocated = true;
    } catch (const std::exception&) {
        refused = true;
    }
    hailstone::fuzz::arm_allocation_guard(false);
    CHECK(refused);
    CHECK_FALSE(allocated);

    // Real data still reads whole: a 1 MB block (the growing buffer's several steps) and 8 KB blocks.
    std::vector<std::byte> text(1 << 20);
    for (std::size_t i = 0; i < text.size(); ++i) text[i] = static_cast<std::byte>((i * 7) ^ (i >> 9));
    CHECK(PfsArchive::from_memory(make_pfs({{"b.bin", text}}, 1 << 20)).read("b.bin").value() == text);
    CHECK(PfsArchive::from_memory(make_pfs({{"b.bin", text}}, 8192)).read("b.bin").value() == text);
    CHECK(PfsArchive::from_memory(make_pfs({{"e.bin", {}}})).read("e.bin").value().empty());
}

TEST_CASE("PfsArchive: a name listed many times is one member") {
    using namespace hailstone::test::pfs_fixtures;
    std::vector<std::byte> text(10, std::byte{'w'});
    auto archive = make_pfs({{"zone.wld", text}, {"other.txt", text}});
    // Rebuild the name table so it lists zone.wld 1000 times (and other.txt once).
    hailstone::ByteWriter names;
    names.le<std::uint32_t>(1001);
    for (int i = 0; i < 1000; ++i) {
        names.le<std::uint32_t>(9);
        names.fixed_string("ZONE.WLD", 9);
    }
    names.le<std::uint32_t>(10);
    names.fixed_string("other.txt", 10);
    auto table = deflate_blocks(names.data(), 1 << 20);
    const std::size_t dir = first_entry_at(archive) - 4;
    hailstone::ByteWriter w;
    w.bytes({archive.data(), dir});
    const auto table_at = static_cast<std::uint32_t>(w.size());
    w.bytes(table);
    const auto new_dir = static_cast<std::uint32_t>(w.size());
    w.bytes({archive.data() + dir, archive.size() - dir});
    auto out = w.take();
    put_u32(out, 0, new_dir);
    // The name table is the directory's last entry.
    put_u32(out, new_dir + 4 + 2 * 12 + 4, table_at);
    put_u32(out, new_dir + 4 + 2 * 12 + 8, static_cast<std::uint32_t>(names.size()));
    auto a = PfsArchive::from_memory(out);
    CHECK(a.names() == std::vector<std::string>{"zone.wld", "other.txt"});
    CHECK(a.read("zone.wld").value() == text);
}

TEST_CASE("PfsArchive refuses a member or name table larger than any game file") {
    using namespace hailstone::test::pfs_fixtures;
    std::vector<std::byte> text(100, std::byte{'a'});
    auto archive = make_pfs({{"a.txt", text}});
    // Zeros deflate within the ratio check, so the declared size alone has to be refused.
    auto big = archive;
    put_u32(big, first_entry_at(big) + 8, 0x20000000u);
    CHECK_THROWS_WITH(PfsArchive::from_memory(big).read("a.txt"), doctest::Contains("larger than any game file"));
    auto table = archive;
    put_u32(table, first_entry_at(table) + 12 + 8, 0x02000000u);
    CHECK_THROWS_WITH(PfsArchive::from_memory(table), doctest::Contains("name table is too large"));
}
