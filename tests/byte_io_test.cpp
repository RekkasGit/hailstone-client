#include <doctest/doctest.h>

#include <array>

#include "core/byte_io.h"

using nmsc::ByteReader;
using nmsc::ByteWriter;

namespace {
template <std::size_t N> std::array<std::byte, N> bytes(const unsigned char (&raw)[N]) {
    std::array<std::byte, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<std::byte>(raw[i]);
    return out;
}
} // namespace

TEST_CASE("ByteReader reads little- and big-endian integers") {
    const unsigned char raw[] = {0x01, 0x02, 0x03, 0x04, 0xff, 0xfe};
    auto data = bytes(raw);
    ByteReader r(data);
    CHECK(r.le<std::uint16_t>() == 0x0201);
    CHECK(r.be<std::uint16_t>() == 0x0304);
    CHECK(r.le<std::int16_t>() == -257);
    CHECK(r.empty());
    CHECK_THROWS_AS(r.le<std::uint8_t>(), std::out_of_range);
}

TEST_CASE("ByteReader reads floats and strings") {
    ByteWriter w;
    w.f32(1.5f);
    w.fixed_string("Everquest", 12);
    w.bytes(std::as_bytes(std::span("abc", 4))); // includes the NUL
    auto data = w.take();
    REQUIRE(data.size() == 4 + 12 + 4);

    ByteReader r(data);
    CHECK(r.f32() == 1.5f);
    CHECK(r.fixed_string(12) == "Everquest");
    CHECK(r.cstring() == "abc");
    CHECK(r.empty());
}

TEST_CASE("ByteWriter round-trips and truncates fixed strings") {
    ByteWriter w;
    w.be<std::uint16_t>(0x0001); // SOE OP_SessionRequest
    w.le<std::uint32_t>(0xdeadbeef);
    w.le<std::int64_t>(-2);
    w.fixed_string("toolong", 4);
    auto data = w.take();

    ByteReader r(data);
    CHECK(std::to_integer<int>(data[0]) == 0x00);
    CHECK(std::to_integer<int>(data[1]) == 0x01);
    CHECK(r.be<std::uint16_t>() == 1);
    CHECK(r.le<std::uint32_t>() == 0xdeadbeef);
    CHECK(r.le<std::int64_t>() == -2);
    CHECK(r.fixed_string(4) == "tool");
}

TEST_CASE("ByteReader rejects unterminated strings and bad seeks") {
    const unsigned char raw[] = {'a', 'b'};
    auto data = bytes(raw);
    ByteReader r(data);
    CHECK_THROWS_AS(r.cstring(), std::out_of_range);
    CHECK_THROWS_AS(r.seek(3), std::out_of_range);
    r.seek(2);
    CHECK(r.empty());
}
