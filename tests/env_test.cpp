#include <doctest/doctest.h>

#include <map>
#include <string>

#include "core/env.h"

using namespace hailstone;

TEST_CASE("core::env: HAILSTONE_<name>, else NMSC_<name> as it was called up to 0.6") {
    std::map<std::string, std::string> vars;
    auto get = [&](const char* n) -> const char* {
        auto it = vars.find(n);
        return it == vars.end() ? nullptr : it->second.c_str();
    };
    CHECK(core::env("VECTORS_DIR", get) == nullptr);
    vars["NMSC_VECTORS_DIR"] = "/old";
    REQUIRE(core::env("VECTORS_DIR", get) != nullptr);
    CHECK(std::string(core::env("VECTORS_DIR", get)) == "/old");
    vars["HAILSTONE_VECTORS_DIR"] = "/new";
    CHECK(std::string(core::env("VECTORS_DIR", get)) == "/new");
    vars.erase("NMSC_VECTORS_DIR");
    vars["HAILSTONE_VECTORS_DIR"] = "";
    REQUIRE(core::env("VECTORS_DIR", get) != nullptr); // set but empty is still set, as getenv says
    CHECK(std::string(core::env("VECTORS_DIR", get)).empty());
}
