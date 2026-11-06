#include "app/server_links.h"

#include <algorithm>
#include <string_view>

namespace nmsc::app {

namespace {

// The browser writes "https://example.org" back as "https://example.org/".
bool same_page(std::string_view a, std::string_view b) {
    if (!a.empty() && a.back() == '/') a.remove_suffix(1);
    if (!b.empty() && b.back() == '/') b.remove_suffix(1);
    return a == b;
}

} // namespace

ServerLinks::Decision ServerLinks::admit(const std::string& url, std::uint64_t now_ms,
                                         const std::function<std::optional<std::string>(std::uint32_t)>& url_of) {
    while (!recent_.empty() && now_ms - recent_.front().at >= kWindowMs) recent_.pop_front();
    if (recent_.empty()) dropping_ = false;
    const bool repeat = std::any_of(recent_.begin(), recent_.end(), [&](const Recent& r) {
        return r.url == url && now_ms - r.at < kRepeatMs;
    });
    Decision d;
    if (repeat || recent_.size() >= kPerWindow) {
        d.first_drop = !dropping_;
        dropping_ = true;
        return d;
    }
    recent_.push_back({url, now_ms});
    std::erase_if(tabs_, [&](const LinkTab& t) {
        const std::optional<std::string> now = url_of ? url_of(t.id) : std::nullopt;
        return !now || !same_page(*now, t.url);
    });
    if (tabs_.size() >= kMaxTabs) {
        d.action = Action::Reuse;
        d.tab = tabs_.back().id;
        return d;
    }
    d.action = Action::Open;
    return d;
}

void ServerLinks::opened(std::uint32_t tab, const std::string& url) {
    if (!tab) return;
    const auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const LinkTab& t) { return t.id == tab; });
    if (it != tabs_.end()) it->url = url;
    else tabs_.push_back({tab, url});
}

} // namespace nmsc::app
