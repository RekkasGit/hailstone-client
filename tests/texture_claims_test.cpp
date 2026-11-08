// render/texture_claims.h: the character textures' claims between the appearance jobs and the main thread.

#include <doctest/doctest.h>

#include <memory>
#include <utility>

#include "render/texture_claims.h"

using namespace hailstone::render;

TEST_CASE("Texture claims: a dropped job gives its claims back; installed textures stay claimed") {
    auto claims = std::make_shared<TextureClaims>();
    {
        ClaimHold a(claims);
        CHECK(a.claim("k"));
        CHECK(claims->pending("k"));
        ClaimHold b(claims);
        CHECK_FALSE(b.claim("k")); // A reads it: B does not
        // A's result is dropped (superseded, cancelled) before K was installed.
    }
    CHECK_FALSE(claims->pending("k"));
    // The next job reads it.
    ClaimHold c(claims);
    CHECK(c.claim("k"));
    // Its result is installed: the slot holds K for good, dropping the result keeps it.
    claims->installed("k");
    CHECK_FALSE(claims->pending("k"));
    { ClaimHold moved = std::move(c); }
    CHECK_FALSE(claims->claim("k"));
    CHECK_FALSE(claims->pending("k"));
}

TEST_CASE("Texture claims: a hold moved from job to result releases once, from where it ends") {
    auto claims = std::make_shared<TextureClaims>();
    ClaimHold outer;
    {
        ClaimHold inner(claims);
        CHECK(inner.claim("a"));
        CHECK(inner.claim("b"));
        outer = std::move(inner);
    }
    CHECK(claims->pending("a")); // the moved-from hold gave nothing back
    CHECK(claims->pending("b"));
    claims->installed("a");
    outer = ClaimHold(); // the result goes: only b comes back
    CHECK_FALSE(claims->pending("a"));
    CHECK_FALSE(claims->claim("a"));
    CHECK(claims->claim("b"));
}
