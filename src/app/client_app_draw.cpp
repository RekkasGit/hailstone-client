// ClientApp: Text overlay, nameplates and the Lua UI host glue.

#include "app/client_app_internal.h"

#include "game/item_preview.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

#include "mq/lua_mq.h"

namespace nmsc::app::client_internal {

void ClientApp::draw_text_overlay() {
    // CTextOverlay::Draw @0x41f610 (game/text_overlay.h): the line wrapped in {x0 + W/4, y0 + 0.33 H, x0 + 3W/4, y1},
    // centred (DrawWrappedText flags 0x10), in font 6 (the static at 0xab3448 passed to the text object's draw,
    // vtable +0xc): EQGraphicsDX9's font table @0x100b2100 makes it Arial with a cell height of 24, the same as
    // the UI's font 6 (ui::eq_font).
    if (!conn_) return;
    auto frame = conn_->text_overlay().frame(static_cast<std::uint32_t>(net::GameConnection::now_ms()));
    if (!frame) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float left = static_cast<float>(static_cast<int>(display.x * 0.25f));
    float top = static_cast<float>(static_cast<int>(display.y * 0.33f));
    float width = static_cast<float>(static_cast<int>(display.x * 0.5f));
    ImFont* font = ui::eq_font(6);
    if (!font) font = ImGui::GetFont();
    float size = font->FontSize;
    std::uint32_t c = frame->argb;
    ImU32 colour = IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, (c >> 24) & 0xff);
    // Wrapped into lines, each centred in the rectangle.
    const std::string& text = frame->text;
    const char* p = text.c_str();
    const char* end = p + text.size();
    float y = top;
    while (p < end) {
        const char* line_end = font->CalcWordWrapPositionA(size / font->FontSize, p, end, width);
        if (line_end == p) ++line_end;
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, line_end);
        dl->AddText(font, size, ImVec2(left + (width - ts.x) * 0.5f, y), colour, p, line_end);
        y += size;
        p = line_end;
        while (p < end && *p == ' ') ++p;
    }
}

void ClientApp::update_market_hidden(bool in_game) {
    market_hidden_.clear();
    if (!in_game || !conn_) return;
    for (const auto& [id, s] : conn_->world().spawns)
        if (conn_->market_hidden(s)) market_hidden_.insert(id);
}

