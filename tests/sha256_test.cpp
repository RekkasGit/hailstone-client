// core/sha256.h against the FIPS 180-4 examples.
#include <doctest/doctest.h>

#include <string>

#include "core/sha1.h"
#include "core/sha256.h"

using namespace hailstone;

TEST_CASE("sha256: FIPS 180-4 vectors") {
    CHECK(to_hex(sha256(std::string_view(""))) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(to_hex(sha256(std::string_view("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(to_hex(sha256(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::string million(1000000, 'a');
    CHECK(to_hex(sha256(million)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: fed in pieces, the same digest") {
    std::string text = "The quick brown fox jumps over the lazy dog, again and again and again.";
    Sha256 s;
    for (std::size_t i = 0; i < text.size(); i += 7) s.update(std::string_view(text).substr(i, 7));
    CHECK(s.finish() == sha256(text));
}

TEST_CASE("parse_sha256: round trip, either case, rejects bad text") {
    auto d = sha256(std::string_view("abc"));
    CHECK(parse_sha256(to_hex(d)) == d);
    std::string upper = to_hex(d);
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(parse_sha256(upper) == d);
    CHECK_FALSE(parse_sha256("abc"));
    CHECK_FALSE(parse_sha256(std::string(64, 'g')));
}

TEST_CASE("sha1: FIPS 180-4 vectors") {
    CHECK(to_hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(to_hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(to_hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    CHECK(to_hex(sha1(std::string(1000, 'a'))) == to_hex(sha1(std::string(500, 'a') + std::string(500, 'a'))));
}
