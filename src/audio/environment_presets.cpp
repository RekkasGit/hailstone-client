#include "audio/environment_presets.h"

#include <algorithm>
#include <atomic>
#include <fstream>

#include "core/byte_io.h"
#include "core/game_dir.h"
#include "core/log.h"

namespace hailstone::audio {

namespace {

// The PE headers read: the DOS header's e_lfanew is at 0x3c, and the NT headers and section table follow within the
// first pages in any linker's output.
constexpr std::size_t kHeaderBytes = 0x1000;

bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

// The file offset of `size` bytes at virtual address `va`, from the headers.
bool map_va(std::span<const std::byte> headers, std::uint32_t va, std::size_t size, std::uint64_t& offset,
            std::string* error) {
    try {
        ByteReader r(headers);
        if (r.le<std::uint16_t>() != 0x5a4d) return fail(error, "not an executable (no MZ header)");
        r.seek(0x3c);
        const std::uint32_t pe = r.le<std::uint32_t>();
        r.seek(pe);
        if (r.le<std::uint32_t>() != 0x00004550) return fail(error, "not a PE executable");
        if (r.le<std::uint16_t>() != 0x14c) return fail(error, "not a 32-bit x86 executable");
        const std::uint16_t sections = r.le<std::uint16_t>();
        r.skip(12);
        const std::uint16_t optional_size = r.le<std::uint16_t>();
        r.skip(2);
        const std::size_t optional_at = r.position();
        if (r.le<std::uint16_t>() != 0x10b) return fail(error, "not a PE32 optional header");
        r.seek(optional_at + 28);
        const std::uint32_t image_base = r.le<std::uint32_t>();
        if (va < image_base) return fail(error, "the table's address is below the image");
        const std::uint32_t rva = va - image_base;
        r.seek(optional_at + optional_size);
        for (std::uint16_t i = 0; i < sections; ++i) {
            r.skip(8); // name
            const std::uint32_t virtual_size = r.le<std::uint32_t>();
            const std::uint32_t virtual_address = r.le<std::uint32_t>();
            const std::uint32_t raw_size = r.le<std::uint32_t>();
            const std::uint32_t raw_at = r.le<std::uint32_t>();
            r.skip(16);
            if (rva < virtual_address) continue;
            const std::uint64_t into = rva - virtual_address;
            if (into >= std::max(virtual_size, raw_size)) continue;
            // In this section: it must be in the bytes the file holds (not zero-filled memory past them).
            if (into + size > raw_size) return fail(error, "the table is not in the file's data");
            offset = std::uint64_t{raw_at} + into;
            return true;
        }
        return fail(error, "no section holds the table");
    } catch (const std::out_of_range&) {
        return fail(error, "truncated PE headers");
    }
}

bool valid_name(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

} // namespace

std::optional<std::vector<ZoneRoomPreset>> read_zone_room_presets(const ExeReader& read, std::string* error) {
    auto headers = read(0, kHeaderBytes);
    if (!headers) {
        fail(error, "cannot read the file");
        return std::nullopt;
    }
    constexpr std::size_t kBytes = kZoneRoomRecords * kZoneRoomRecordSize;
    std::uint64_t at = 0;
    if (!map_va(*headers, kZoneRoomTableVa, kBytes, at, error)) return std::nullopt;
    auto table = read(at, kBytes);
    if (!table || table->size() != kBytes) {
        fail(error, "the file ends inside the table");
        return std::nullopt;
    }
    std::vector<ZoneRoomPreset> out;
    out.reserve(kZoneRoomRecords);
    ByteReader r(*table);
    for (std::size_t i = 0; i < kZoneRoomRecords; ++i) {
        auto name = r.bytes(32);
        const char* p = reinterpret_cast<const char*>(name.data());
        std::size_t len = 0;
        while (len < 32 && p[len] != 0) ++len;
        ZoneRoomPreset z;
        z.zone.assign(p, len);
        std::int32_t rooms[3];
        for (auto& room : rooms) room = r.le<std::int32_t>();
        if (len == 32 || !valid_name(z.zone)) {
            fail(error, "a record's zone name is not a short name");
            return std::nullopt;
        }
        for (std::int32_t room : rooms)
            if (room < 0 || room > kMaxRoomType) {
                fail(error, "a record's room type is out of range");
                return std::nullopt;
            }
        z.outside = rooms[0];
        z.high = rooms[1];
        z.low = rooms[2];
        out.push_back(std::move(z));
    }
    return out;
}

std::optional<std::vector<ZoneRoomPreset>> read_zone_room_presets(std::span<const std::byte> exe, std::string* error) {
    return read_zone_room_presets(
        [exe](std::uint64_t offset, std::size_t size) -> std::optional<std::vector<std::byte>> {
            if (offset > exe.size()) return std::nullopt;
            auto s = exe.subspan(static_cast<std::size_t>(offset)).first(std::min(size, static_cast<std::size_t>(exe.size() - offset)));
            return std::vector<std::byte>(s.begin(), s.end());
        },
        error);
}

std::vector<ZoneRoomPreset> load_zone_room_presets(const GameDir& game) {
    static std::atomic<bool> logged{false};
    auto note = [&](const std::string& why) {
        if (!logged.exchange(true))
            log::info("audio: no per-zone room presets ({}); every zone uses the default rooms", why);
    };
    auto path = game.find_base("eqgame.exe");
    if (!path) {
        note("no eqgame.exe in the asset folder");
        return {};
    }
    std::ifstream f(*path, std::ios::binary);
    if (!f) {
        note("cannot open eqgame.exe");
        return {};
    }
    // Only what is asked for is read (the exe is several MB), and the file is opened for reading only.
    ExeReader read = [&f](std::uint64_t offset, std::size_t size) -> std::optional<std::vector<std::byte>> {
        f.clear();
        f.seekg(static_cast<std::streamoff>(offset));
        if (!f) return std::nullopt;
        std::vector<std::byte> out(size);
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (f.bad()) return std::nullopt;
        out.resize(static_cast<std::size_t>(f.gcount())); // short at the end of the file
        return out;
    };
    std::string error;
    auto table = read_zone_room_presets(read, &error);
    if (!table) {
        note("eqgame.exe: " + error);
        return {};
    }
    return std::move(*table);
}

} // namespace hailstone::audio