void ClientApp::draw_nameplates(const glm::mat4& view_proj, VkExtent2D extent) {
    if (!conn_) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // The 3D view's pixels to its place in the window (/viewport).
    float sx = view_area_.w / static_cast<float>(extent.width), sy = view_area_.h / static_cast<float>(extent.height);
    dl->PushClipRect(ImVec2(view_area_.x, view_area_.y), ImVec2(view_area_.x + view_area_.w, view_area_.y + view_area_.h));
    const auto& world = conn_->world();
    // The leadership marks (game::name_sprite_with_marks).
    game::NameSpriteMarks marks;
    {
        const auto& l = conn_->leadership();
        marks.leadership = conn_->has_gates_of_discord();
        marks.npc_health = conn_->leadership_ability_active(1, 1) || conn_->leadership_ability_active(0x11, 1);
        marks.target_id = world.target_id;
        marks.assist_targets = l.assist_targets;
        marks.group_marks = l.group_marks;
        marks.raid_marks = l.raid_marks;
    }
    for (const auto& [id, s] : world.spawns) {
        if (id == world.player_id) continue;
        if (market_hidden_.contains(id)) continue; // IsInvisible: Hide Traders / Hide Buyers
        if (s.is_npc || s.is_corpse ? !npc_names_ : !pc_names_) continue; // ToggleNpcNameSprites / ToggleCharacterNameSprites
        // PlayerClient::SetNameSpriteState draws nothing without bDisplayNameSprite (the spawn's showname bit); body
        // type 11 has no name either (EQEmu's BodyType::NoTarget, "no name, can't target": the server's teleport
        // triggers, #TPTrigger*, are sent with showname set).
        if (!s.show_name || s.body_type == 11) continue;
        glm::vec3 base = s.display_valid ? s.display_position : s.position;
        // The head is straight above the spawn, so one farther across the ground than the names' range is out of it
        // whatever its height: passed over before its model is looked up (most of a busy zone, every frame).
        if (glm::distance(glm::vec2(base), glm::vec2(camera_.position)) > kNameplateRange) continue;
        // Just above the model's head: its rest-pose top at the actor's scale above the model origin (the
        // spawn's z, lowered for EQG models). Without a model: the eye (ViewHeight above the feet).
        float top = s.heights.view_height - s.heights.avatar_height;
        if (actor_renderer_) {
            if (auto model = actor_renderer_->model_for(s))
                top = model->bounds_max.z * s.heights.model_scale +
                      render::ActorRenderer::model_z_offset(*model, s.heights.model_scale, world.height_tables.get());
        }
        glm::vec3 head = base + glm::vec3(0, 0, top + 1.5f);
        if (glm::distance(head, camera_.position) > kNameplateRange) continue;
        glm::vec4 clip = view_proj * glm::vec4(head, 1.0f);
        if (clip.w <= 0.1f) continue;
        glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
        if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) continue;
        if (collision_ && !collision_->line_of_sight(camera_.position, head)) continue;
        float px = view_area_.x + (ndc.x * 0.5f + 0.5f) * static_cast<float>(extent.width) * sx;
        float py = view_area_.y + (0.5f - ndc.y * 0.5f) * static_cast<float>(extent.height) * sy;
        std::string label = game::display_name(s.name);
        // An NPC's last name is its owner line under the name (below); a player's follows the name.
        const bool npc = s.is_npc && !s.is_corpse;
        // PlayerClient::SetNameSpriteState @0x58e2d0 by __ShowNames (/shownames): a player's title and suffix above
        // level 3, the last name at 2, 3, 4 and 6, the guild at 3 and 4 (below); an NPC's name whatever the level.
        const bool player = !s.is_npc && !s.is_corpse;
        const int level = show_names_;
        if (!s.last_name.empty() && !npc && (!player || level == 2 || level == 3 || level == 4 || level == 6))
            label += " " + s.last_name;
        if (!s.title.empty() && (!player || level > 3)) label = s.title + " " + label;
        if (!s.suffix.empty() && (!player || level > 3)) label += " " + s.suffix;
        if (!player) label = game::name_sprite_with_marks(s, label, marks);
        ImVec2 size = ImGui::CalcTextSize(label.c_str());
        ImU32 color = s.is_corpse ? IM_COL32(170, 170, 170, 255) : s.is_npc ? IM_COL32(255, 220, 120, 255) : IM_COL32(140, 190, 255, 255);
        // Hailstone (option con_name_colour, a deliberate difference; docs/graphics.md "Name plates"): an NPC's name takes
        // its consider colour, as the target's always has. Off: the stock yellow.
        if (auto rgb = game::npc_name_colour(s, conn_->player_status().level, conn_->pvp_rules(), con_name_colour_))
            color = IM_COL32((*rgb >> 16) & 0xff, (*rgb >> 8) & 0xff, *rgb & 0xff, 255);
        // PlayerClient::SetNameSpriteTint: an NPC with IsMercenary (+0x208) gets (0x11, 0xb5, 0xe1).
        if (s.is_npc && !s.is_corpse && s.mercenary) color = IM_COL32(0x11, 0xb5, 0xe1, 255);
        if (id == world.target_id) {
            // The target: its consider colour on a dark plate edged in that colour, with a marker bobbing above it
            // (Hailstone's look, not the original's; its ring: draw_target_ring).
            std::uint32_t rgb = game::con_rgb(game::con_level(s, conn_->player_status().level, conn_->pvp_rules()));
            color = IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
            const float fs = ImGui::GetFontSize();
            const float padx = fs * 0.45f, pady = fs * 0.12f;
            ImVec2 a(px - size.x * 0.5f - padx, py - pady), b(px + size.x * 0.5f + padx, py + size.y + pady);
            dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150), fs * 0.35f);
            dl->AddRect(a, b, color, fs * 0.35f, 0, std::max(1.5f, fs * 0.1f));
            const float bob = std::abs(std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f)) * fs * 0.3f;
            const float tip = a.y - fs * 0.25f - bob, half = fs * 0.4f;
            ImVec2 t0(px - half, tip - half * 1.2f), t1(px + half, tip - half * 1.2f), t2(px, tip);
            dl->AddTriangleFilled(ImVec2(t0.x, t0.y + 1.5f), ImVec2(t1.x, t1.y + 1.5f), ImVec2(t2.x, t2.y + 1.5f),
                                  IM_COL32(0, 0, 0, 200));
            dl->AddTriangleFilled(t0, t1, t2, color);
        }
        dl->AddText(ImVec2(px - size.x * 0.5f + 1, py + 1), IM_COL32(0, 0, 0, 200), label.c_str());
        dl->AddText(ImVec2(px - size.x * 0.5f, py), color, label.c_str());
        // An NPC's owner line ("(Banker)", a pet's "(Soandso`s pet)" with PetOwnerNames), the name sprite's second
        // line (PlayerClient::SetNameSpriteState's "\n(owner)").
        if (std::string owner = game::npc_owner_line(s, conn_->client_option(game::ClientOption::PetOwnerNames));
            !owner.empty()) {
            ImVec2 os = ImGui::CalcTextSize(owner.c_str());
            float oy = py + size.y;
            dl->AddText(ImVec2(px - os.x * 0.5f + 1, oy + 1), IM_COL32(0, 0, 0, 200), owner.c_str());
            dl->AddText(ImVec2(px - os.x * 0.5f, oy), color, owner.c_str());
        }
        // The guild tag under a player's name ("<Guild Name>", the name sprite's second line).
        if (game::name_sprite_shows_guild(s, show_names_)) {
            std::string guild = conn_->guild().name_of(s.guild_id);
            if (!guild.empty()) {
                guild = "<" + guild + ">";
                ImVec2 gs = ImGui::CalcTextSize(guild.c_str());
                float gy = py + size.y;
                dl->AddText(ImVec2(px - gs.x * 0.5f + 1, gy + 1), IM_COL32(0, 0, 0, 200), guild.c_str());
                dl->AddText(ImVec2(px - gs.x * 0.5f, gy), color, guild.c_str());
            }
        }
    }
    dl->PopClipRect();
}

