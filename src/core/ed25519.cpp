#include "core/ed25519.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

namespace nmsc {

bool ed25519_verify(const Ed25519Signature& signature, const Ed25519PublicKey& public_key,
                    std::span<const std::byte> message) {
    return crypto_ed25519_check(signature.data(), public_key.data(),
                                reinterpret_cast<const std::uint8_t*>(message.data()), message.size()) == 0;
}

bool ed25519_verify(const Ed25519Signature& signature, const Ed25519PublicKey& public_key, std::string_view message) {
    return ed25519_verify(signature, public_key, std::as_bytes(std::span(message.data(), message.size())));
}

void ed25519_key_pair(const std::array<std::uint8_t, 32>& seed, Ed25519SecretKey& secret_key,
                      Ed25519PublicKey& public_key) {
    std::array<std::uint8_t, 32> copy = seed; // Monocypher wipes the seed it is given
    crypto_ed25519_key_pair(secret_key.data(), public_key.data(), copy.data());
}

Ed25519Signature ed25519_sign(const Ed25519SecretKey& secret_key, std::string_view message) {
    Ed25519Signature signature{};
    crypto_ed25519_sign(signature.data(), secret_key.data(), reinterpret_cast<const std::uint8_t*>(message.data()),
                        message.size());
    return signature;
}

std::string hex_encode(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

namespace {

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

template <std::size_t N> std::optional<std::array<std::uint8_t, N>> hex_decode(std::string_view hex) {
    if (hex.size() != N * 2) return std::nullopt;
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = static_cast<std::uint8_t>(hi << 4 | lo);
    }
    return out;
}

template std::optional<std::array<std::uint8_t, 32>> hex_decode<32>(std::string_view);
template std::optional<std::array<std::uint8_t, 64>> hex_decode<64>(std::string_view);

} // namespace nmsc
