#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// The SDK's C++ header under its name up to 0.6, as a plugin written then includes it (sdk/nmsc_byte_io.h): the
// namespace `nmsc` still names the readers and writers. The C header's old names are built by
// tests/plugins/ExtendTest.c.
#include "nmsc_byte_io.h"

TEST_CASE("plugin SDK: nmsc_byte_io.h and nmsc::ByteReader / ByteWriter still build") {
    nmsc::ByteWriter w;
    w.le<std::uint32_t>(0x01020304u);
    const std::vector<std::byte> bytes = w.data();
    nmsc::ByteReader r(bytes.data(), bytes.size());
    CHECK(r.le<std::uint32_t>() == 0x01020304u);
    hailstone::ByteReader same(bytes.data(), bytes.size()); // the same type
    CHECK(same.le<std::uint32_t>() == 0x01020304u);
}
