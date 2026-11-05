#pragma once

// Reading rof2decomp's test vectors (re/test/vectors/*.jsonl, recorded from
// the original eqgame.exe): the directory (NMSC_VECTORS_DIR, else
// rof2decomp/re/test/vectors beside the source tree, or beside the folder
// holding it for a worktree, found from NMSC_SOURCE_DIR so that a run from
// any directory (ctest) finds it), a per-record runner that counts
// mismatches, and float helpers. Without the directory the checks pass with
// a message.

#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

#include "json_lite.h"

namespace nmsc::test {

inline std::optional<std::filesystem::path> vector_dir() {
    namespace fs = std::filesystem;
    if (const char* env = std::getenv("NMSC_VECTORS_DIR")) return fs::path(env);
#ifdef NMSC_SOURCE_DIR
    const fs::path source(NMSC_SOURCE_DIR);
#else
    const fs::path source(".");
#endif
    // The source tree's sibling, or its parent's sibling (a worktree under ../<repo>-wt/<topic>).
    for (fs::path p : {source / "../rof2decomp/re/test/vectors", source / "../../rof2decomp/re/test/vectors"}) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) return p.lexically_normal();
    }
    return std::nullopt;
}

// Runs `check` on every record of `file` (the whole record); it returns a
// mismatch text or "". Reports the count and fails on any mismatch.
inline void each_vector(const char* file, const std::function<std::string(const Json& record)>& check) {
    auto dir = vector_dir();
    std::ifstream f(dir ? *dir / file : std::filesystem::path());
    if (!f) {
        MESSAGE(std::string(file) << ": no vectors (NMSC_VECTORS_DIR); skipped");
        return;
    }
    std::string line;
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
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

} // namespace nmsc::test
