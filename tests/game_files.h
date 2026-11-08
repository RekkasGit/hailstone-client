#pragma once

// Tests that need the real game files run only when EQ_GAME_DIR points at a
// RoF2 installation; otherwise they are skipped.

#include <cstdlib>
#include <memory>

#include "core/game_dir.h"

namespace hailstone::test {

inline const GameDir* game_dir() {
    static std::unique_ptr<GameDir> dir = [] {
        const char* env = std::getenv("EQ_GAME_DIR");
        return env && *env ? std::make_unique<GameDir>(env) : nullptr;
    }();
    return dir.get();
}

} // namespace hailstone::test
