#include "game/reward_selection.h"

#include <algorithm>
#include <utility>
#include "core/process_serial.h"

namespace hailstone::game {
namespace {
bool reject(std::string* why, const char* message) {
    if (why) *why = message;
    return false;
}
const net::RewardOption* option(const RewardTab& tab, std::uint32_t id) {
    const auto it = std::find_if(tab.reward.options.begin(), tab.reward.options.end(),
        [id](const net::RewardOption& value) { return value.wire_id == id; });
    return it == tab.reward.options.end() ? nullptr : &*it;
}
bool collection_fits(const std::vector<RewardTab>& tabs) {
    if (tabs.size() > net::kMaxRewardTabs) return false;
    std::size_t options = 0, entries = 0, items = 0, text = 0;
    const auto add = [](std::size_t count, std::size_t limit, std::size_t& used) {
        if (count > limit - used) return false;
        used += count;
        return true;
    };
    for (const auto& tab : tabs) {
        if (!add(tab.reward.title.size(), 2u << 20, text) ||
            !add(tab.reward.options.size(), net::kMaxRewardOptions, options)) return false;
        for (const auto& choice : tab.reward.options) {
            if (!add(choice.label.size(), 2u << 20, text) ||
                !add(choice.entries.size(), net::kMaxRewardEntries, entries)) return false;
            for (const auto& entry : choice.entries) {
                if (!add(entry.description.size(), 2u << 20, text) ||
                    !add(entry.items.size(), net::kMaxRewardItems, items)) return false;
                for (const auto& item : entry.items) if (!add(item.name.size(), 2u << 20, text)) return false;
            }
        }
    }
    return true;
}
} // namespace

std::uint64_t RewardSelection::next_token() {
    return core::next_process_identity();
}
RewardTab* RewardSelection::find_tab(RewardLane value, std::uint64_t revision, std::uint64_t token) {
    auto& state = lanes_[index(value)];
    if (state.revision != revision) return nullptr;
    const auto it = std::find_if(state.tabs.begin(), state.tabs.end(),
        [token](const RewardTab& tab) { return tab.token == token; });
    return it == state.tabs.end() ? nullptr : &*it;
}
void RewardSelection::show(RewardLane value) { lanes_[index(value)].shown = true; }
void RewardSelection::close(RewardLane value) {
    auto& state = lanes_[index(value)];
    state.shown = false;
    // Also retires an outgoing open intent whose hook closes an initially hidden page.
    state.revision = next_token();
    retire_inspection(value);
}
void RewardSelection::close_all() { close(RewardLane::Pending); close(RewardLane::Preview); }
void RewardSelection::reset() {
    claim_.reset();
    for (std::size_t i = 0; i < lanes_.size(); ++i) {
        lanes_[i] = {};
        lanes_[i].revision = next_token();
        inspections_[i].reset();
        retired_inspections_[i].clear();
    }
}
void RewardSelection::retire_inspection(RewardLane value) {
    const auto i = index(value);
    if (inspections_[i]) {
        retired_inspections_[i].emplace(inspections_[i]->request.item_id, inspections_[i]->request.request_token);
        inspections_[i].reset();
    }
    lanes_[i].inspect_pending = false;
}

std::optional<net::Item> RewardSelection::receive(net::RewardPacket packet) {
    auto& state = lanes_[index(packet.lane)];
    if (packet.kind == net::RewardPacket::Kind::Collection) {
        if (!packet.replace_collection && packet.sets.empty()) return std::nullopt;
        // Build before publishing, preserving the old collection if allocation fails.
        std::vector<RewardTab> tabs = packet.replace_collection ? std::vector<RewardTab>{} : state.tabs;
        if (!packet.replace_collection) std::erase_if(tabs, [&](const RewardTab& old) {
            return std::any_of(packet.sets.begin(), packet.sets.end(), [&](const net::RewardSet& incoming) {
                return incoming.pending_id == old.reward.pending_id;
            });
        });
        tabs.reserve(tabs.size() + packet.sets.size());
        for (auto& reward : packet.sets) tabs.push_back({next_token(), std::move(reward), false});
        if (!collection_fits(tabs)) return std::nullopt;
        retire_inspection(packet.lane);
        state.tabs = std::move(tabs);
        state.revision = next_token();
        if (!state.tabs.empty()) state.shown = true;
        if (!state.claim_pending && !state.claim_uncertain) state.message.clear();
        return std::nullopt;
    }
    if (packet.kind == net::RewardPacket::Kind::Claim) {
        if (packet.lane != RewardLane::Pending || !claim_) return std::nullopt;
        const auto request = claim_->request;
        const auto& reply = packet.claim;
        if (reply.pending_id != request.pending_id || reply.set_id != request.set_id ||
            reply.wire_option_id != request.wire_option_id) return std::nullopt;
        // This terminal drains the original reservation even if a newer collection arrived.
        // It must never delete/block an equally numbered tab from that newer collection.
        const auto found = std::find_if(state.tabs.begin(), state.tabs.end(),
            [&](const RewardTab& value) { return value.token == request.tab_token; });
        auto* tab = found == state.tabs.end() ? nullptr : &*found;
        if (tab && reply.success) {
            std::erase_if(state.tabs, [&](const RewardTab& value) { return value.token == request.tab_token; });
            state.revision = next_token();
        } else if (tab) {
            tab->blocked = true;
            state.revision = next_token();
        }
        state.claim_pending = state.claim_uncertain = false;
        state.message = reply.success ? "Reward choice accepted. Delivery is confirmed by the server's item and currency updates."
                                      : "Reward choice was not accepted. Restore pending rewards to obtain a fresh server view.";
        claim_.reset();
        return std::nullopt;
    }
    if (!packet.inspection) return std::nullopt;
    const auto i = index(packet.lane);
    const auto id = packet.inspection->item_id;
    if (retired_inspections_[i].erase(id)) return std::nullopt;
    if (!inspections_[i] || inspections_[i]->request.item_id != id) return std::nullopt;
    const auto request = inspections_[i]->request;
    const bool current = state.shown && find_tab(packet.lane, request.revision, request.tab_token);
    inspections_[i].reset();
    state.inspect_pending = false;
    if (!current) return std::nullopt;
    return std::move(packet.inspection->item);
}

std::optional<RewardClaimRequest> RewardSelection::begin_claim(std::uint64_t revision, std::uint64_t token,
    std::uint32_t id, std::int64_t now, std::string* why) {
    auto& state = lanes_[0];
    auto* tab = find_tab(RewardLane::Pending, revision, token);
    if (!state.shown || !tab) { reject(why, "The reward view changed. Select the reward again."); return std::nullopt; }
    if (claim_) { reject(why, "A reward choice is still awaiting its terminal server reply."); return std::nullopt; }
    const auto* selected = option(*tab, id);
    if (tab->blocked || !selected || selected->common) {
        reject(why, "This is not a current selectable reward option."); return std::nullopt;
    }
    const bool ambiguous = std::any_of(state.tabs.begin(), state.tabs.end(), [&](const RewardTab& other) {
        return other.token != token && other.reward.pending_id == tab->reward.pending_id &&
            other.reward.set_id == tab->reward.set_id && option(other, id);
    });
    if (ambiguous) { reject(why, "The server supplied an ambiguous reward choice identity."); return std::nullopt; }
    RewardClaimRequest request{next_token(), revision, token, tab->reward.pending_id, tab->reward.set_id, id};
    claim_ = Claim{request, now};
    state.claim_pending = true;
    state.claim_uncertain = false;
    state.message = "Waiting for the server to confirm the reward choice.";
    return request;
}
std::optional<RewardInspectRequest> RewardSelection::begin_inspect(RewardLane value, std::uint64_t revision,
    std::uint64_t token, std::uint32_t id, std::uint32_t entry_id, std::uint32_t item_id,
    std::int64_t now, std::string* why) {
    const auto i = index(value);
    auto& state = lanes_[i];
    auto* tab = find_tab(value, revision, token);
    if (!state.shown || !tab) { reject(why, "The reward view changed. Select the item again."); return std::nullopt; }
    if ((value == RewardLane::Pending && claim_) || inspections_[i]) {
        reject(why, "A reward request is already pending on this page."); return std::nullopt;
    }
    if (retired_inspections_[i].size() >= net::kMaxRewardTabs || retired_inspections_[i].contains(item_id)) {
        reject(why, "An older item reply has not drained. Reconnect before inspecting this item again."); return std::nullopt;
    }
    const auto* selected = option(*tab, id);
    bool found = false;
    if (selected && item_id) for (const auto& entry : selected->entries) {
        if (entry.entry_id != entry_id || entry.type != net::RewardEntryType::Item) continue;
        found = std::any_of(entry.items.begin(), entry.items.end(),
            [item_id](const net::RewardItem& item) { return item.id == item_id; });
        if (found) break;
    }
    if (!found) { reject(why, "This item is not in the current reward option."); return std::nullopt; }
    RewardInspectRequest request{next_token(), revision, token, value, tab->reward.set_id, id, entry_id, item_id};
    inspections_[i] = Inspect{request, now};
    state.inspect_pending = true;
    return request;
}
void RewardSelection::cancel_unsent_claim(const RewardClaimRequest& request) {
    if (!claim_ || claim_->request.request_token != request.request_token) return;
    claim_.reset();
    lanes_[0].claim_pending = lanes_[0].claim_uncertain = false;
    lanes_[0].message = "The reward request was not sent.";
}
void RewardSelection::cancel_unsent_inspect(const RewardInspectRequest& request) {
    const auto i = index(request.lane);
    if (inspections_[i] && inspections_[i]->request.request_token == request.request_token) {
        inspections_[i].reset();
        lanes_[i].inspect_pending = false;
    }
    const auto retired = retired_inspections_[i].find(request.item_id);
    if (retired != retired_inspections_[i].end() && retired->second == request.request_token)
        retired_inspections_[i].erase(retired);
}
bool RewardSelection::claim_current(const RewardClaimRequest& request) const {
    const auto& state = lanes_[0];
    return claim_ && claim_->request.request_token == request.request_token && state.shown &&
        state.revision == request.revision && std::any_of(state.tabs.begin(), state.tabs.end(),
            [&](const RewardTab& tab) { return tab.token == request.tab_token && !tab.blocked; });
}
bool RewardSelection::inspect_current(const RewardInspectRequest& request) const {
    const auto i = index(request.lane);
    const auto& state = lanes_[i];
    return inspections_[i] && inspections_[i]->request.request_token == request.request_token && state.shown &&
        state.revision == request.revision && std::any_of(state.tabs.begin(), state.tabs.end(),
            [&](const RewardTab& tab) { return tab.token == request.tab_token; });
}
void RewardSelection::update(std::int64_t now) {
    if (claim_ && now >= claim_->sent_ms && now - claim_->sent_ms >= kRequestTimeoutMs) {
        lanes_[0].claim_pending = false;
        lanes_[0].claim_uncertain = true;
        lanes_[0].message = "The reward result is unknown. Do not repeat the choice; zone or reconnect to restore the server's state if its reply never arrives.";
    }
    for (const auto value : {RewardLane::Pending, RewardLane::Preview}) {
        const auto i = index(value);
        if (inspections_[i] && now >= inspections_[i]->sent_ms && now - inspections_[i]->sent_ms >= kRequestTimeoutMs) {
            retire_inspection(value);
            lanes_[i].message = "The item inspection timed out.";
        }
    }
}

} // namespace hailstone::game
