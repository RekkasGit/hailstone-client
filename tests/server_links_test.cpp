// app::ServerLinks: the limits on the server's OP_Weblink pages (docs/browser.md).
#include <doctest/doctest.h>

#include <set>
#include <string>

#include "app/server_links.h"

using nmsc::app::ServerLinks;

TEST_CASE("server links: a flood of OP_Weblink opens a few pages, not one per packet") {
    ServerLinks links;
    std::set<std::uint32_t> open;
    auto live = [&](std::uint32_t id) { return open.count(id) != 0; };
    std::uint32_t next = 1;
    int opened = 0, reused = 0, dropped = 0, first_drops = 0;
    for (int i = 0; i < 500; ++i) {
        auto d = links.admit("https://example.org/?n=" + std::to_string(i), 1'000, live);
        if (d.action == ServerLinks::Action::Open) {
            ++opened;
            open.insert(next);
            links.opened(next++);
        } else if (d.action == ServerLinks::Action::Reuse) {
            ++reused;
            CHECK(live(d.tab));
        } else {
            ++dropped;
            first_drops += d.first_drop ? 1 : 0;
        }
    }
    CHECK(opened == static_cast<int>(ServerLinks::kMaxTabs));
    CHECK(opened + reused == static_cast<int>(ServerLinks::kPerWindow));
    CHECK(dropped == 500 - static_cast<int>(ServerLinks::kPerWindow));
    CHECK(first_drops == 1);
    // A minute on, links go again; a tab the player closed is not reused.
    open.erase(4);
    auto d = links.admit("https://example.org/later", 1'000 + ServerLinks::kWindowMs, live);
    CHECK(d.action == ServerLinks::Action::Open);
}

TEST_CASE("server links: the same address twice in a row opens once") {
    ServerLinks links;
    auto none = [](std::uint32_t) { return false; };
    CHECK(links.admit("https://example.org/", 0, none).action == ServerLinks::Action::Open);
    links.opened(0); // the system's browser
    CHECK(links.admit("https://example.org/", 5'000, none).action == ServerLinks::Action::Drop);
    CHECK(links.admit("https://example.org/b", 5'000, none).action == ServerLinks::Action::Open);
    CHECK(links.admit("https://example.org/", ServerLinks::kRepeatMs, none).action == ServerLinks::Action::Open);
}
