// Hailstone: sorting, stacking and tidying bags (game/item_sort.h), run as clicks through click_slot, and selling
// the items marked as junk to the open merchant.

#include <algorithm>

#include "core/log.h"
#include "game/merchant.h"
#include "net/game_connection.h"

namespace hailstone::net {

namespace {
constexpr std::int64_t kItemJobStepMs = 60; // between two clicks
constexpr std::int64_t kSellStepMs = 150;   // between two sales (each also waits for the merchant's answer)
}

std::string GameConnection::item_job_blocked(ItemJobScope scope) const {
    if (state_ != ConnectionState::InZone || pstatus_.dead) return "You cannot move items now.";
    if (scope == ItemJobScope::Bank && !bank_) return "The bank is not open.";
    if (inventory_.cursor()) return "Put down what you are holding first.";
    for (std::uint32_t c : cursor_coins_)
        if (c) return "Put down the coin you are holding first.";
    if (quantity_) return "Close the quantity window first.";
    if (merchant_ && merchant_->open) return "Close the merchant first.";
    if (trade_) return "Finish the trade first.";
    return {};
}

std::string GameConnection::start_item_job(ItemJobScope scope, const ItemSlot& bag, ItemJobMode mode,
                                           const ItemJobOptions& options) {
    if (item_job_) return "Already sorting.";
    if (mode == ItemJobMode::Sell) return "Use start_sell_job.";
    if (auto why = item_job_blocked(scope); !why.empty()) return why;
    std::vector<game::SortPosition> positions;
    if (scope == ItemJobScope::Bag) {
        if (bag.type == kInvBank && !bank_) return "The bank is not open.";
        if (bag.type != kInvPossessions && bag.type != kInvBank) return "Only bags you carry or bank can be sorted.";
        positions = game::bag_positions(inventory_, bag);
    } else if (scope == ItemJobScope::Inventory) {
        std::vector<std::int16_t> slots;
        for (int s = kSlotGeneral1; s <= kSlotGeneral10; ++s)
            if (inv_slot_available(s)) slots.push_back(static_cast<std::int16_t>(s));
        positions = game::sort_positions(inventory_, kInvPossessions, slots);
    } else {
        std::vector<std::int16_t> slots;
        for (int s = 0; s < bank_->slots; ++s) slots.push_back(static_cast<std::int16_t>(s));
        positions = game::sort_positions(inventory_, kInvBank, slots);
    }
    // the filters, and the locked slots out
    for (auto& p : positions)
        for (const auto& [top, category] : options.filters)
            if (top.same(p.slot.parent())) p.filter = category;
    std::erase_if(positions, [&](const game::SortPosition& p) {
        return std::any_of(options.locked.begin(), options.locked.end(), [&](const ItemSlot& l) { return l.same(p.slot); });
    });
    std::vector<game::SortClick> clicks;
    if (mode == ItemJobMode::Stack) {
        clicks = game::plan_stack(inventory_, positions);
    } else if (mode == ItemJobMode::Tidy) {
        clicks = game::plan_tidy(inventory_, positions);
    } else if (auto plan = game::plan_sort(inventory_, positions)) {
        clicks = std::move(*plan);
    } else {
        return "Some items have nowhere they fit.";
    }
    if (clicks.empty()) return {};
    ItemJob job;
    job.clicks = std::move(clicks);
    job.status = {0, job.clicks.size(), mode, scope};
    log::info("item job (mode {}): {} clicks", static_cast<int>(mode), job.clicks.size());
    item_job_ = std::move(job);
    return {};
}

std::string GameConnection::start_sell_job(const std::vector<std::uint32_t>& ids) {
    if (item_job_) return "Already busy with your items.";
    if (state_ != ConnectionState::InZone || pstatus_.dead) return "You cannot sell now.";
    if (!merchant_ || !merchant_->open || merchant_->trader) return "Open a merchant first.";
    // the general slots, then their bags' contents (a bag's own slot never: selling it would sell what it holds)
    ItemJob job;
    auto take = [&](const ItemSlot& at) {
        const Item* it = inventory_.at(at);
        if (it && !it->is_bag() && std::find(ids.begin(), ids.end(), it->id) != ids.end() && game::sell_refusal(*it) == 0 &&
            !kept_items_.kept(it->id)) // Hailstone: a kept item is never sold (game/kept_items.h)
            job.clicks.push_back({at, it->id});
    };
    for (int s = kSlotGeneral1; s <= kSlotGeneral10; ++s) {
        const ItemSlot top = ItemSlot::possessions(static_cast<std::int16_t>(s));
        const Item* it = inventory_.at(top);
        if (!it) continue;
        if (!it->is_bag()) {
            take(top);
            continue;
        }
        for (int i = 0; i < it->bag_slots; ++i) take(ItemSlot::possessions(static_cast<std::int16_t>(s), static_cast<std::int16_t>(i)));
    }
    if (job.clicks.empty()) return "You carry nothing marked to sell that this merchant buys.";
    job.status = {0, job.clicks.size(), ItemJobMode::Sell, ItemJobScope::Inventory};
    item_job_ = std::move(job);
    return {};
}

std::optional<GameConnection::ItemJobStatus> GameConnection::item_job() const {
    if (!item_job_) return std::nullopt;
    return item_job_->status;
}

void GameConnection::update_item_job() {
    if (!item_job_) return;
    ItemJob& job = *item_job_;
    std::int64_t now = now_ms();
    if (now < job.next_ms) return;
    if (job.next >= job.clicks.size()) {
        if (job.status.mode != ItemJobMode::Sell || !merchant_ || merchant_->pending == 0) item_job_.reset();
        return;
    }
    const game::SortClick& c = job.clicks[job.next];
    if (job.status.mode == ItemJobMode::Sell) {
        // one sale at a time: the merchant's answer clears pending (CMerchantWnd's "one thing at a time")
        if (state_ != ConnectionState::InZone || !merchant_ || !merchant_->open) {
            item_job_.reset();
            return;
        }
        if (merchant_->pending > 0) return;
        job.next_ms = now + kSellStepMs;
        ++job.next;
        job.status.done = job.next;
        const Item* it = inventory_.at(c.slot);
        if (!it || it->id != c.cursor_id) return; // gone or moved since: skipped
        merchant_sell(c.slot, it->stackable() ? it->stack_count : 1);
        return;
    }
    job.next_ms = now + kItemJobStepMs;
    const Item* held = inventory_.cursor();
    // Something else moved (the server put an item back, the player clicked, a window opened): stop, and put away
    // what the cursor holds.
    bool blocked = state_ != ConnectionState::InZone || pstatus_.dead || quantity_ || (merchant_ && merchant_->open) ||
                   trade_ || (job.status.scope == ItemJobScope::Bank && !bank_) ||
                   (c.slot.type == kInvBank && !bank_);
    if (blocked || (held ? held->id : 0u) != c.cursor_id) {
        log::info("item job stopped at click {} of {}", job.next, job.clicks.size());
        item_job_.reset();
        if (inventory_.cursor()) auto_inventory();
        chat(ChatEvent::Kind::System, "Sorting stopped: your items changed while it ran.");
        return;
    }
    ++job.next;
    job.status.done = job.next;
    click_slot(c.slot, true, false);
}

} // namespace hailstone::net
