#include "app/server_links.h"

#include <algorithm>

namespace nmsc::app {

ServerLinks::Decision ServerLinks::admit(const std::string& url, std::uint64_t now_ms,
                                         const std::function<bool(std::uint32_t)>& live) {
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
    std::erase_if(tabs_, [&](std::uint32_t id) { return !live || !live(id); });
    if (tabs_.size() >= kMaxTabs) {
        d.action = Action::Reuse;
        d.tab = tabs_.back();
        return d;
    }
    d.action = Action::Open;
    return d;
}

void ServerLinks::opened(std::uint32_t tab) {
    if (tab) tabs_.push_back(tab);
}

} // namespace nmsc::app
