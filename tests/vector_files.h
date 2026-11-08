#pragma once

// Reading the recorded test vectors (*.jsonl, recorded from the original
// eqgame.exe): the directory (HAILSTONE_VECTORS_DIR), a per-record runner that
// counts mismatches, and float helpers. Without the directory the checks pass
// with a message.

#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/env.h"
#include "json_lite.h"

namespace hailstone::test {

inline std::optional<std::filesystem::path> vector_dir() {
    namespace fs = std::filesystem;
    if (const char* env = hailstone::core::env("VECTORS_DIR")) return fs::path(env);
    return std::nullopt;
}

// The recordings name a function they have no name for by its address with a
// FUN_ prefix and 8 hex digits; the tests name it as the comments do, 0x<address>
// ("FUN_008cfd10" -> "0x8cfd10"). Rewrites every such name in a record's line.
inline void normalise_recorded_names(std::string& line) {
    constexpr std::string_view prefix = "FUN_";
    auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); };
    for (std::size_t at = line.find(prefix); at != std::string::npos; at = line.find(prefix, at + 1)) {
        std::size_t digits = at + prefix.size();
        if (digits + 8 > line.size()) break;
        bool ok = true;
        for (std::size_t i = digits; i < digits + 8; ++i) ok = ok && hex(line[i]);
        if (!ok) continue;
        std::size_t first = digits;
        while (first < digits + 7 && line[first] == '0') ++first;
        line.replace(at, digits + 8 - at, "0x" + line.substr(first, digits + 8 - first));
    }
}

// Runs `check` on every record of `file` (the whole record); it returns a
// mismatch text or "". Reports the count and fails on any mismatch.
inline void each_vector(const char* file, const std::function<std::string(const Json& record)>& check) {
    auto dir = vector_dir();
    std::ifstream f(dir ? *dir / file : std::filesystem::path());
    if (!f) {
        MESSAGE(std::string(file) << ": no vectors (HAILSTONE_VECTORS_DIR); skipped");
        return;
    }
    std::string line;
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        normalise_recorded_names(line);
        ++n;
        std::string why = check(parse_json(line));
        if (!why.empty() && ++bad <= 10) FAIL_CHECK(std::string(file) << ":" << n << ": " << why);
    }
    MESSAGE(std::string(file) << ": " << n << " records, " << bad << " mismatches");
    CHECK(n > 0);
    CHECK(bad == 0);
}

// A float's bits as the vectors write it (rof2decomp tools/re/export_vectors.py): a number, "inf", "-inf",
// "nan" (the quiet NaN 0x7fc00000; 0x7ff8000000000000 for a double) or "nan:0x<bits>" for any other NaN.
inline std::uint32_t f32_bits(const Json& j) {
    if (j.type == Json::Type::String && j.text == "nan") return 0x7fc00000u;
    return std::bit_cast<std::uint32_t>(j.f32());
}
inline std::uint64_t f64_bits(const Json& j) {
    if (j.type == Json::Type::String && j.text == "nan") return 0x7ff8000000000000ull;
    if (j.type == Json::Type::String && j.text.rfind("nan:0x", 0) == 0) {
        // A double's NaN keeps its 16 hex digits; a shorter one is a float's, widened.
        if (j.text.size() > 6 + 8) return std::stoull(j.text.substr(6), nullptr, 16);
        return std::bit_cast<std::uint64_t>(static_cast<double>(j.f32()));
    }
    return std::bit_cast<std::uint64_t>(j.f64());
}
inline float f32_of(const Json& j) { return std::bit_cast<float>(f32_bits(j)); }

} // namespace hailstone::test
