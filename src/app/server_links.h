#pragma once

// The server's OP_Weblink pages (docs/browser.md): the original opens each in an in-game HTML window at once, but a
// server's link is not the player's choice, so a server (or a quest looping on SendWebLink) may not open pages
// without bound. A deliberate difference, only past what a quest plausibly sends (docs/unknowns.md "OP_Weblink"):
// the same address again within kRepeatMs is not opened again, no more than kPerWindow open in kWindowMs, and once
// kMaxTabs of the links' tabs are open the newest is sent to the next address instead of a new tab. A tab is the
// links' only while it still shows the address the last link sent it to: one the player browsed on from (or that a
// page sent elsewhere) is the player's, never taken over, and no longer counts.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nmsc::app {

class ServerLinks {
public:
    static constexpr std::uint64_t kRepeatMs = 10'000;
    static constexpr std::uint64_t kWindowMs = 60'000;
    static constexpr std::size_t kPerWindow = 5;
    static constexpr std::size_t kMaxTabs = 4;

    enum class Action { Open, Reuse, Drop };
    struct Decision {
        Action action = Action::Drop;
        std::uint32_t tab = 0; // Reuse: the tab to send there
        bool first_drop = false; // Drop: the first in this window (worth a log line)
    };
    // What to do with a link that came at `now_ms` (any steady clock); `url_of` gives the address a tab from
    // opened() shows now, or nothing once it is closed.
    Decision admit(const std::string& url, std::uint64_t now_ms,
                   const std::function<std::optional<std::string>(std::uint32_t)>& url_of);
    // The tab a link opened (Open) or was sent to (Reuse) with `url`; tab 0 when it went to the system's browser.
    void opened(std::uint32_t tab, const std::string& url);

private:
    struct Recent {
        std::string url;
        std::uint64_t at = 0;
    };
    std::deque<Recent> recent_; // what was let through within kWindowMs, oldest first
    struct LinkTab {
        std::uint32_t id = 0;
        std::string url; // the last link's address in it
    };
    std::vector<LinkTab> tabs_; // the links' tabs, oldest first
    bool dropping_ = false;
};

} // namespace nmsc::app