void ClientApp::register_eq_api(lua_State* L) { eq_api_->register_functions(L); }

// What the Lua windows' eq.* functions read this frame.
void ClientApp::sync_item_preview() {
    sync_item_preview(previews_[0], conn_ ? &conn_->item_preview() : nullptr);
    sync_item_preview(previews_[1], conn_ ? &conn_->marketplace().preview : nullptr);
}

void ClientApp::sync_item_preview(PreviewSlot& slot, const net::GameConnection::ItemPreview* ip) {
    if (!ip || !ip->open || !ip->show || !ip->item) {
        if (slot.view) slot.view->clear_subject();
        slot.serial = 0xffffffffu;
        return;
    }
    if (!slot.view) {
        if (!actor_library_) return;
        try {
            slot.view = std::make_unique<ItemPreviewView>(vk_, actor_library_, shader_dir_);
        } catch (const std::exception& e) {
            log::warn("item preview unavailable: {}", e.what());
            return;
        }
    }
    const game::Spawn* me = conn_->world().player();
    // FUN_00577700: on a copy of the player (FUN_005769a0) or the item's own model (FUN_00577440); built again for a
    // new reply or when the player's own look changed.
    if (ip->serial == slot.serial && (!ip->on_player || !me || me->appearance == slot.look)) return;
    slot.serial = ip->serial;
    if (ip->on_player && me && actor_renderer_) {
        slot.look = me->appearance;
        auto choice = actor_renderer_->choose_model(*me);
        if (!choice.model) {
            slot.view->clear_subject();
            return;
        }
        const net::Item* primary = conn_->inventory().at(net::ItemSlot::possessions(net::kSlotPrimary));
        const net::Item* secondary = conn_->inventory().at(net::ItemSlot::possessions(net::kSlotSecondary));
        game::Appearance look = game::preview_appearance(me->appearance, *ip->item, primary, secondary);
        auto mesh = slot.view->library().dress(*choice.model, look);
        slot.view->set_subject(choice.model, mesh, me->heights.model_scale, 0.0f);
    } else {
        slot.view->set_item_subject(game::item_model_number(ip->item->id_file));
    }
}

