#pragma once

// The game-side decoders of server data in tests/fuzz_decoders.inc (HAILSTONE_FUZZ_GAME_DECODER): each takes the packet's
// bytes as the span or text it is declared with.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "game/barter.h"
#include "game/missiles.h"
#include "game/zoning.h"

namespace hailstone::fuzz_game {

template <typename R> void feed(R (*fn)(std::span<const std::byte>), std::span<const std::byte> d) { (void)fn(d); }

template <typename R> void feed(R (*fn)(std::span<const std::uint8_t>), std::span<const std::byte> d) {
    (void)fn(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(d.data()), d.size()));
}

template <typename R> void feed(R (*fn)(std::string_view), std::span<const std::byte> d) {
    (void)fn(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
}

} // namespace hailstone::fuzz_game
