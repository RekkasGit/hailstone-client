// platform/http_get.h without the network: only http and https addresses are fetched, and asking whether this system
// can fetch at all does not fail. The transfers themselves need a server, so are not covered here.
#include <doctest/doctest.h>

#include <string>

#include "platform/http_get.h"

using namespace nmsc::platform;

namespace {

HttpGetResult get(const std::string& url) {
    HttpGetOptions options;
    options.user_agent = "hailstone-test";
    bool called = false;
    HttpGetResult r = http_get(url, options, [&](std::span<const std::byte>) { return called = true; }, {});
    CHECK_FALSE(called);
    return r;
}

} // namespace

TEST_CASE("http_get: refuses addresses that are not http or https") {
    for (const char* url : {"ftp://example.com/file", "file:///etc/passwd", "garbage", "", "https:/", "http://",
                            "javascript:alert(1)"}) {
        CAPTURE(url);
        HttpGetResult r = get(url);
        CHECK_FALSE(r.ok);
        CHECK(r.status == 0);
        CHECK_FALSE(r.error.empty());
    }
}

TEST_CASE("http_get: http_available answers and gives a reason when it cannot") {
    std::string why = "unset";
    const bool available = http_available(&why);
    if (available) CHECK(why.empty());
    else CHECK_FALSE(why.empty());
    CHECK(http_available() == available);
}
