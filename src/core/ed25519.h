#pragma once

// Ed25519 signatures (RFC 8032, with SHA-512) over Monocypher: the update manifest is signed with the project's
// release key and checked with its public half before anything it names is downloaded (docs/updater.md). Signing
// and key making are for the release tool (tools/hailstone_sign.cpp) and the tests.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hailstone {

using Ed25519PublicKey = std::array<std::uint8_t, 32>;
using Ed25519SecretKey = std::array<std::uint8_t, 64>; // Monocypher's: the 32-byte seed, then the public key
using Ed25519Signature = std::array<std::uint8_t, 64>;

// Whether `signature` is `public_key`'s over `message`.
bool ed25519_verify(const Ed25519Signature& signature, const Ed25519PublicKey& public_key,
                    std::span<const std::byte> message);
bool ed25519_verify(const Ed25519Signature& signature, const Ed25519PublicKey& public_key, std::string_view message);

// The key pair of a 32-byte seed (which must be secret and random).
void ed25519_key_pair(const std::array<std::uint8_t, 32>& seed, Ed25519SecretKey& secret_key,
                      Ed25519PublicKey& public_key);
Ed25519Signature ed25519_sign(const Ed25519SecretKey& secret_key, std::string_view message);

// Lower-case hex of any bytes, and back (either case; nullopt unless exactly N bytes of hex).
std::string hex_encode(std::span<const std::uint8_t> bytes);
template <std::size_t N> std::optional<std::array<std::uint8_t, N>> hex_decode(std::string_view hex);
extern template std::optional<std::array<std::uint8_t, 32>> hex_decode<32>(std::string_view);
extern template std::optional<std::array<std::uint8_t, 64>> hex_decode<64>(std::string_view);

} // namespace hailstone
