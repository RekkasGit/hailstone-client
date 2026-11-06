#pragma once

// The server's OP_Weblink pages (docs/browser.md): the original opens each in an in-game HTML window at once, but a
// server's link is not the player's choice, so a server (or a quest looping on SendWebLink) may not open pages
// without bound. A deliberate difference, only past what a quest plausibly sends (docs/unknowns.md "OP_Weblink"):
// the same address again within kRepeatMs is not opened again, no more than kPerWindow open in kWindowMs, and once
// kMaxTabs of the links' tabs are open the newest is sent to the next address instead of a new tab.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
    // What to do with a link that came at `now_ms` (any steady clock); `live` says whether a tab from opened() is
    // still open.
    Decision admit(const std::string& url, std::uint64_t now_ms, const std::function<bool(std::uint32_t)>& live);
    // The tab a link opened (Open), or 0 when it went to the system's browser.
    void opened(std::uint32_t tab);

private:
    struct Recent {
        std::string url;
        std::uint64_t at = 0;
    };
    std::deque<Recent> recent_; // what was let through within kWindowMs, oldest first
    std::vector<std::uint32_t> tabs_; // the links' tabs, oldest first
    bool dropping_ = false;
};

} // namespace nmsc::app
