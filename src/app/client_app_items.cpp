// ClientApp: Item input (loot, inventory), auto-scribe and the item windows.

#include "app/client_app_internal.h"
#include "app/lua_ui_windows.h"
#include "app/spawn_interaction.h"
#include "game/interact.h"
#include "game/item_notify.h"
#include "game/window_notify.h"
#include "plugin/policy.h"

namespace hailstone::app::client_internal {

// Items: a right click on a corpse loots it, as in the original; I toggles the inventory (the original's default
// key); clicked chat links ask for the item.
void ClientApp::update_item_input() {
    // CEverQuest::RMouseUp's click (update_mouse_look: no look, or a right-button look of at most 500 ms), at the
    // place the button went down.
    if (!right_click_) return;
    right_click_ = false;
    // CEverQuest::RMouseUp: ClickedPlayer, then RightClickedOnPlayer.
    PickTarget picked = pick_at(right_down_pos_.x, right_down_pos_.y, game::kClickRequired, game::kClickSkip, "right click");
    auto sx = static_cast<std::int32_t>(right_down_pos_.x), sy = static_cast<std::int32_t>(right_down_pos_.y);
    // RMouseUp: nothing while placing; a placed object asks for its menu (0x21ec); so does a spawn the server marked
    // as a real estate NPC item, before its own right click.
    if (conn_->placement().active()) return;
    // Click to move (the isometric camera's, or mouse.click_move): an NPC or a corpse out of reach is walked to, then
    // right clicked.
    if (click_moves() && iso_click(right_down_pos_, picked, true)) return;
    if (picked.kind == PickTarget::Kind::Placed && world_objects_) {
        if (auto b = world_objects_->placed_bounds(picked.re, picked.item))
            conn_->re_request_placed_menu(picked.re, picked.item, b->center, b->radius, sx, sy);
        return;
    }
    if (picked.kind == PickTarget::Kind::Spawn && conn_->re_npc_item(picked.id) &&
        conn_->re_request_npc_item_menu(picked.id, sx, sy))
        return;
    if (picked.kind != PickTarget::Kind::Spawn) return;
    // Hailstone (a deliberate difference, game::opens_player_menu): another player's menu over them, where the
    // original inspects; with the switch off, context menus off or the player down, the original's right click.
    // The gamepad's Interact keeps the original's (right_click_spawn).
    if (player_menu_ && ui_api_) {
        const auto& world = conn_->world();
        auto it = world.spawns.find(picked.id);
        if (it != world.spawns.end() && game::opens_player_menu(it->second, picked.id == world.player_id) &&
            ui_api_->open_player_menu(picked.id, right_down_pos_.x, right_down_pos_.y)) {
            conn_->set_target(picked.id);
            return;
        }
    }
    right_click_spawn(picked.id, right_click_consider_);
}

bool ClientApp::right_click_spawn(std::uint32_t id, bool consider_npc) {
    return app::right_click_spawn(*conn_, id, consider_npc);
}

void ClientApp::auto_scribe(Clock::time_point now) {
    // Testing (--auto-scribe): what a player does with a new scroll, one step at a time: sit, pick the scroll up,
    // scribe it from the cursor into the first free book slot, memorize it into gem 1, wait until the gem is ready.
    if (scribe_step_ == 4 || now - scribe_at_ < std::chrono::milliseconds(1500)) return;
    const auto& spells = conn_->spell_state();
    const auto& inv = conn_->inventory();
    switch (scribe_step_) {
    case 0:
        for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
            if (const auto* it = inv.at(net::ItemSlot::possessions(s)); it && it->name.rfind("Spell: ", 0) == 0) {
                if (!conn_->sitting()) conn_->set_sitting(true);
                log::info("auto-scribe: picking up {} from general slot {}", it->name, s);
                conn_->click_slot(net::ItemSlot::possessions(s));
                scribe_step_ = 1;
                break;
            }
        if (scribe_step_ == 0) {
            log::warn("auto-scribe: no spell scroll in the bags");
            scribe_step_ = 4;
        }
        break;
    case 1:
        if (!inv.cursor()) break;
        log::info("auto-scribe: scribing {}", inv.cursor()->name);
        conn_->scribe_cursor(-1);
        scribe_step_ = 2;
        break;
    case 2:
        if (spells.scribing) break;
        for (int i = 0; i < game::kSpellBookSize; ++i)
            if (std::uint32_t id = spells.book[static_cast<std::size_t>(i)]; id != game::kNoSpell && id != 0) {
                log::info("auto-scribe: book slot {} holds spell {}; memorizing it into gem 1", i, id);
                conn_->memorize(i, 0);
                scribe_step_ = 3;
                break;
            }
        break;
    case 3:
        if (spells.memorizing || spells.memorize_sent_gem != -1 || spells.gems[0].spell == game::kNoSpell ||
            !spells.gem_castable(0, net::GameConnection::eq_time()))
            break;
        log::info("auto-scribe: gem 1 holds spell {} and is ready", spells.gems[0].spell);
        scribe_step_ = 4;
        break;
    default: break;
    }
    scribe_at_ = now;
}

void ClientApp::draw_items() {
    if (!item_ui_ || !conn_) return;
    // A bank window hiding closes every bag window (CBankWnd::OnShow @0x627760 -> CContainerMgr::CloseAllContainers
    // @0x65d4e0), carried bags too, each with its close wave. Checked every frame so a hide still closes them when
    // no key or click follows.
    prune_bag_windows(*conn_);
    if (opts_.show_items && player_ready_) {
        // Testing aid (--show-items).
        if (items_since_ == Clock::time_point{}) items_since_ = Clock::now();
        if (Clock::now() - items_since_ > std::chrono::seconds(4)) {
            const auto& inv = conn_->inventory();
            if (opts_.show_items & 1) {
                item_ui_->toggle_inventory();
                if (ui_api_) ui_api_->show("InventoryWindow", true);
                for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
                    if (const auto* it = inv.at(net::ItemSlot::possessions(s)); it && it->is_bag()) {
                        item_ui_->open_bag(*conn_, s);
                        if (ui_api_) ui_api_->right_click_item(net::ItemSlot::possessions(s));
                    }
            }
            if (opts_.show_items & 2) {
                for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
                    if (inv.at(net::ItemSlot::possessions(s))) {
                        conn_->click_slot(net::ItemSlot::possessions(s));
                        break;
                    }
            }
            if (opts_.show_items & 8) { // the first general-slot item onto the cursor, a stack whole (shift)
                for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
                    if (inv.at(net::ItemSlot::possessions(s))) {
                        conn_->click_slot(net::ItemSlot::possessions(s), true, false);
                        break;
                    }
            }
            if (opts_.show_items & 32) ui_host_.show_spell(200, false); // a spell display (testing)
            if ((opts_.show_items & 16) && ui_api_) { // the EQ menu, its Character submenu open (testing)
                ui_api_->click_child("EQMainWnd", "EQButton");
                ui_api_->cascade_hover(0, 2);
            }
            if (opts_.show_items & 4) {
                if (const auto* it = inv.at(net::ItemSlot::possessions(net::kSlotPrimary))) {
                    game::ItemLink link;
                    link.item_id = it->id;
                    item_ui_->link_clicked(*conn_, link);
                }
            }
            opts_.show_items = 0;
        }
    }
    item_ui_->draw(*conn_, stats_ui_ ? stats_ui_->character() : nullptr);
    if (game_view_.chat.clicked_link) {
        item_ui_->link_clicked(*conn_, *game_view_.chat.clicked_link);
        game_view_.chat.clicked_link.reset();
    }
    // (INVENTORY, I by default, goes through the key map: key_command.)
}


net::SlotClickKeys ClientApp::command_keys() const {
    if (forced_keys_) return *forced_keys_;
    const ImGuiIO& io = ImGui::GetIO();
    return {io.KeyShift, io.KeyCtrl, io.KeyAlt};
}

void ClientApp::run_with_keys(std::string_view line, net::SlotClickKeys keys) {
    while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
    if (!conn_ || line.empty()) return;
    const std::optional<net::SlotClickKeys> before = forced_keys_; // (nested: /shiftkey /nomodkey ...)
    const auto mods_before = ui_host_.forced_mods;
    forced_keys_ = keys;
    ui_host_.forced_mods = std::array<bool, 2>{keys.shift, keys.ctrl};
    conn_->send_command(std::string(line));
    forced_keys_ = before;
    ui_host_.forced_mods = mods_before;
}

// MQ's ItemNotify on Hailstone's own clicks: leftmouseup is the slot's left click (CInvSlot::HandleLButtonUp: pick up,
// put down, loot), rightmouseup its right click (use, read, open or close a bag), rightmouseheld the item's display.
// MQ clicks the slot's window, so a closed bag's slot works only through its fallbacks; here every slot works as if
// its window were open.
void ClientApp::notify_command(std::string_view args) {
    if (!conn_) return;
    const game::WindowNotify n = game::parse_window_notify(args);
    if (!n.error.empty()) {
        conn_->ui_message(n.error);
        return;
    }
    // What E3 presses runs from the client's state, UI or none (game/window_notify.h's maps): each with the call its
    // window's handler makes, and only while that window would show (the stock window's shown()'s checks). The
    // windows read and write the same state (the merchant's and the alternate currency's selection, the task and
    // respawn choice, the quantity dialog's slider, the confirmation's question), so the two agree.
    using A = game::NotifyAction;
    using L = game::NotifyList;
    auto& c = *conn_;
    if (n.notification == "leftmouseup")
        if (const A action = game::notify_action(n.window, n.child); action != A::None) {
            switch (action) {
            case A::TradeAccept: if (c.trade() && c.trade()->open) c.trade_accept(); break;
            case A::TradeCancel: if (c.trade() && c.trade()->open) c.trade_cancel(); break;
            case A::LootAll: if (c.loot_session() && c.loot_session()->open) c.loot_all(); break;
            case A::LootDone: if (c.loot_session() && c.loot_session()->open) c.end_loot(); break;
            // The box's question, whichever it is: the ground drop, the server's, a window's own, the guild invite.
            case A::ConfirmYes: answer_confirmation_question(ui_host_, true); break;
            case A::ConfirmNo: answer_confirmation_question(ui_host_, false); break;
            case A::LargeDialogYes: if (c.large_dialog()) c.answer_large_dialog(1); break;
            case A::LargeDialogNo: if (c.large_dialog()) c.answer_large_dialog(2); break;
            case A::TaskAccept: c.accept_selected_task(); break;
            case A::TaskDecline: if (c.tasks().offers) c.decline_tasks(); break;
            case A::BankAuto: if (c.bank()) c.auto_bank(); break;
            case A::BankDone: if (c.bank()) c.close_bank(); break;
            case A::TributeToggle: c.tribute_toggle(!c.tribute().settings.active); break;
            case A::RespawnSelect: c.choose_selected_respawn(); break;
            case A::RaidLock:
            case A::RaidUnlock: {
                // RAID_LockButton is enabled for the leader of an unlocked raid, RAID_UnlockButton of a locked one.
                const auto& r = c.raid();
                const bool lock = action == A::RaidLock;
                if (r.active && r.leader == c.character_name() && r.locked != lock) c.raid_lock(lock);
                break;
            }
            case A::MerchantBuy: merchant_buy_button(ui_host_); break;
            case A::MerchantSell: merchant_sell_button(ui_host_); break;
            case A::MerchantDone: if (c.merchant() && c.merchant()->open) c.close_merchant(); break;
            case A::QuantityAccept: quantity_accept(ui_host_); break;
            case A::AltCurrencyReclaim: alt_currency_reclaim_button(ui_host_); break;
            case A::AltCurrencyCreate: alt_currency_create_button(ui_host_); break;
            case A::Money0: case A::Money1: case A::Money2: case A::Money3:
                inventory_money_button(ui_host_, static_cast<int>(action) - static_cast<int>(A::Money0));
                break;
            case A::None: break;
            }
            return;
        }
    if (n.notification == "listselect")
        if (const L list = game::notify_list(n.window, n.child); list != L::None) {
            const int row = n.value - 1;
            switch (list) {
            case L::MerchantItems: merchant_select_row(ui_host_, row); break;
            case L::AltCurrencies: alt_currency_select_row(ui_host_, row); break;
            case L::TaskOffers: c.select_task_offer(row); break;
            case L::RespawnOptions:
                if (const auto& w = c.respawn_window(); w && row >= 0 && row < static_cast<int>(w->options.size()))
                    c.set_respawn_choice(w->options[static_cast<std::size_t>(row)].id);
                break;
            case L::None: break;
            }
            return;
        }
    if (n.notification == "newvalue" && game::notify_quantity_slider(n.window, n.child)) {
        quantity_set_slider(ui_host_, n.value);
        return;
    }
    // Anything else (a page's tab, a window E3 does not use): the stock window, as the player's click would.
    if (!ui_api_) {
        log::info("/notify: {} {} {}: needs the UI", n.window, n.child, n.notification);
        return;
    }
    bool done = false;
    // MQ's listselect sets the list's selection. Few stock lists take a selection of their own (list_select); the
    // rest select a row on its click, so that is the fallback. The row is the window's own (StockWindow::list), not
    // a UI pack's sorted or filtered view of it.
    if (n.notification == "listselect")
        done = ui_api_->select_list(n.window, n.child, n.value - 1, 0) || ui_api_->click_list(n.window, n.child, n.value - 1, 0);
    else if (n.notification == "tabselect") ui_api_->set_tab_page(n.window, n.child, n.value - 1), done = true;
    else if (n.notification != "newvalue")
        done = ui_api_->click_child(n.window, n.child, n.notification == "rightmouseup" ? "rightup" : "left");
    if (!done) log::info("/notify: {} {} {}: nothing took it", n.window, n.child, n.notification);
}

bool ClientApp::window_act(std::string_view window, std::string_view child, std::string_view action) {
    // A read that acts, held to what /notify needs: a script without 'actions' cannot click or close (a merchant's
    // or trade's DoClose ends it) by reading a member instead of sending the command.
    if ((plugin::caller_caps() & plugin::kCapActions) == 0) {
        if (conn_)
            conn_->ui_message(std::format("{}: the window method {} needs the server's 'actions' permission",
                                          plugin::caller_name(), action));
        return false;
    }
    // MQ's SendWndClick2 on a child: the click /notify would send (its game-state actions first, the UI after).
    if (action == "leftmouseup" || action == "rightmouseup") {
        if (child.empty() || !conn_) return false;
        notify_command(std::string(window) + " " + std::string(child) + " " + std::string(action));
        return true;
    }
    // CXWnd::Show(true / false): the stock window's own open state (UiApi's windows are state alone, built with or
    // without anything drawn), which Window[...].Open then reads.
    if (action == "doopen" || action == "doclose") return ui_api_ && ui_api_->show_window(window, action == "doopen");
    return false;
}

void ClientApp::itemnotify_command(std::string_view args) {
    if (!conn_) return;
    const game::ItemNotify n = game::parse_item_notify(args);
    if (!n.error.empty()) {
        conn_->ui_message(n.error);
        return;
    }
    const auto& inv = conn_->inventory();
    std::optional<net::ItemSlot> slot = n.slot;
    if (!slot) { // an item carried, by id or exact name: the first in slot order, bags' contents after their bag
        auto match = [&](const net::Item& it) {
            return n.item_id ? it.id == n.item_id : iequals(it.name, n.item_name);
        };
        for (const auto& [key, it] : inv.items()) {
            if (key.first != net::kInvPossessions) continue;
            if (match(it)) {
                slot = net::ItemSlot::possessions(key.second);
                break;
            }
            for (const auto& b : it.contents)
                if (match(b.item)) {
                    slot = net::ItemSlot::possessions(key.second, static_cast<std::int16_t>(b.index));
                    break;
                }
            if (slot) break;
        }
        if (!slot) {
            conn_->ui_message(n.item_id ? std::format("Item '#{}' not found.", n.item_id)
                                        : std::format("Item '{}' not found.", n.item_name));
            return;
        }
    }
    // A slot in a bag: the bag must be there, and the slot one it has (MQ's messages).
    if (slot->sub >= 0) {
        const net::Item* bag = inv.at(slot->parent());
        if (!bag || !bag->is_bag()) {
            conn_->ui_message(std::format("There was no container in slot {}", slot->slot));
            return;
        }
        if (slot->sub >= bag->bag_slots) {
            conn_->ui_message(std::format("{} is not a valid slot for this container.", slot->sub));
            return;
        }
    }
    const net::SlotClickKeys keys = command_keys();
    switch (n.notification) {
    case game::ItemNotification::LeftMouseUp: conn_->click_slot(*slot, keys.shift, keys.ctrl); break;
    case game::ItemNotification::RightMouseUp:
        if (ui_api_) ui_api_->right_click_item(*slot, keys);
        else conn_->right_click_item(*slot, keys);
        break;
    case game::ItemNotification::RightMouseHeld: {
        const net::Item* it = nullptr;
        if (slot->type == net::kInvCorpse) {
            const auto& loot = conn_->loot_session();
            if (loot && loot->open)
                if (auto f = loot->items.find(slot->slot); f != loot->items.end()) it = &f->second;
        } else {
            it = inv.at(*slot);
        }
        if (it && item_ui_) item_ui_->inspect(*it);
        break;
    }
    }
}

} // namespace hailstone::app::client_internal
