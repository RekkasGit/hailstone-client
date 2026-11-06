// app::ServerLinks: the limits on the server's OP_Weblink pages (docs/browser.md).
#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>

#include "app/server_links.h"

using nmsc::app::ServerLinks;

namespace {

// The tabs that are open, and the address each shows.
struct Tabs {
    std::map<std::uint32_t, std::string> open;
    std::optional<std::string> operator()(std::uint32_t id) const {
        auto it = open.find(id);
        if (it == open.end()) return std::nullopt;
        return it->second;
    }
};

} // namespace

TEST_CASE("server links: a flood of OP_Weblink opens a few pages, not one per packet") {
    ServerLinks links;
    Tabs tabs;
    auto url_of = [&](std::uint32_t id) { return tabs(id); };
    std::uint32_t next = 1;
    int opened = 0, reused = 0, dropped = 0, first_drops = 0;
    for (int i = 0; i < 500; ++i) {
        const std::string url = "https://example.org/?n=" + std::to_string(i);
        auto d = links.admit(url, 1'000, url_of);
        if (d.action == ServerLinks::Action::Open) {
            ++opened;
            tabs.open[next] = url;
            links.opened(next++, url);
        } else if (d.action == ServerLinks::Action::Reuse) {
            ++reused;
            REQUIRE(tabs.open.count(d.tab) == 1);
            tabs.open[d.tab] = url;
            links.opened(d.tab, url);
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
    tabs.open.erase(4);
    auto d = links.admit("https://example.org/later", 1'000 + ServerLinks::kWindowMs, url_of);
    CHECK(d.action == ServerLinks::Action::Open);
}

TEST_CASE("server links: a tab the player browsed on from is never taken over") {
    ServerLinks links;
    Tabs tabs;
    auto url_of = [&](std::uint32_t id) { return tabs(id); };
    for (std::uint32_t id = 1; id <= ServerLinks::kMaxTabs; ++id) {
        const std::string url = "https://example.org/" + std::to_string(id);
        REQUIRE(links.admit(url, 0, url_of).action == ServerLinks::Action::Open);
        tabs.open[id] = url;
        links.opened(id, url);
    }
    // The browser writes a bare host back with a '/': still the link's page.
    tabs.open[1] = "https://example.org/1/";
    // The newest tab is now somewhere the player went: the next link opens a tab of its own.
    tabs.open[ServerLinks::kMaxTabs] = "https://player.example.net/inbox";
    auto d = links.admit("https://example.org/next", ServerLinks::kWindowMs, url_of);
    CHECK(d.action == ServerLinks::Action::Open);
    tabs.open[9] = "https://example.org/next";
    links.opened(9, "https://example.org/next");
    // Four of the links' tabs again: the next goes to the newest of them, never the player's.
    d = links.admit("https://example.org/again", ServerLinks::kWindowMs, url_of);
    REQUIRE(d.action == ServerLinks::Action::Reuse);
    CHECK(d.tab == 9);
}

TEST_CASE("server links: the same address twice in a row opens once") {
    ServerLinks links;
    auto none = [](std::uint32_t) { return std::optional<std::string>{}; };
    CHECK(links.admit("https://example.org/", 0, none).action == ServerLinks::Action::Open);
    links.opened(0, "https://example.org/"); // the system's browser
    CHECK(links.admit("https://example.org/", 5'000, none).action == ServerLinks::Action::Drop);
    CHECK(links.admit("https://example.org/b", 5'000, none).action == ServerLinks::Action::Open);
    CHECK(links.admit("https://example.org/", ServerLinks::kRepeatMs, none).action == ServerLinks::Action::Open);
}
