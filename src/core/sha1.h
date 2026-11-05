#pragma once

// SHA-1 (FIPS 180-4): /survey sends the hash of the account id, as the original's SHA1_Update / SHA1_Final do.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace nmsc {

using Sha1Digest = std::array<std::uint8_t, 20>;

Sha1Digest sha1(std::string_view data);
// Lower-case hex, 40 characters.
std::string to_hex(const Sha1Digest& d);

} // namespace nmsc