void ClientApp::update_eq_host() {
    EqHost& h = eq_host_;
    h.conn = conn_.get();
    if (!h.item_preview_image) {
        h.item_preview_image = [this](int w, int ht, int view) -> void* {
            auto& slot = previews_[view == 1 ? 1 : 0];
            return slot.view ? slot.view->image(w, ht) : nullptr;
        };
        h.item_preview_control = [this](std::string_view what, float x, float y, int view) {
            auto& slot = previews_[view == 1 ? 1 : 0];
            if (slot.view) slot.view->control(what, x, y);
        };
        // Send_OP_ItemPreviewRequest @0x6ffc20 by item id (the guild bank's Alt right click, sub_0067cb80).
        h.preview_item = [this](std::uint32_t item_id) {
            if (conn_) conn_->preview_item_id(item_id);
        };
        h.plugin_caps = [this](std::string_view name) { return automation_ ? automation_->plugin_caps(name) : 0u; };
        h.plugin_consent = [this]() -> std::optional<plugin::ConsentPrompt> {
            return automation_ ? automation_->plugin_consent() : std::nullopt;
        };
        h.answer_plugin_consent = [this](std::optional<bool> allow) {
            if (automation_) automation_->answer_plugin_consent(allow);
        };
    }
    h.web = web_service_.get();
    h.scripts = automation_ ? &automation_->scripts() : nullptr;
    h.registry = automation_ ? &automation_->registry() : nullptr;
    if (!h.tlo_context && automation_) h.tlo_context = [this]() -> const mq::Context& { return automation_->context(); };
    h.in_game = screen_ == Screen::InGame && player_ready_ && conn_;
    h.ui_ready = h.in_game || screen_ == Screen::Characters;
    h.game_state = h.in_game ? 5 : screen_ == Screen::Characters ? 1 : screen_ == Screen::Create ? 2 : 0;
    h.server_select = screen_ == Screen::Servers ? &server_view_ : nullptr;
    h.login = screen_ == Screen::Login ? &login_view_ : nullptr;
    h.char_select = screen_ == Screen::Characters ? &char_view_ : nullptr;
    h.char_create_view = screen_ == Screen::Create ? &create_view_ : nullptr;
    h.login_server = screen_ == Screen::Servers || screen_ == Screen::Characters ? server_profile_.name : std::string();
    // An action left from a screen the client has since left is not taken on its next showing.
    if (!h.login) h.login_action.reset();
    if (!h.char_select) h.char_select_action.reset();
    if (!h.char_create_view) h.char_create_action.reset();
    h.char_create_open = screen_ == Screen::Create;
    if (!h.char_create_submit) {
        h.char_create_submit = [this](const std::string& name, const net::CharCreate& cc) {
            if (!conn_) return;
            conn_->create_character(name, cc);
            creating_ = true;
            create_view_.busy = true;
            create_view_.name = name;
        };
        h.char_create_cancel = [this] { screen_ = Screen::Characters; };
    }
    h.last_server = screen_ == Screen::Servers
                        ? config_.get_string("login.last_world." + (server_profile_.id.empty() ? std::string("_test") : server_profile_.id), "")
                        : std::string();
    h.character = stats_ui_ ? stats_ui_->character() : nullptr;
    // The player customization window's model (app/lua_eq_customization.cpp).
    h.char_library = actor_renderer_ ? &actor_renderer_->library() : nullptr;
    h.player_model.reset();
    if (actor_renderer_ && h.in_game)
        if (const game::Spawn* me = conn_->world().player()) h.player_model = actor_renderer_->model_for(*me);
    h.hot_buttons = actions_ui_ ? &actions_ui_->hot_buttons() : nullptr;
    h.actions = actions_ui_.get();
    h.heading = game::yaw_to_heading(yaw_);
    // CCompassWnd::OnProcessFrame's heading error (game/compass.h), over the base Sense Heading skill.
    if (actions_ui_) {
        const auto* ch = h.in_game ? h.character : nullptr;
        actions_ui_->compass().process(static_cast<std::uint32_t>(net::GameConnection::now_ms()),
                                       h.in_game && eq_api_ && eq_api_->shown("CompassWindow"),
                                       ch ? ch->sheet().skills[game::mechanics::skill::kSenseHeading] : -1,
                                       [] { return static_cast<std::int32_t>(std::rand() & 0x7fff); });
    }
    h.air = movement_.air_supply();
    h.max_air = movement_.max_air_supply();
    h.zone_short_name = conn_ ? conn_->world().zone_short_name : std::string();
    h.zone_long_name = conn_ ? conn_->world().zone_long_name : std::string();
    h.game_hour = sky_hour_;
    h.feet_valid = h.in_game;
    for (int i = 0; i < 3; ++i) h.feet[i] = feet_[i];
    h.zone_box_valid = collision_ && collision_->bounds();
    if (h.zone_box_valid)
        for (int i = 0; i < 3; ++i) {
            h.zone_box_lo[i] = collision_->bounds()->first[i];
            h.zone_box_hi[i] = collision_->bounds()->second[i];
        }
    // Every frame: reload_server_files makes a new ItemUi, and the item display windows read these the next frame.
    h.item_displays = item_ui_ ? &item_ui_->item_displays() : nullptr;
    h.item_display_ids = item_ui_ ? &item_ui_->item_display_ids() : nullptr;
    if (!h.press_hot_button) {
        h.press_hot_button = [this](int bar, int button, bool mouse) {
            if (conn_ && actions_ui_) actions_ui_->hot_button(*conn_, stats_ui_ ? stats_ui_->character() : nullptr,
                                                              ability_view(), bar, button, mouse);
        };
        h.hot_button_menu = [this](int bar, int button) {
            if (conn_ && actions_ui_) actions_ui_->hot_button_right_click(*conn_, bar, button);
        };
        {
            // DoHotButton's reach outside the actions code (game/hot_button_press.h).
            HotButtonHooks hooks;
            auto screen = [](game::HotButtonWindow w) {
                return w == game::HotButtonWindow::Options ? "OptionsWindow"
                       : w == game::HotButtonWindow::Inventory ? "InventoryWindow" : "SpellBookWnd";
            };
            hooks.window_active = [this, screen](game::HotButtonWindow w) {
                if (w == game::HotButtonWindow::SpellBook && spell_ui_) return spell_ui_->book_open();
                return eq_api_ && eq_api_->shown(screen(w));
            };
            hooks.window_show = [this, screen](game::HotButtonWindow w, bool show) {
                if (w == game::HotButtonWindow::SpellBook) {
                    if (spell_ui_ && spell_ui_->book_open() != show) spell_ui_->toggle_book();
                } else if (eq_api_) {
                    eq_api_->show(screen(w), show);
                }
            };
            hooks.execute_cmd = [this](std::uint32_t command) { key_command({static_cast<int>(command), true}); };
            hooks.toggle_view = [this] { toggle_view(); };
            hooks.pet_button = [this](int button) { return eq_api_ && eq_api_->click_child("PetInfoWindow", "Pet" + std::to_string(button) + "_Button"); };
            hooks.use_item = [this](std::int32_t slot, std::int32_t bag) {
                if (eq_api_) eq_api_->right_click_item(net::ItemSlot::possessions(static_cast<std::int16_t>(slot), static_cast<std::int16_t>(bag)));
            };
            hooks.open_social_editor = [&h](std::int32_t social) {
                if (h.open_social_editor) h.open_social_editor(social);
            };
            hooks.show_aa_ability = [this](std::int32_t group) {
                if (eq_api_) eq_api_->activate_window("AAWindow", group);
            };
            hooks.spell_gem_right_click = [this](std::int32_t gem) {
                if (eq_api_) eq_api_->click_gem(gem, true);
            };
            // Lua socials (game/lua_social.h): their Lua runs in the script host with the modifiers held now.
            hooks.press_lua_social = [this](std::int32_t social, int bar, int button) {
                if (!automation_ || !actions_ui_) return;
                const game::Social& s = actions_ui_->hot_buttons().social(social);
                mq::ButtonPress p;
                p.social = social;
                p.name = s.name;
                p.lua = s.lua;
                p.bar = bar;
                p.button = button;
                const ImGuiIO& io = ImGui::GetIO();
                p.shift = io.KeyShift;
                p.ctrl = io.KeyCtrl;
                p.alt = io.KeyAlt;
                automation_->scripts().press_button(p);
            };
            hooks.stop_lua_social = [this](std::int32_t social) {
                return automation_ && automation_->scripts().stop_button(social);
            };
            hooks.edit_lua_social = [&h](std::int32_t social) { h.lua_social_edit = social; };
            hooks.forget_lua_social = [this](std::int32_t social) {
                if (automation_) automation_->scripts().forget_button(social);
            };
            actions_ui_->set_hooks(std::move(hooks));
        }
        h.hot_button_label = [this](int bar, int button) {
            if (!conn_ || !actions_ui_) return std::string();
            return actions_ui_->button_label(*conn_, actions_ui_->hot_buttons().current(bar, button));
        };
        h.set_hot_button = [this](int bar, int button, const game::HotButton& b) {
            if (actions_ui_) actions_ui_->set_hot_button(bar, button, b);
        };
        h.button_label = [this](const game::HotButton& b) {
            return conn_ && actions_ui_ ? actions_ui_->button_label(*conn_, b) : b.label;
        };
        h.toggle_spell_book = [this] {
            if (spell_ui_) spell_ui_->toggle_book();
        };
        h.run_button = [this](const game::HotButton& b) {
            if (conn_ && actions_ui_) actions_ui_->run_button(*conn_, stats_ui_ ? stats_ui_->character() : nullptr, ability_view(), b);
        };
        h.autoskill_held = [this] { return automation_ && automation_->autoskill_held(); };
        h.target_in_melee_range = [this] {
            const auto* ch = stats_ui_ ? stats_ui_->character() : nullptr;
            return conn_ && actions_ui_ && ch && actions_ui_->target_in_melee_range(*conn_, *ch, ability_view());
        };
        h.skill_name = [this](std::uint32_t skill) {
            if (!conn_ || !actions_ui_) return std::string();
            game::HotButton b;
            b.type = game::HotButtonType::Skill;
            b.slot = static_cast<std::int32_t>(skill);
            return actions_ui_->button_label(*conn_, b);
        };
        h.spell_book_open = [this] { return spell_ui_ && spell_ui_->book_open(); };
        h.drop_confirm = &drop_confirm_;
        h.game = &game_;
        h.chat = &game_view_.chat;
        h.submit_chat = [this](const std::string& line) {
            if (conn_) conn_->send_command(line);
        };
        h.click_link = [this](const game::ItemLink& link) {
            if (conn_ && item_ui_) item_ui_->link_clicked(*conn_, link);
        };
        h.inspect_item = [this](const net::Item& item) {
            if (item_ui_) item_ui_->inspect(item);
        };
        h.get_option = [this](std::string_view o) { return option_value(o).value_or(0); };
        h.find_option = [this](std::string_view o) { return option_value(o); };
        h.set_option = [this](std::string_view o, int v) { set_option_value(o, v); };
        h.option_choices = [this](std::string_view o) { return option_choices(o); };
        h.run_command = [this](int command) {
            if (conn_) key_command({command, true});
        };
        h.close_item_display = [this](int index) {
            if (item_ui_) item_ui_->close_item_display(index);
        };
    }
    wire_bug_reports(h);
}

// /luasocial [n | new]: opens the Lua social editor (the UI pack's window takes EqHost::lua_social_edit).
void ClientApp::lua_social_command(std::string_view args) {
    while (!args.empty() && args.front() == ' ') args.remove_prefix(1);
    auto say = [this](const std::string& text) {
        if (conn_) conn_->ui_message(text);
    };
    if (!actions_ui_) return;
    game::HotButtonStore& store = actions_ui_->hot_buttons();
    constexpr int kSocials = game::kSocialPages * game::kSocialButtons;
    int social = -1;
    if (args.empty() || args == "new") {
        for (int i = 0; i < kSocials && social < 0; ++i)
            if (store.social(i).empty()) social = i;
        if (social < 0) return say("[Lua] every social is in use");
    } else {
        int n = 0;
        auto [p, ec] = std::from_chars(args.data(), args.data() + args.size(), n);
        if (ec != std::errc() || n < 1 || n > kSocials) return say("[Lua] usage: /luasocial [1..120 | new]");
        social = n - 1;
    }
    eq_host_.lua_social_edit = social;
}

} // namespace nmsc::app::client_internal
