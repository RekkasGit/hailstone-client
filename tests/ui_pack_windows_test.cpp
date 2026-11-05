// The shipped UIs (ui/modern, ui/gamepad) run for real over Dear ImGui with no renderer: every script loads, and the
// windows each draws for itself (the ones that once came from the Classic UI among them) draw with the game asking
// for them, with no script error.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "core/byte_io.h"
#include "core/game_dir.h"
#include "net/aa_packets.h"
#include "net/game_connection.h"
#include "net/group_packets.h"
#include "net/merchant_packets.h"
#include "net/social_packets.h"
#include "net/spell_packets.h"
#include "ui/dev_screens.h"
#include "ui/lua/lua_host.h"
#include "mock_server/harness.h"
#include "scratch_dir.h"
#include "app/web_service.h"
#include "mq/lua_mq.h"
#include "plugin/policy.h"
#include "web/browser_host.h"

using namespace nmsc;
namespace fs = std::filesystem;

namespace {

struct Session {
    GameDir game;
    net::GameConnection conn;
    ui::ChatView view;
    app::EqHost host;
    app::EqApi api;
    ui::lua::LuaUi lua;
    Session(const fs::path& dir, const std::string& pack)
        : game(dir / "game"), conn(game), api(host), lua(dir / pack, dir / "ui.txt") {
        host.game = &game;
        host.conn = &conn;
        host.chat = &view;
        host.in_game = host.ui_ready = true;
        lua.add_eq_api([this](lua_State* L) {
            api.register_functions(L);
            lua_pushcfunction(L, [](lua_State* S) { lua_pushboolean(S, 1); return 1; });
            lua_setfield(L, -2, "InGame");
        });
        api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(1600, 900);
            io.DeltaTime = 1.0f / 60.0f;
            api.begin_frame();
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
    }
};

// Windows the packs redid from the Classic UI's (and a few more of theirs): shown one after another.
constexpr const char* kScreens[] = {
    "GroupSearchWnd", "GroupSearchFiltersWnd", "GroupMentorWindow", "FellowshipWnd", "SocialEditWnd",
    "IconSelectionWnd", "CombatAbilityWnd", "JournalNPCWnd", "JournalCatWnd", "HelpWindow", "StoryWnd",
    "AlertHistoryWnd", "FindLocationWnd", "ZonePathWnd", "TributeTrophyWnd",
    "LoadskinWnd", "LayoutCopyWindow", "VideoModesWindow", "NameChangeWnd", "NameChangePetWnd", "NameChangeMercWnd",
    "OptionsWindow", "AlarmWnd", "TipWindow", "NoteWindow", "EditLabelWnd", "BugReportWindow", "FeedbackWindow",
    "ClaimWnd", "ColorPickerWnd", "BodyTintWnd", "PlayerCustomizationWnd", "TitleWnd", "BreathWindow",
    "AggroMeterWnd", "PotionBeltWnd", "BandolierWnd", "BlockedBuffWnd", "BlockedPetBuffWnd", "InspectWnd",
    "LeadershipWindow", "LFGuildWnd", "MailAddressBookWindow", "MailIgnoreListWindow", "RaidOptionsWindow",
    "CombatSkillSelectWnd", "AdventureRequestWnd", "AdventureStatsWnd", "AdventureLeaderboardWnd", "DynamicZoneWnd",
    "PvPStatsWnd", "PvpLeaderboardWnd", "BazaarSearchWnd", "BarterSearchWnd", "TrainWindow", "TrackingWnd",
    "GuildBankWnd", "HotButtonWnd2", "HotButtonWnd3", "BrowserWnd", "RealEstatePurchaseWnd", "RealEstateManageWnd",
    "RealEstateItemsWnd", "RealEstateNeighborhoodWnd", "RealEstatePlotSearchWnd", "ScriptsWnd"};

} // namespace

TEST_CASE("UI packs: every script of the shipped UIs loads, and their windows draw without script errors") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        REQUIRE(fs::exists(src / "ui.ini"));
        nmsc::test::ScratchDir dir_scratch("ui_pack_windows_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        CHECK_FALSE(fs::exists(dir / "classic")); // nothing of the Classic UI is there to borrow

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            s.frames(3);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            lua_State* L = s.lua.state();
            for (const char* screen : kScreens) {
                INFO(screen);
                std::string show = std::string("eq.ShowWindow('") + screen + "', true)";
                REQUIRE(luaL_dostring(L, show.c_str()) == LUA_OK);
                s.frames(2);
                CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
                std::string hide = std::string("eq.ShowWindow('") + screen + "', false)";
                REQUIRE(luaL_dostring(L, hide.c_str()) == LUA_OK);
            }
        }
        ImGui::DestroyContext(ctx);
    }
}

TEST_CASE("UI packs: each draws the login, server select and character select screens in the client's place") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_pregame_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            s.host.in_game = s.host.ui_ready = false;
            REQUIRE(luaL_dostring(s.lua.state(), "eq.InGame = function() return false end") == LUA_OK);

            // The login screen: saved servers with accounts and characters (no keyring), then a new server, then a
            // test login's.
            ui::LoginView login;
            login.servers_dir = dir / "servers";
            login.ui_packs = {{"modern", "Modern", "", "mouse", false, {}}, {"gamepad", "Gamepad", "", "gamepad", false, {}}};
            login.ui = login.default_ui = pack;
            ServerProfile p;
            p.id = "alpha";
            p.name = "Alpha";
            p.host = "127.0.0.1";
            p.username = "first";
            p.accounts = {{"first", "Bob", "World One", {{"Bob", "World One", 12, "WAR"}}, false, false},
                          {"second", "", "", {{"Cid", "World Two", 50, "SHD/CLR"}}, false, false}};
            login.profiles = {p};
            ui::select_profile(login, 0);
            login.game_folders = {{"/games/rof2", false, true}};
            login.status = {"Could not connect.", true};
            login.version = "0.2.0-alpha (abc1234)"; // the small version line at the foot
            s.host.login = &login;
            // the modern pack's box set card open from the first frame (test.login = boxset)
            REQUIRE(luaL_dostring(s.lua.state(), "ui.SetSetting('test.login', 'boxset')") == LUA_OK);
            s.frames(3);
            CHECK(s.api.replaces("LoginScreen"));
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            if (std::string(pack) == "modern") CHECK(ImGui::FindWindowByName("###modern.pregame.login.boxset") != nullptr);
            // The updater's notice in each of its states (docs/updater.md).
            login.update.version = "9.0.0";
            login.update.current = "0.1.0-alpha";
            login.update.notes = "https://example.invalid/r";
            login.update.has_package = true;
            login.update.can_restart = true;
            for (const char* state : {"available", "downloading", "ready", "failed", ""}) {
                login.update.state = state;
                login.update.progress = 0.4f;
                login.update.error = "no network";
                login.update.message = *state ? "" : "The update was cut off, so 0.1.0-alpha was put back.";
                s.frames(2);
                CHECK_MESSAGE(ImGui::FindWindowByName("UI errors") == nullptr, state);
            }
            login.update = {};
            // The corner: this build, each of the updater's words on it, What's new and the GitHub page.
            login.release_notes = {"A thing that changed", "Another, long enough to wrap over more than one line of the "
                                                            "popup that shows the release notes"};
            login.project_page = "https://example.invalid/hailstone";
            // What's new open (test.whatsnew opens it once in the modern pack; the gamepad pack's page is below)
            REQUIRE(luaL_dostring(s.lua.state(), "ui.SetSetting('test.whatsnew', '1')") == LUA_OK);
            for (const char* check : {"off", "checking", "uptodate", "unknown", "newer"}) {
                login.update.check = check;
                login.update.off_reason = "a development build";
                s.frames(2);
                CHECK_MESSAGE(ImGui::FindWindowByName("UI errors") == nullptr, check);
            }
            if (std::string(pack) == "gamepad") {
                // What's new is a page of its own there (test.login = notes, as the screen appears again)
                REQUIRE(luaL_dostring(s.lua.state(), "ui.SetSetting('test.login', 'notes')") == LUA_OK);
                s.host.login = nullptr;
                s.frames(2);
                s.host.login = &login;
                s.frames(3);
                CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            }
            login.update = {};
            login.release_notes.clear();
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            // The add-on manager's panel (docs/addons.md): an install card, and rows of each group and state.
            login.addons.open = true;
            login.addons.available = true;
            login.addons.link = "https://github.com/owner/autoloot";
            login.addons.status = {"Looking at https://github.com/owner/autoloot...", false};
            login.addons.server = "Alpha";
            ui::AddonOfferView offer;
            offer.name = "autoloot";
            offer.kind = "plugin";
            offer.version = "1.3.0";
            offer.author = "Some One";
            offer.description = "Loots what you tell it to.";
            offer.source = "owner/autoloot";
            offer.page = "https://github.com/owner/autoloot";
            offer.key = "0123456789abcdef";
            offer.caps = {"read the game state", "draw windows"};
            offer.blocked = "an add-on named autoloot is installed already";
            login.addons.offer = offer;
            for (const char* state : {"uptodate", "available", "downloading", "ready", "approval", "modified",
                                      "blocked", "failed"}) {
                ui::AddonRowView r;
                r.name = std::string("addon-") + state;
                r.kind = "script";
                r.group = "installed";
                r.version = "1.0.0";
                r.source = "owner/" + r.name;
                r.page = "https://github.com/owner/" + r.name;
                r.state = state;
                r.state_text = std::string("State ") + state;
                r.caps = {"read the game state"};
                if (std::string(state) == "approval") r.new_caps = {"speak for you"};
                if (std::string(state) == "modified") r.modified = {"init.lua"};
                r.can_update = r.can_remove = true;
                r.auto_update = true;
                r.folder = "/x/scripts/" + r.name;
                login.addons.rows.push_back(r);
            }
            ui::AddonRowView manual;
            manual.name = "byhand";
            manual.kind = "plugin";
            manual.group = "manual";
            manual.note = "This server runs only its own plugins: it does not run here.";
            manual.folder = "/x/plugins/byhand";
            login.addons.rows.push_back(manual);
            ui::AddonRowView server = manual;
            server.name = "hud";
            server.group = "server";
            server.note = "Provided by Alpha: you allowed it, so it runs there.";
            server.consent = "allowed";
            login.addons.rows.push_back(server);
            server.name = "map";
            server.consent = "denied";
            login.addons.rows.push_back(server);
            // The store (docs/addons.md "Finding add-ons"): an item in each state, then the list being fetched, then
            // an empty one.
            for (const char* state : {"install", "update", "installed", "manual", "unavailable"}) {
                ui::AddonStoreItemView it;
                it.name = std::string("store-") + state;
                it.source = "owner/" + it.name;
                it.page = "https://github.com/" + it.source;
                it.kind = std::string(state) == "unavailable" ? "" : "plugin";
                it.version = "2.0.0";
                it.author = "Some One";
                it.description = "Does a thing for you, described in a sentence long enough to wrap in the card.";
                it.worlds = std::string(state) == "install" ? "Alpha World" : "";
                it.installed_version = std::string(state) == "update" || std::string(state) == "installed" ? "1.0.0" : "";
                it.state = state;
                it.problem = std::string(state) == "unavailable" ? "store-unavailable has no build for linux-x64" : "";
                login.addons.store.push_back(it);
            }
            login.addons.browsed = true;
            s.frames(3);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            if (std::string(pack) == "modern") {
                // the list of their own, then back to the store
                REQUIRE(luaL_dostring(s.lua.state(), "ui.SetSetting('test.addontab', 'mine')") == LUA_OK);
                s.frames(3);
                CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
                REQUIRE(luaL_dostring(s.lua.state(), "ui.SetSetting('test.addontab', 'store')") == LUA_OK);
            }
            login.addons.store.clear();
            login.addons.browsing = true;
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            login.addons.browsing = false;
            login.addons.store_error = "The list of add-ons could not be fetched.";
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            login.addons.offer.reset();
            login.addons.available = false;
            login.addons.why_off = "Add-ons cannot be fetched here.";
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            login.addons = {};
            // Suggested add-ons (docs/addons.md "Suggested add-ons"): the index's and a server's.
            login.addons.suggestions = {
                {"servplug", "owner/servplug", "Alpha World has a Hailstone add-on for its own features: servplug.", false},
                {"", "owner/extra", "Alpha suggests a Hailstone add-on: owner/extra.", true}};
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            login.addons = {};
            s.frames(2);
            ui::new_profile(login);
            s.frames(2);
            login.transient = true;
            login.busy = true;
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            s.host.login = nullptr;

            ui::ServerSelectView worlds;
            worlds.servers = {{"Open World", "Up", true, true}, {"Locked World", "Locked", false}}; // (a favourite: its star)
            worlds.selected = 0;
            s.host.server_select = &worlds;
            s.host.last_server = "Open World";
            s.host.login_server = "Alpha";
            s.frames(3);
            CHECK(s.api.replaces("serverselect"));
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            worlds.addon_hint = "Open World has a Hailstone add-on: go Back and open Add-ons to look at it.";
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            s.host.server_select = nullptr;

            // Character select, with characters and without; with the scene and without.
            ui::CharacterSelectView chars;
            chars.server_name = "World One";
            chars.characters = {{"Bob", 12, 1, {1}, "North Qeynos"}, {"Cid", 50, 2, {5, 2}, "Kaladim"}};
            chars.selected = 1;
            chars.scene = true;
            s.host.ui_ready = true;
            s.host.game_state = 1;
            s.host.char_select = &chars;
            s.frames(3);
            CHECK(s.api.replaces("CharacterListWnd"));
            chars.characters.clear();
            chars.selected = -1;
            chars.scene = false;
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            s.host.char_select = nullptr;

            // Character creation, with the scene and without, waiting on the world, and while customizing.
            ui::CharacterCreateView create;
            auto opt = [](std::uint32_t race, std::uint32_t cls, std::uint32_t deity, std::uint32_t zone) {
                ui::CreateOption o{race, cls, deity, zone, {}, {}};
                o.base = {75, 75, 75, 75, 75, 75, 75};
                o.points = {10, 0, 0, 10, 0, 0, 5};
                return o;
            };
            for (std::uint32_t race : {1u, 2u, 3u, 128u})
                for (std::uint32_t cls : {1u, 2u, 3u, 11u}) create.options.push_back(opt(race, cls, 201, 2));
            create.options.push_back(opt(1, 1, 202, 9));
            create.scene = true;
            s.host.game_state = 2;
            s.host.char_create_view = &create;
            s.frames(3);
            CHECK(s.api.replaces("CharacterCreateScreen"));
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            create.name = "Bobby";
            create.scene = false;
            create.status = {"That name is taken.", true};
            s.frames(2);
            create.busy = true;
            s.frames(2);
            s.host.customization_shown = true;
            s.frames(2);
            s.host.customization_shown = false;
            create.options.clear(); // no tables from the server
            create.option = -1;
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            s.host.char_create_view = nullptr;
        }
        ImGui::DestroyContext(ctx);
    }
}

TEST_CASE("UI packs: the marketplace draws its shelves, details and pages from a filled store") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_marketplace_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            auto& m = s.conn.marketplace();
            for (std::uint32_t i = 0; i < 6; ++i) {
                net::MarketplaceEntry e;
                e.id = 100 + i;
                e.name = "Item " + std::to_string(i);
                e.category = i < 3 ? "Gear/Head" : "Fun";
                e.price = 10 * (i + 1);
                e.description = "Line one<BR>marketid=101";
                e.is_new = i == 0;
                e.is_featured = i == 4;
                e.was_price = i == 2 ? 99 : 0;
                e.available_until = i == 1 ? 4102444800 : -1;
                e.item_of_week = i == 4;
                m.catalog.push_back(e);
            }
            m.window.populate(m.catalog, true);
            for (std::uint32_t i = 0; i < 6; ++i) m.window.apply_state(m.catalog, {100 + i, 0, i == 0 ? 1001u : 0u, 0, i == 3 ? "Maker" : ""});
            m.window.rebuild_tree(true);
            m.window.refilter(m.catalog);
            m.wallet = 25;
            m.open = true;
            lua_State* L = s.lua.state();
            // The landing page, the grid, the list, then the confirmation and result pages.
            for (int step = 0; step < 6; ++step) {
                INFO(step);
                if (step == 1) m.window.click_category(m.catalog, 1);
                if (step == 2) m.window.set_view(game::MarketplaceView::List);
                if (step == 3) m.window.page = game::MarketplacePage::TaxInfo;
                if (step == 4) m.window.page = game::MarketplacePage::Confirm;
                if (step == 5) {
                    m.window.page = game::MarketplacePage::Results;
                    m.result_text = "Done.";
                    m.window.preview_tab = true;
                }
                s.frames(2);
                CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            }
            REQUIRE(luaL_dostring(L, "local n = eq.ListInfo('MarketplaceWnd', 'MKPW_CategoriesTree') return n") == LUA_OK);
            CHECK(lua_tointeger(L, -1) >= 4);
            lua_settop(L, 0);
        }
        ImGui::DestroyContext(ctx);
    }
}

namespace {

// spells_us.txt lines: id and name (the rest 0).
std::shared_ptr<game::SpellTable> book_spells() {
    auto line = [](const char* id, const char* name) {
        std::vector<std::string> f(237, "0");
        f[0] = id;
        f[1] = name;
        std::string out;
        for (std::size_t i = 0; i < f.size(); ++i) out += (i ? "^" : "") + f[i];
        return out;
    };
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(line("200", "Minor Healing") + "\n" + line("201", "Courage") + "\n" + line("202", "Strike")) == 3);
    return table;
}

} // namespace

TEST_CASE("UI packs: the spell book's sets and set editor draw; eq.SaveSpellSet / EditSpellSet save, memorizing nothing") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_spellsets_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            // the book opens only in a zone (CSpellBookWnd::AboutToShow): the mock EQEmu's connection in place of
            // the session's own
            nmsc::test::Rig rig;
            REQUIRE(rig.to_zone());
            auto& conn = rig.conn;
            s.host.conn = &conn;
            conn.set_spell_table_for_test(book_spells());
            REQUIRE(conn.profile());
            net::PlayerProfile pp = *conn.profile();
            pp.class_ = 2; // a cleric: the book opens
            conn.set_profile_for_test(pp);
            for (std::uint32_t i = 0; i < 3; ++i)
                conn.receive_zone_packet_for_test(net::encode(net::MemorizeSpell{i, 200 + i, net::kMemScribe, 0}));
            REQUIRE(conn.spell_state().book[2] == 202);
            lua_State* L = s.lua.state();
            auto run = [&](const char* code) {
                INFO(code);
                REQUIRE(luaL_dostring(L, code) == LUA_OK);
            };
            auto number = [&](const char* code) -> lua_Integer {
                run(code);
                lua_Integer v = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : -100;
                lua_settop(L, 0);
                return v;
            };
            const auto& sets = conn.spell_loadouts();
            const auto gems_before = conn.spell_state().gems;
            // Saved from a table, not the gems: a spell not in the book (999) is kept; nil, -1, negatives and
            // non-numbers are empty gems.
            CHECK(number("return eq.SaveSpellSet('Heals', { 200, -1, 201, nil, 999 })") == 1);
            CHECK(sets.at(0).name == "Heals");
            CHECK(sets.at(0).spells[0] == 200);
            CHECK(sets.at(0).spells[1] == -1);
            CHECK(sets.at(0).spells[2] == 201);
            CHECK(sets.at(0).spells[3] == -1);
            CHECK(sets.at(0).spells[4] == 999);
            CHECK(number("return eq.SaveSpellSet('', { 200 }) or -1") == -1);
            CHECK(number("return eq.EditSpellSet(1, 'Alpha', { 'x', 202, -5 })") == 1);
            CHECK(sets.at(0).name == "Alpha");
            CHECK(sets.at(0).spells[0] == -1);
            CHECK(sets.at(0).spells[1] == 202);
            CHECK(sets.at(0).spells[2] == -1);
            CHECK(number("return eq.EditSpellSet(2, 'Beta', {}) or -1") == -1); // set 2 is not in use
            CHECK(number("return eq.SaveSpellSet('Pulls', { 202, 201 })") == 2);
            // nothing was memorized: no gem, no memorize, no set being memorized
            for (int g = 0; g < game::kSpellGems; ++g)
                CHECK(conn.spell_state().gems[static_cast<std::size_t>(g)].spell ==
                      gems_before[static_cast<std::size_t>(g)].spell);
            CHECK_FALSE(conn.spell_state().memorizing.has_value());
            CHECK_FALSE(conn.memorizing_set());
            run("local t = eq.SpellSets() assert(t[1].name == 'Alpha' and t[1].spells[2] == 202 and t[2].name == 'Pulls')");

            // The views: the list of sets, the editor of set 1, a new set, a new set from the gems, back.
            run("eq.ShowWindow('SpellBookWnd', true)");
            REQUIRE(conn.spell_book_open());
            s.frames(3);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            const bool modern = std::string(pack) == "modern";
            const char* views[] = {"sets", "1", "new", "gems", "2"};
            const char* gamepad_views[] = {"sets", "edit1", "editnew", "edit2", "book"};
            for (int v = 0; v < 5; ++v) {
                std::string code = modern ? std::string("ui.SetSetting('test.book.edit', '") + views[v] + "')"
                                          : std::string("ui.SetSetting('test.book.view', '") + gamepad_views[v] + "')";
                if (modern && v == 0) code = "ui.SetSetting('test.book.tab', 'sets')";
                INFO(code);
                run(code.c_str());
                s.frames(3);
                CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            }
            CHECK(ImGui::FindWindowByName(modern ? "###modern.SpellBookWnd" : "Spell Book") != nullptr);
            // all ten in use: a new name is refused, an existing one still saves
            for (int i = 0; i < 8; ++i) {
                std::string code = "return eq.SaveSpellSet('Set" + std::to_string(i) + "', { 200 })";
                CHECK(number(code.c_str()) > 0);
            }
            CHECK(number("return eq.SaveSpellSet('Eleventh', { 200 }) or -1") == -1);
            CHECK(number("return eq.SaveSpellSet('pulls', { 201 })") > 0);
            s.frames(3);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        }
        ImGui::DestroyContext(ctx);
    }
}

TEST_CASE("UI packs: the inventory's alternate currencies draw, and a row selects the stock list's row") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_currency_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            std::vector<net::AltCurrencyEntry> entries{{31, 22000, 1500, 1000, true}, {32, 22001, 1501, 500, true}};
            s.conn.receive_zone_packet_for_test(net::encode_alt_currency_populate(entries));
            s.conn.receive_zone_packet_for_test(net::encode_alt_currency_update({31, 7}));
            lua_State* L = s.lua.state();
            REQUIRE(luaL_dostring(L, "ui.SetSetting('test.inv.page', 'currency') eq.ShowWindow('InventoryWindow', true)") ==
                    LUA_OK);
            s.frames(3);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            // the page keeps the stock Display Missing off with nothing held (modern: Show all off)
            REQUIRE(luaL_dostring(L, "return (select(2, eq.ChildState('InventoryWindow', 'AltCurr_DisplayMissingButton')))") ==
                    LUA_OK);
            CHECK_FALSE(lua_toboolean(L, -1));
            lua_settop(L, 0);
            REQUIRE(luaL_dostring(L, "eq.ListClick('InventoryWindow', 'AltCurr_PointList', 0, 'left') "
                                     "return (eq.ChildState('InventoryWindow', 'AltCurr_CreateItemButton'))") == LUA_OK);
            CHECK(lua_toboolean(L, -1)); // 31, held: Claim is on
            lua_settop(L, 0);
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        }
        ImGui::DestroyContext(ctx);
    }
}

// The modern extended target window as a library (ui/modern/lib/xtarget.lua): a window of the same name that calls
// it with a row part gets its style and draw called for each filled slot, with the slot number, and draws cleanly.
TEST_CASE("UI packs (modern): lib/xtarget.lua takes a plugin's part on each extended target row") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_xtarget_lib_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    // The test's window, a plugin's (its own folder of windows), takes the pack's place.
    fs::create_directories(dir / "plug");
    std::ofstream(dir / "plug" / "xtarget_cover.lua") << R"lua(
local xtarget = dofile("lib/xtarget.lua")
drawn, styled = {}, {}
ui.window("Extended Target", function()
  xtarget.window({ width = 262, row = { width = 52,
    style = function(r) styled[#styled + 1] = r.n return r.n == 1 and { alpha = 0.45, strike = true, tag = { "IGNORED" } } or nil end,
    draw = function(r, x0, y0, x1, y1, s) drawn[#drawn + 1] = string.format("%d:%s:%d", r.n, r.name, math.floor(x1 - x0 + 0.5)) end,
  } })
end)
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        for (auto [slot, id, name] : {std::tuple{0u, 101u, std::string("a gnoll")}, std::tuple{1u, 102u, std::string("a pup")}}) {
            ByteWriter b;
            b.le<std::uint32_t>(5);
            b.le<std::uint32_t>(1);
            b.le<std::uint32_t>(slot);
            b.le<std::uint8_t>(1);
            b.le<std::uint32_t>(id);
            b.fixed_string(name, name.size() + 1);
            s.conn.receive_zone_packet_for_test(net::AppPacket(net::to_wire(net::Op::XTargetResponse), b.take()));
            game::Spawn& m = s.conn.world_for_test().spawns[id];
            m.id = id;
            m.name = name;
            m.is_npc = true;
            m.hp_percent = 80;
        }
        s.lua.set_folders({{"plug", dir / "plug"}});
        lua_State* L = s.lua.state();
        REQUIRE(luaL_dostring(L, "eq.ShowWindow('ExtendedTargetWnd', true)") == LUA_OK);
        s.frames(3);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        // (the plugin's globals are its own: run in them)
        REQUIRE(luaL_loadstring(L, "return table.concat(drawn, ','), #styled") == LUA_OK);
        s.lua.push_folder_env_for_test("plug");
        lua_setupvalue(L, -2, 1);
        REQUIRE(lua_pcall(L, 0, 2, 0) == LUA_OK);
        std::string out = lua_tostring(L, -2) ? lua_tostring(L, -2) : "";
        INFO(out);
        CHECK(out.find("0:a gnoll:") != std::string::npos);
        CHECK(out.find("1:a pup:") != std::string::npos);
        CHECK(out.find("0:a gnoll:52") != std::string::npos); // the part's width, at UI scale 1
        CHECK(out.find("2:") == std::string::npos);          // an empty slot gets no part
        CHECK(lua_tointeger(L, -1) >= 2);
        lua_settop(L, 0);
    }
    ImGui::DestroyContext(ctx);
}

// The modern AA window as a library (ui/modern/lib/aa.lua): a window of the same name that calls it with a row part,
// a details part and a banner gets each called, the row's with the ability's name, ranks and cost, and draws cleanly.
TEST_CASE("UI packs (modern): lib/aa.lua takes a script's part on each AA row, in the details and as a banner") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_aa_lib_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    std::ofstream(dir / "game" / "dbstr_us.txt") << "9001^1^Innate Strength^0^\n9002^1^Mystical Shielding^0^\n";
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    fs::create_directories(dir / "plug");
    std::ofstream(dir / "plug" / "aa_cover.lua") << R"lua(
local aa = dofile("lib/aa.lua")
drawn, styled, details, banners = {}, {}, 0, 0
ui.window("Alternate Advancement", function()
  aa.window({
    row = { width = 40,
      style = function(r) styled[#styled + 1] = r.name return { ring = { 1, 0.8, 0.2, 1 }, tag = { "AUTO" } } end,
      draw = function(r, x0, y0, x1, y1, s)
        drawn[#drawn + 1] = string.format("%s:%d/%d:%s:%d", r.name, r.rank, r.max, r.cost, math.floor(x1 - x0 + 0.5))
      end },
    details = function(sel, x, y, w, s) details = details + 1 return 20 * s end,
    banner = function(x, y, w, s) banners = banners + 1 return 24 * s end,
  })
end)
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        net::PlayerProfile pp;
        pp.class_ = 2;
        pp.level = 60;
        s.conn.set_profile_for_test(pp);
        for (auto [id, title, cost] : {std::tuple{100u, 9001, 2}, std::tuple{200u, 9002, 5}}) {
            game::AltAbility a;
            a.id = id;
            a.group = static_cast<std::int32_t>(id / 10);
            a.rank = 1;
            a.max_rank = 3;
            a.cost = cost;
            a.level_req = 10;
            a.classes = 1u << 2;
            a.type = 1;
            a.title_sid = title;
            a.prereq_groups = {0};
            a.prereq_ranks = {0};
            s.conn.receive_zone_packet_for_test(net::encode_send_aa_table(a));
        }
        s.lua.set_folders({{"plug", dir / "plug"}});
        lua_State* L = s.lua.state();
        REQUIRE(luaL_dostring(L, "eq.ShowWindow('AAWindow', true)") == LUA_OK);
        s.frames(3);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        REQUIRE(luaL_loadstring(L, "return table.concat(drawn, ','), #styled, banners") == LUA_OK);
        s.lua.push_folder_env_for_test("plug");
        lua_setupvalue(L, -2, 1);
        REQUIRE(lua_pcall(L, 0, 3, 0) == LUA_OK);
        std::string out = lua_tostring(L, -3) ? lua_tostring(L, -3) : "";
        INFO(out);
        CHECK(out.find("Innate Strength:0/3:2:40") != std::string::npos); // the part's width, at UI scale 1
        CHECK(out.find("Mystical Shielding:0/3:5:40") != std::string::npos);
        CHECK(lua_tointeger(L, -2) >= 2);
        CHECK(lua_tointeger(L, -1) >= 1);
        lua_settop(L, 0);
        // A selected ability brings the details part.
        REQUIRE(luaL_dostring(L, "eq.ListClick('AAWindow', 'List1', 0, 'left')") == LUA_OK);
        s.frames(2);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        REQUIRE(luaL_loadstring(L, "return details") == LUA_OK);
        s.lua.push_folder_env_for_test("plug");
        lua_setupvalue(L, -2, 1);
        REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
        CHECK(lua_tointeger(L, -1) >= 1);
        lua_settop(L, 0);
    }
    ImGui::DestroyContext(ctx);
}

// Every row the EQ menu can show (RoF2's CascadeMenu.txt, other skins' and later clients' names for the same
// windows, the rows this client adds) has a line icon of its own in both packs, not the fallback dot, and draws.
TEST_CASE("UI packs: every EQ menu row has a menu icon") {
    constexpr const char* kScript = R"lua(
local icons = dofile("lib/menuicons.lua")
local rows = {
  "Navigation", "Compass", "Find", "Map", "Atlas/Map",
  "Players", "Group Status", "Group Window", "Guild", "Raid", "Friends/Ignore", "Looking for Group",
  "Looking for Guild",
  "Actions", "Actions Window", "Combat Abilities", "Ability Book", "Target", "Extended Target", "Hotkeys",
  "Hotkeys 1", "Hotkeys 10", "Bandolier", "Alarm", "Alarm Window", "Aggro Meter",
  "Quests", "Expeditions", "Instance Information", "Quest Journal", "NPC Journal", "Storyline",
  "Character", "Player Information", "Player Info Window", "Pet", "Spell Gems", "Spell Book",
  "Alternate Advancement", "Buffs", "Short Duration Buffs", "Buffs (Short Duration)", "Leadership", "Skills",
  "Blocked Buffs", "Blocked Pet Buffs", "Auras",
  "Miscellaneous", "Bazaar Search", "Barter Search", "Tribute", "PvP Stats", "Mail", "Mercenaries",
  "Inventory", "Options", "Help", "Everquest Help", "Lua Scripts", "Web Browser", "Report a Bug", "Camp & Quit",
  "Marketplace", "Real Estate", "Manage", "Plot Search", "Real Estate Items", "Plugins",
}
local missing = {}
ui.Begin("menu icons test")
for _, t in ipairs(rows) do
  if icons.Name(t) == "dot" then missing[#missing + 1] = t end
  icons.Draw(t, 40, 40, 18, { 1, 1, 1 }, 1)
end
ui.End()
return table.concat(missing, ", ")
)lua";
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_menu_icons_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            s.frames(1);
            ImGui::GetIO().DisplaySize = ImVec2(1600, 900);
            s.api.begin_frame();
            ImGui::NewFrame();
            lua_State* L = s.lua.state();
            int rc = luaL_dostring(L, kScript);
            std::string out = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
            INFO(out);
            CHECK(rc == LUA_OK);
            CHECK(out.empty()); // (on success: the rows with no icon of their own)
            lua_settop(L, 0);
            ImGui::Render();
        }
        ImGui::DestroyContext(ctx);
    }
}

#ifdef NMSC_MOCK_WEB_HELPER
// The browser window in both packs with a browser behind it (the stand-in helper, tests/mock_web_helper.cpp, over
// real pipes): eq.Web* from the window's Lua, and the window drawing the tabs without script errors.
TEST_CASE("UI packs: the Lua Scripts window and eq.Scripts*: list, run, pause, stop, autorun") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_scripts_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::create_directories(dir / "scripts" / "other");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        std::ofstream(dir / "scripts" / "loop.lua", std::ios::binary) << "while true do require('mq').delay(10) end\n";
        std::ofstream(dir / "scripts" / "other" / "init.lua", std::ios::binary) << "print(1)\n";
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            plugin::Hooks hooks;
            plugin::Policy policy;
            mq::Registry reg;
            mq::Context mctx;
            mq::ScriptEnv se;
            se.hooks = &hooks;
            se.policy = &policy;
            se.registry = &reg;
            se.context = [&]() -> const mq::Context& { return mctx; };
            se.dirs = {dir / "scripts"};
            se.config_dir = dir / "scripts" / "config";
            mq::ScriptHost scripts(se);
            Session s(dir, pack);
            s.host.scripts = &scripts;
            lua_State* L = s.lua.state();
            auto run = [&](const char* code) {
                const bool ok = luaL_dostring(L, code) == LUA_OK;
                if (!ok) std::fprintf(stderr, "lua: %s: %s\n", code, lua_tostring(L, -1));
                REQUIRE(ok);
            };
            // Before the zone-in's rules: listed, not allowed, with the reason; no character's list yet.
            run("local l, info = eq.Scripts(true); assert(#l == 2 and l[1].name == 'loop' and l[2].name == 'other');"
                "assert(not l[1].allowed and l[1].why:find('zone%-in') and l[1].status == '' and l[1].found);"
                "assert(not info.autorun and info.folders[1]:find('scripts'));"
                "local ok, why = eq.ScriptRun('loop'); assert(not ok and why:find('may not run'))");
            run("eq.ShowWindow('ScriptsWnd', true)");
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            policy.set(plugin::Rules{}); // no hailstone.cfg: the player's scripts run
            run("local l = eq.Scripts(true); assert(l[1].allowed and l[1].caps ~= '')");
            run("assert(eq.ScriptRun('loop', 'a \\\"b c\\\"'))");
            CHECK(scripts.running("loop"));
            run("local l = eq.Scripts(); assert(l[1].status == 'running' and not l[1].kept)");
            s.frames(2);
            bool drawn = false; // the window is there (not only free of errors)
            for (ImGuiWindow* win : ImGui::GetCurrentContext()->Windows)
                drawn |= win->Active && std::string(win->Name).find("Lua Scripts") != std::string::npos;
            CHECK(drawn);
            run("assert(eq.ScriptPause('loop')); assert(eq.Scripts()[1].status == 'paused')");
            s.frames(2);
            run("assert(eq.ScriptStop('loop')); assert(not eq.ScriptStop('nope'))");
            CHECK_FALSE(scripts.running("loop"));
            // The character's autorun list: refused before a character, kept in its scripts.ini after.
            run("assert(not eq.SetScriptAutorun('other', true))");
            scripts.set_autorun_file(dir / "chars" / "scripts.ini");
            run("local _, info = eq.Scripts(); assert(info.autorun); assert(eq.SetScriptAutorun('other', true));"
                "local l = eq.Scripts(true); assert(l[2].autorun and not l[1].autorun)");
            CHECK(scripts.autorun("other"));
            CHECK(fs::exists(dir / "chars" / "scripts.ini"));
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            run("eq.ShowWindow('ScriptsWnd', false)");
            s.host.scripts = nullptr;
        }
        ImGui::DestroyContext(ctx);
    }
}

TEST_CASE("UI packs: the web browser window and eq.Web* with a browser") {
    for (const char* pack : {"modern", "gamepad"}) {
        INFO(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch("ui_pack_browser_test");
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            web::BrowserHost browser([] { return web::process_transport({NMSC_MOCK_WEB_HELPER}); });
            Session s(dir, pack);
            int wants_text = 0;
            app::WebService web({[&] { return &browser; }, [] { return nullptr; },
                                 [&] { return (dir / "web" / "server").string(); }, [] { return std::vector<std::string>{}; },
                                 [&] { ++wants_text; }});
            s.host.web = &web;
            lua_State* L = s.lua.state();
            auto run = [&](const char* code) {
                const bool ok = luaL_dostring(L, code) == LUA_OK;
                if (!ok) std::fprintf(stderr, "lua: %s: %s\n", code, lua_tostring(L, -1));
                REQUIRE(ok);
            };
            run("assert(eq.WebAvailable())");
            run("local id, why = eq.WebOpen('file:///etc/passwd'); assert(id == nil and why:find('not an http'))");
            run("TAB = eq.WebOpen('example.org', 64, 48); assert(TAB)");
            // A script's tab is not the player's: the window does not list it.
            web::TabOptions theirs;
            theirs.owner = "somescript";
            REQUIRE(browser.open("https://example.org/", theirs) != 0);
            run("assert(#eq.WebTabs() == 1 and eq.WebTabs()[1] == TAB)");
            run("eq.ShowWindow('BrowserWnd', true)");
            const web::Tab* t = browser.tab(1);
            REQUIRE(t);
            for (int i = 0; i < 2000 && t->title.empty(); ++i) {
                browser.poll(0);
                s.frames(1);
            }
            CHECK(t->title == "Mock https://example.org");
            CHECK(t->options.profile == (dir / "web" / "server").string());
            run("local st = eq.WebState(TAB); assert(st.url == 'https://example.org' and not st.closed)");
            run("local ok, why = eq.WebNavigate(TAB, 'file:///etc/passwd'); assert(not ok and why)");
            // Typed text without a scheme gets https:// in front: "javascript:..." is a (bad) web address, no script.
            run("assert(eq.WebNavigate(TAB, 'javascript:alert(1)'))");
            CHECK(t->url == "https://javascript:alert(1)");
            run("assert(eq.WebNavigate(TAB, 'https://example.org/b'))");
            run("eq.WebNav(TAB, 'reload'); eq.WebText(TAB, 'hi')");
            s.frames(3);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            CHECK(wants_text == 0); // the page was never clicked: it has not the keyboard
            run("eq.ShowWindow('BrowserWnd', false)");
            // The page's input (eq.WebView): a click reaches the page (the stand-in paints the pixel white) and gives
            // it the keyboard; a click elsewhere takes the keyboard back.
            run("ui.window('ViewTest', function() ui.SetNextWindowPos(0, 0); ui.Begin('ViewTest');"
                " eq.WebView(TAB, 64, 48); VX, VY = ui.GetItemRect(); ui.End() end)");
            s.frames(1);
            lua_getglobal(L, "VX");
            lua_getglobal(L, "VY");
            const float vx = static_cast<float>(lua_tonumber(L, -2)), vy = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 2);
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(vx + 10, vy + 5);
            s.frames(1);
            io.AddMouseButtonEvent(0, true);
            s.frames(1);
            io.AddMouseButtonEvent(0, false);
            s.frames(1);
            io.AddInputCharacter('z');
            s.frames(1);
            CHECK(wants_text > 0);
            // The clicked pixel (10, 5), once the page has the view's size (the browser window had it bigger).
            auto clicked = [&] {
                const std::size_t at = (5 * std::size_t{t->view.width} + 10) * 4;
                return t->view.width == 64 && t->view.bgra.size() > at && t->view.bgra[at] == std::byte{0xff};
            };
            for (int i = 0; i < 20000 && !clicked(); ++i) {
                browser.poll(0);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            CHECK(clicked());
            io.AddMousePosEvent(vx + 400, vy + 400);
            s.frames(1);
            io.AddMouseButtonEvent(0, true);
            s.frames(1);
            io.AddMouseButtonEvent(0, false);
            const int before = wants_text;
            s.frames(2);
            CHECK(wants_text == before); // the keyboard is back with the game
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            run("eq.WebClose(TAB); assert(eq.WebState(TAB) == nil)");

            // A plugin's window (its folder's ui/<pack>/): eq.Web* as "plugin:<name>", with the plugin's permissions.
            std::uint32_t plug_caps = plugin::kCapAll & ~plugin::kCapWeb;
            s.host.plugin_caps = [&](std::string_view name) { return name == "plugin:Plug" ? plug_caps : 0u; };
            fs::create_directories(dir / "plug");
            std::ofstream(dir / "plug" / "pw.lua")
                << "ui.window('PlugWeb', function()\n"
                   "  if PW_DONE then return end\n"
                   "  PW_DONE = true\n"
                   "  PW_TAB, PW_WHY = eq.WebOpen('https://example.org/')\n"
                   "  PW_PLAYER_TABS = #eq.WebTabs()\n"
                   "  PW_EXTERNAL = eq.WebOpenExternal('https://example.org/')\n"
                   "end)\n";
            s.lua.set_folders({{"plugin:Plug", dir / "plug"}});
            s.frames(2);
            auto run_plug = [&](const char* code) { // in the plugin's own globals
                bool ok = luaL_loadstring(L, code) == LUA_OK;
                if (ok) {
                    s.lua.push_folder_env_for_test("plugin:Plug");
                    lua_setupvalue(L, -2, 1);
                    ok = lua_pcall(L, 0, 0, 0) == LUA_OK;
                }
                if (!ok) std::fprintf(stderr, "lua: %s: %s\n", code, lua_tostring(L, -1));
                REQUIRE(ok);
            };
            run_plug("assert(PW_TAB == nil and PW_WHY:find('web'), tostring(PW_WHY))");
            run_plug("assert(PW_EXTERNAL == false)");
            CHECK(web.tabs("plugin:Plug").empty());
            plug_caps = plugin::kCapAll;
            run_plug("PW_DONE = nil");
            s.frames(2);
            run_plug("assert(PW_TAB ~= nil and PW_PLAYER_TABS == 1)");
            CHECK(web.tabs("plugin:Plug").size() == 1);
            CHECK(web.tabs("").empty()); // the pack's own window sees none of the plugin's tabs
            s.lua.set_folders({});
            browser.shutdown();
        }
        ImGui::DestroyContext(ctx);
    }
}
#endif
namespace {

// The most opaque vertex a window drew last frame (0..255), or -1 when the window is not there.
int max_alpha(const char* name) {
    ImGuiWindow* w = ImGui::FindWindowByName(name);
    if (!w || !w->WasActive) return -1;
    int out = 0;
    for (const ImDrawVert& v : w->DrawList->VtxBuffer) out = std::max(out, static_cast<int>((v.col >> IM_COL32_A_SHIFT) & 0xff));
    return out;
}

} // namespace

TEST_CASE("UI packs (modern): the Options window's Window Transparency and a window's Fades") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_fade_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        lua_State* L = s.lua.state();
        const char* player = "Player###modern.PlayerWindow";
        ImGui::GetIO().MousePos = ImVec2(-1000, -1000); // away from every window
        s.frames(3);
        REQUIRE(max_alpha(player) == 255);
        // Window Transparency: every window at most that opaque (eased over the fade's half second).
        REQUIRE(luaL_dostring(L, "eq.SetUiIni('Main', 'GlobalAlpha', '128')") == LUA_OK);
        s.frames(40);
        CHECK(max_alpha(player) <= 128);
        CHECK(max_alpha(player) >= 120);
        // Fades: after the delay (2 s) and the fade (0.5 s) with the mouse away, the Fade-to Transparency.
        REQUIRE(luaL_dostring(L, "eq.SetUiIni('Main', 'GlobalAlpha', '255') eq.SetUiIni('Main', 'GlobalFadeToAlpha', '64') "
                                 "ui.SetSetting('hud.fades.PlayerWindow', '1')") == LUA_OK);
        s.frames(60); // back to full first (the mouse counts as over at the start)
        CHECK(max_alpha(player) == 255); // within the delay
        s.frames(150);
        CHECK(max_alpha(player) <= 64);
        // The mouse back over it: full again after the fade.
        ImGuiWindow* win = ImGui::FindWindowByName(player);
        REQUIRE(win);
        ImGui::GetIO().MousePos = ImVec2(win->Pos.x + 5, win->Pos.y + 5);
        s.frames(40);
        CHECK(max_alpha(player) == 255);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("UI packs (modern): a resizable dialog's Scale zooms the whole window") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_dialog_scale_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        lua_State* L = s.lua.state();
        const char* guild = "Guild###modern.GuildManagementWnd";
        REQUIRE(luaL_dostring(L, "ui.SetSetting('hud.size.GuildManagementWnd', '600,450') "
                                 "eq.ShowWindow('GuildManagementWnd', true)") == LUA_OK);
        s.frames(3);
        ImGuiWindow* win = ImGui::FindWindowByName(guild);
        REQUIRE(win);
        const ImVec2 before = win->Size;
        CHECK(before.x == doctest::Approx(600).epsilon(0.02));
        REQUIRE(luaL_dostring(L, "ui.SetSetting('hud.scale.GuildManagementWnd', '1.5')") == LUA_OK);
        s.frames(3);
        CHECK(win->Size.x == doctest::Approx(before.x * 1.5f).epsilon(0.02));
        CHECK(win->Size.y == doctest::Approx(before.y * 1.5f).epsilon(0.02));
        // its size in design pixels is kept
        REQUIRE(luaL_dostring(L, "return ui.GetSetting('hud.size.GuildManagementWnd')") == LUA_OK);
        CHECK(std::string(lua_tostring(L, -1)) == "600,450");
        lua_settop(L, 0);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("UI packs (modern): an invitation to a group pops up with who sent it") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_group_invite_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        lua_State* L = s.lua.state();
        const char* popup = "Group Invite###modern.Group Invite";
        auto shown = [&] {
            ImGuiWindow* win = ImGui::FindWindowByName(popup);
            return win && win->WasActive;
        };
        s.frames(2);
        CHECK_FALSE(shown());
        net::GroupInvite inv;
        inv.invitee = "Tester";
        inv.inviter = "Bob";
        s.conn.receive_zone_packet_for_test(net::encode(inv));
        s.frames(2);
        CHECK(shown());
        CHECK(s.api.replaces("GroupInvite")); // the client's own prompt stays off: one popup, not two
        REQUIRE(luaL_dostring(L, "return (eq.WindowText('GroupWindow', 'Hailstone_Inviter'))") == LUA_OK);
        CHECK(std::string(lua_tostring(L, -1)) == "Bob");
        lua_settop(L, 0);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

// A server's compiled plugins ask before they load (docs/plugins.md): each pack draws the question in the client's
// place (eq.PluginConsent; "PluginConsent" replaced), and nothing while there is none.
TEST_CASE("UI packs: the server plugins' question draws in each pack in the client's place") {
    for (const char* pack : {"modern", "gamepad"}) {
        CAPTURE(pack);
        fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / pack;
        nmsc::test::ScratchDir dir_scratch(std::string("ui_pack_consent_") + pack);
        fs::path dir = dir_scratch.path();
        fs::create_directories(dir / "game");
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        {
            Session s(dir, pack);
            std::optional<plugin::ConsentPrompt> prompt;
            s.host.plugin_consent = [&] { return prompt; };
            s.host.answer_plugin_consent = [&](std::optional<bool>) { prompt.reset(); };
            auto drawn = [&] {
                for (ImGuiWindow* win : ImGui::GetCurrentContext()->Windows)
                    if (win->WasActive && (std::string(win->Name).find("###modern.PluginConsent") != std::string::npos ||
                                           std::string(win->Name) == "Server Plugins"))
                        return true;
                return false;
            };
            s.frames(2);
            CHECK(s.api.replaces("PluginConsent")); // the client's own prompt stays off
            CHECK_FALSE(drawn());
            plugin::ConsentPrompt p;
            p.server = "Alpha";
            p.serial = 1;
            p.files.push_back({"MQ2Thing", "MQ2Thing.so", 2u * 1024 * 1024, {}});
            p.files.push_back({"MQ2Other", "MQ2Other.so", 4096, {}});
            prompt = p;
            s.frames(3);
            CHECK(drawn());
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            prompt.reset();
            s.frames(2);
            CHECK_FALSE(drawn());
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        }
        ImGui::DestroyContext(ctx);
    }
}

// The gamepad pack has no popup of its own: the client's prompt stays, and its group window gives it the pad.
TEST_CASE("UI packs (gamepad): an invitation to a group keeps the client's own prompt") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "gamepad";
    nmsc::test::ScratchDir dir_scratch("ui_pack_group_invite_gamepad_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "gamepad", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "gamepad");
        net::GroupInvite inv;
        inv.invitee = "Tester";
        inv.inviter = "Bob";
        s.conn.receive_zone_packet_for_test(net::encode(inv));
        s.frames(2);
        CHECK_FALSE(s.api.replaces("GroupInvite"));
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

// Every dialog's bottom-right corner scales it, a resizable one's too (the AA window): the scale grows, its size
// in design pixels stays, and the window grows with it. Its right edge still changes its size.
TEST_CASE("UI packs (modern): a dialog's corner scales it, its edge resizes it") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_dialog_corner_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        lua_State* L = s.lua.state();
        auto setting = [&](const char* name) {
            REQUIRE(luaL_dostring(L, (std::string("return ui.GetSetting('") + name + "') or ''").c_str()) == LUA_OK);
            std::string v = lua_tostring(L, -1);
            lua_settop(L, 0);
            return v;
        };
        REQUIRE(luaL_dostring(L, "ui.SetSetting('hud.size.AAWindow', '600,450') ui.SetSetting('test.pos.AAWindow', "
                                 "'40,40') eq.ShowWindow('AAWindow', true)") == LUA_OK);
        s.frames(3);
        ImGuiWindow* win = ImGui::FindWindowByName("Alternate Advancement###modern.AAWindow");
        REQUIRE(win);
        const ImVec2 before = win->Size;
        ImGuiIO& io = ImGui::GetIO();
        auto drag = [&](ImVec2 from, float dx, float dy) {
            io.AddMousePosEvent(from.x, from.y);
            s.frames(1);
            io.AddMouseButtonEvent(0, true);
            s.frames(1);
            for (int i = 1; i <= 10; ++i) {
                io.AddMousePosEvent(from.x + dx * static_cast<float>(i) / 10, from.y + dy * static_cast<float>(i) / 10);
                s.frames(1);
            }
            io.AddMouseButtonEvent(0, false);
            s.frames(1);
            io.AddMousePosEvent(-1000, -1000);
            s.frames(3);
        };
        // the corner: a larger scale, the same design size, a larger window
        drag(ImVec2(win->Pos.x + win->Size.x - 4, win->Pos.y + win->Size.y - 4), 120, 90);
        const float scale = std::stof(setting("hud.scale.AAWindow"));
        CHECK(scale > 1.1f);
        CHECK(setting("hud.size.AAWindow") == "600,450");
        CHECK(win->Size.x == doctest::Approx(before.x * scale).epsilon(0.03));
        // the right edge: a wider design size, the same scale
        const ImVec2 scaled = win->Size;
        drag(ImVec2(win->Pos.x + win->Size.x - 2, win->Pos.y + win->Size.y / 2), 60, 0);
        CHECK(std::stof(setting("hud.scale.AAWindow")) == doctest::Approx(scale));
        CHECK(win->Size.x > scaled.x + 30);
        CHECK(win->Size.y == doctest::Approx(scaled.y).epsilon(0.01));
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

// The gamepad pack's shared pieces that every window calls each frame, against the way they worked before they were
// made cheaper: T.fit (a binary search; never half a UTF-8 character), G.wrapped (a line measured whole only near the
// width; maxLines lines drawn, the bottom of all), CH.testOpen (off for good once test.show is empty in the world).
TEST_CASE("UI packs (gamepad): T.fit, G.wrapped and CH.testOpen give what they gave") {
    constexpr const char* kScript = R"lua(
local T = dofile("lib/theme.lua")
local G = dofile("lib/stock.lua")
local CH = dofile("lib/child.lua")
local function check(ok, what) if not ok then error(what, 2) end end
ui.Begin("gamepad lib test")
-- T.fit: the old byte-by-byte search, for ASCII
local function oldFit(text, w)
  if ui.CalcTextSize(text) <= w then return text end
  local n = #text
  while n > 1 and ui.CalcTextSize(text:sub(1, n) .. "...") > w do n = n - 1 end
  return text:sub(1, n) .. "..."
end
for _, text in ipairs({ "", "a", "ab", "Lord Nagafen's Lair", "A very long item name, of the ages (Legendary)" }) do
  for w = -5, 400, 3 do
    check(T.fit(text, w) == oldFit(text, w), "ascii " .. text .. " @" .. w)
  end
end
-- T.fit, UTF-8: the longest start that ends a character and fits, never fewer than the first character
local function isCont(b) return b and b >= 0x80 and b < 0xC0 end
for _, text in ipairs({ "\u{C9}l\u{E9}onore d\u{2019}Ambr\u{E9}e", "\u{1F600}\u{1F600}x\u{E9}\u{E9}\u{E9}", "\u{E9}" }) do
  local first = 1
  while isCont(text:byte(first + 1)) do first = first + 1 end
  for w = -5, 300, 2 do
    local got = T.fit(text, w)
    local want = text
    if ui.CalcTextSize(text) > w then
      local best = first
      for n = first, #text do
        if not isCont(text:byte(n + 1)) and ui.CalcTextSize(text:sub(1, n) .. "...") <= w then best = n end
      end
      want = text:sub(1, best) .. "..."
    end
    check(got == want, "utf8 @" .. w .. ": " .. got .. " / " .. want)
  end
end
-- G.wrapped: the old wrap (every line measured whole), the lines drawn and the bottom
local function oldWrapped(text, x, y, width, maxLines, out)
  local _, lh = ui.CalcTextSize("Ag")
  local n = 0
  for para in (text .. "\n"):gmatch("(.-)\n") do
    local line = ""
    for word in para:gmatch("%S+") do
      local try = line == "" and word or (line .. " " .. word)
      if ui.CalcTextSize(try) > width and line ~= "" then
        if not maxLines or n < maxLines then out[#out + 1] = y .. ":" .. line end
        y, n, line = y + lh, n + 1, word
      else
        line = try
      end
    end
    if not maxLines or n < maxLines then out[#out + 1] = y .. ":" .. line end
    y, n = y + lh, n + 1
  end
  return y
end
local words = {}
for i = 1, 120 do words[#words + 1] = ({ "a", "gnoll", "pup", "the", "Commonlands", "of", "W", "iiii", "MMMM" })[i % 9 + 1] end
local long = table.concat(words, " ") .. "\n\nA second paragraph  with  spaces.\nSupercalifragilisticexpialidocious!"
local drawn
local draw = T.draw
T.draw = function(x, y, text) drawn[#drawn + 1] = y .. ":" .. text end
for _, maxLines in ipairs({ false, 1, 3 }) do
  for width = 20, 600, 7 do
    drawn = {}
    local want = {}
    local yNew = G.wrapped(long, 10, 100, width, nil, maxLines or nil)
    local yOld = oldWrapped(long, 10, 100, width, maxLines or nil, want)
    check(yNew == yOld, "bottom @" .. width)
    check(table.concat(drawn, "|") == table.concat(want, "|"), "lines @" .. width)
    if maxLines then check(#drawn <= maxLines, "maxLines @" .. width) end
  end
end
T.draw = draw
ui.End()
-- CH.testOpen: test.show empty in the world turns it off for good (as M.testShow and G.testShow); a list is read
GPSTATE.testOpenOff = nil
ui.SetSetting("test.show", "NoSuchWnd,OtherWnd")
CH.testOpen("OtherWnd")
check(not GPSTATE.testOpenOff and GPSTATE.testOpenList.names.OtherWnd and GPSTATE.testOpenList.names.NoSuchWnd,
  "list read")
ui.SetSetting("test.show", "")
CH.testOpen("OtherWnd")
check(GPSTATE.testOpenOff == true, "off when empty")
ui.SetSetting("test.show", "OtherWnd")
GPSTATE.testOpenList = nil
CH.testOpen("OtherWnd")
check(GPSTATE.testOpenList == nil, "stays off")
return "ok"
)lua";
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "gamepad";
    nmsc::test::ScratchDir dir_scratch("ui_pack_gamepad_lib_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "gamepad", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "gamepad");
        s.frames(1);
        ImGui::GetIO().DisplaySize = ImVec2(1600, 900);
        s.api.begin_frame();
        ImGui::NewFrame();
        lua_State* L = s.lua.state();
        int rc = luaL_dostring(L, kScript);
        std::string out = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        INFO(out);
        CHECK(rc == LUA_OK);
        CHECK(out == "ok");
        lua_settop(L, 0);
        ImGui::Render();
    }
    ImGui::DestroyContext(ctx);
}

// The gamepad windows keep what is costly to read or lay out (a raid's members, the raid's and the guild's message of
// the day, the /who answer, an item's wrapped description) between frames: a change still shows. The stock windows'
// texts and lists are stood in for from Lua, and every text the theme draws is recorded.
TEST_CASE("UI packs (gamepad): cached window contents follow a change") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "gamepad";
    nmsc::test::ScratchDir dir_scratch("ui_pack_gamepad_caches_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "gamepad", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "gamepad");
        s.frames(2);
        lua_State* L = s.lua.state();
        auto run = [&](const char* code) {
            INFO(code);
            REQUIRE(luaL_dostring(L, code) == LUA_OK);
        };
        auto saw = [&](const char* needle) {
            lua_getglobal(L, "saw");
            lua_pushstring(L, needle);
            REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
            bool found = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return found;
        };
        run(R"lua(
            seen = {}
            -- (every theme text, T.text, T.draw and T.textIn alike, ends in ui.DrawText(x, y, r, g, b, a, text))
            local DrawText = ui.DrawText
            ui.DrawText = function(...)
              seen[#seen + 1] = select(7, ...)
              return DrawText(...)
            end
            function saw(needle)
              for _, s in ipairs(seen) do
                if type(s) == "string" and s:find(needle, 1, true) then return true end
              end
              return false
            end
            fake = {
              raidMotd = "Raid message one", members = { { "1", "Alpha", "50", "Warrior", "", "Leader" } },
              guildMotd = "Guild message one", desc = "The first description",
              who = 1, players = { { name = "Echo", line = "[50 Warrior] Echo (Human) <Guild> ZONE: qeynos" } },
            }
            local ListInfo, ListRow, WindowText, WhoResults = eq.ListInfo, eq.ListRow, eq.WindowText, eq.WhoResults
            eq.ListInfo = function(win, child, ...)
              if win == "RaidWindow" then
                if child == "RAID_PlayerList" then return #fake.members, 6, -1 end
                return 0, 0, -1
              end
              if win == "GuildManagementWnd" and child == "MemberList" then return 0, 0, -1 end
              return ListInfo(win, child, ...)
            end
            eq.ListRow = function(win, child, row, ...)
              if win == "RaidWindow" then
                return child == "RAID_PlayerList" and fake.members[row + 1] or nil
              end
              return ListRow(win, child, row, ...)
            end
            eq.WindowText = function(win, child, ...)
              if win == "RaidWindow" and child == "MOTDViewer" then return fake.raidMotd end
              if win == "GuildManagementWnd" and child == "MOTDViewer" then return fake.guildMotd end
              if win == "GuildManagementWnd" and child == "GuildName" then return "The Guild" end
              if win == "ItemDisplayWindow" and child == "ItemDescription" then return fake.desc end
              return WindowText(win, child, ...)
            end
            eq.WhoResults = function() return fake.who, fake.players end
            -- (the stock guild and item windows show only in a guild / for an item: shown here on asking)
            fake.shown = {}
            local ReplaceWindow, WindowShown = eq.ReplaceWindow, eq.WindowShown
            eq.ReplaceWindow = function(win, ...)
              local r = ReplaceWindow(win, ...)
              if fake.shown[win] ~= nil then return fake.shown[win] end
              return r
            end
            eq.WindowShown = function(win)
              local r = WindowShown(win)
              if fake.shown[win] ~= nil then return fake.shown[win] end
              return r
            end
        )lua");

        // the raid: its message of the day at once, a new member at once (the row count), a renamed one within 0.25 s
        run("eq.ShowWindow('RaidWindow', true) seen = {}");
        s.frames(2);
        CHECK(saw("MOTD: Raid message one"));
        CHECK(saw("Alpha"));
        run("fake.raidMotd = 'Raid message two' "
            "fake.members[2] = { '2', 'Bravo', '40', 'Cleric', '', '' } seen = {}");
        s.frames(1);
        CHECK(saw("MOTD: Raid message two"));
        CHECK_FALSE(saw("Raid message one"));
        CHECK(saw("Bravo"));
        run("fake.members[2] = { '2', 'Charlie', '40', 'Cleric', '', '' }");
        s.frames(20);
        run("seen = {}");
        s.frames(1);
        CHECK(saw("Charlie"));
        CHECK_FALSE(saw("Bravo"));
        run("eq.ShowWindow('RaidWindow', false)");

        // the guild's message of the day
        run("fake.shown.GuildManagementWnd = true seen = {}");
        s.frames(2);
        CHECK(saw("Guild message one"));
        run("fake.guildMotd = '<b>Guild</b>   message two' seen = {}");
        s.frames(1);
        CHECK(saw("Guild message two"));
        CHECK_FALSE(saw("Guild message one"));
        run("fake.shown.GuildManagementWnd = false");

        // the /who answer: each row's line cut down to what follows the name; another reply within 0.25 s
        run("GPSTATE.whoOpen = true seen = {}");
        s.frames(2);
        CHECK(saw("Echo"));
        CHECK(saw("(Human) <Guild> ZONE: qeynos"));
        run("fake.who = 2 fake.players = { { name = 'Delta', line = '[60 Cleric] Delta (Erudite)' } }");
        s.frames(20);
        run("seen = {}");
        s.frames(1);
        CHECK(saw("Delta"));
        CHECK(saw("(Erudite)"));
        CHECK_FALSE(saw("Echo"));
        run("GPSTATE.whoOpen = false");

        // an item's description, wrapped once while it stays the same
        run("fake.shown.ItemDisplayWindow = true seen = {}");
        s.frames(2);
        CHECK(saw("The first description"));
        run("fake.desc = 'Another description' seen = {}");
        s.frames(1);
        CHECK(saw("Another description"));
        CHECK_FALSE(saw("The first description"));
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

// The modern item display reads the window's info lines and stat grid again only when the item changes (or every
// quarter second): another item shows at once, and what it draws is what the window holds.
TEST_CASE("UI packs (modern): an item display draws the shown item's stats, and another item at once") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_item_display_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        auto item = [](std::uint32_t id, const char* name, int str) {
            net::Item it;
            it.id = id;
            it.name = name;
            it.classes = 0xffff;
            it.races = 0xffff;
            it.equip_slots = 1u << 13;
            it.damage = 4;
            it.delay = 29;
            it.astr = static_cast<std::int8_t>(str);
            it.ac = str * 2;
            return it;
        };
        std::vector<net::Item> displays{item(1001, "Test Sword", 3)};
        std::vector<std::uint64_t> ids{1};
        s.host.item_displays = &displays;
        s.host.item_display_ids = &ids;
        lua_State* L = s.lua.state();
        // Every string drawn (DrawText, TextColored); then: is each stat value the window holds among them?
        REQUIRE(luaL_dostring(L, R"(
            test_seen = {}
            local dt, tc = ui.DrawText, ui.TextColored
            ui.DrawText = function(...) local a = { ... } test_seen[tostring(a[#a])] = true return dt(...) end
            ui.TextColored = function(...) local a = { ... } test_seen[tostring(a[5])] = true return tc(...) end
            function test_check()
              local n, missing = 0, {}
              for c = 1, 3 do
                for r = 1, 26 do
                  local st = eq.WindowText("ItemDisplayWindow", "IDW_Row" .. r .. "Col" .. c .. "Stat") or ""
                  local v = (eq.WindowText("ItemDisplayWindow", "IDW_Row" .. r .. "Col" .. c .. "Value") or "")
                    :match("^%s*(.-)%s*$")
                  if eq.ChildShown("ItemDisplayWindow", "IDW_Row" .. r .. "Col" .. c .. "Stat") ~= false
                      and st:match("%S") and v ~= "" then
                    n = n + 1
                    if not test_seen[v] then missing[#missing + 1] = v end
                  end
                end
              end
              return n, table.concat(missing, ",")
            end)") == LUA_OK);
        auto check = [&](const char* name) {
            REQUIRE(luaL_dostring(L, "test_seen = {}") == LUA_OK);
            s.frames(1);
            REQUIRE(luaL_dostring(L, "return test_check()") == LUA_OK);
            CHECK(lua_tointeger(L, -2) > 0);
            CHECK(std::string(lua_tostring(L, -1)) == "");
            lua_settop(L, 0);
            REQUIRE(luaL_dostring(L, (std::string("return test_seen['") + name + "'] == true").c_str()) == LUA_OK);
            CHECK(lua_toboolean(L, -1));
            lua_settop(L, 0);
        };
        s.frames(2);
        check("Test Sword");
        // another item in the same window: drawn the next frame
        displays[0] = item(1002, "Other Blade", 7);
        check("Other Blade");
        // one of the same name with other stats: drawn within the quarter second the parse is kept
        displays[0] = item(1003, "Other Blade", 11);
        s.frames(20);
        check("Other Blade");
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}

// The modern windows whose per-frame reading was cut down (the reads kept to what changed, the tables made once)
// draw without script errors when shown.
TEST_CASE("UI packs (modern): the inventory, loot, trade, raid, guild, merchant, map and their kin draw") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_data_windows_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        lua_State* L = s.lua.state();
        s.frames(2);
        for (const char* screen : {"InventoryWindow", "LootWnd", "TradeWnd", "RaidWindow", "GuildManagementWnd",
                                   "MerchantWnd", "MapViewWnd", "AuraWindow", "PotionBeltWnd", "Bag1",
                                   "MarketplaceWnd", "PointMerchantWnd", "MMTW_MerchantWnd", "MMGW_ManageWnd",
                                   "MIW_InfoWnd", "SpellDisplayWindow"}) {
            INFO(screen);
            REQUIRE(luaL_dostring(L, (std::string("eq.ShowWindow('") + screen + "', true)").c_str()) == LUA_OK);
            s.frames(2);
            CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
            REQUIRE(luaL_dostring(L, (std::string("eq.ShowWindow('") + screen + "', false)").c_str()) == LUA_OK);
        }
        // the dialogs a test setting draws though the game has not opened them (their opts.shown, set each frame)
        REQUIRE(luaL_dostring(L, "ui.SetSetting('test.draw.MarketplaceWnd', '1') ui.SetSetting('test.draw.ZonePathWnd', '1')"
                                 " ui.SetSetting('test.draw.PointMerchantWnd', '1')") == LUA_OK);
        s.frames(2);
        CHECK(ImGui::FindWindowByName("Zone Path###modern.ZonePathWnd") != nullptr);
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        REQUIRE(luaL_dostring(L, "ui.SetSetting('test.draw.MarketplaceWnd', '') ui.SetSetting('test.draw.ZonePathWnd', '')"
                                 " ui.SetSetting('test.draw.PointMerchantWnd', '')") == LUA_OK);
        s.frames(2);
        ImGuiWindow* zp = ImGui::FindWindowByName("Zone Path###modern.ZonePathWnd");
        CHECK((zp == nullptr || !zp->WasActive));
    }
    ImGui::DestroyContext(ctx);
}

// The raid window's members move between groups for the raid leader: a member dragged onto another group's card
// in an unlocked raid locks it first (the game moves members only in a locked raid, CRaidWnd::UpdateButtons) and is
// moved once the lock comes back; in a locked raid at once; a full group is refused. Each is the stock window's row
// click and group button, so what goes out is CRaid_SendMoveGroup's OP_RaidInvite {6, leader, member, group}.
TEST_CASE("UI packs (modern): the raid leader drags a member onto another group") {
    fs::path src = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern";
    nmsc::test::ScratchDir dir_scratch("ui_pack_raid_move_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir / "game");
    fs::copy(src, dir / "modern", fs::copy_options::recursive);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        Session s(dir, "modern");
        nmsc::test::Rig rig;
        REQUIRE(rig.to_zone());
        auto& conn = rig.conn;
        s.host.conn = &conn;
        const std::string me = conn.character_name();
        auto raid = [&](std::uint32_t action, const std::string& player, std::uint32_t group = 0) {
            net::RaidUpdate u;
            u.general.action = action;
            u.general.player_name = player;
            u.general.leader_name = me;
            u.general.parameter = group;
            u.class_ = 1;
            u.level = 50;
            conn.receive_zone_packet_for_test(net::encode_raid_update(u));
        };
        raid(net::kRaidCreate, me);
        raid(net::kRaidAddMember, me, 0);
        raid(net::kRaidAddMember, "Alpha", 0);
        raid(net::kRaidAddMember, "Bravo", 1);
        for (int i = 1; i <= 6; ++i) raid(net::kRaidAddMember, "Full" + std::to_string(i), 3); // group 4 is full
        REQUIRE(conn.raid().members.size() == 9);
        REQUIRE(conn.raid().leader == me);
        // OP_RaidInvite's raid commands so far: {action, the member (leader_name), parameter}
        auto commands = [&] {
            std::vector<std::tuple<std::uint32_t, std::string, std::uint32_t>> v;
            for (const auto& p : rig.server.zone_log) {
                if (p.opcode != net::to_wire(net::Op::RaidInvite)) continue;
                ByteReader r(p.data);
                std::uint32_t action = r.le<std::uint32_t>();
                r.fixed_string(64);
                r.skip(4);
                std::string member = r.fixed_string(64);
                v.emplace_back(action, member, r.le<std::uint32_t>());
            }
            return v;
        };
        auto count = [&](std::uint32_t action, const std::string& member, std::uint32_t parameter) {
            int n = 0;
            for (const auto& [a, m, p] : commands()) n += a == action && m == member && p == parameter;
            return n;
        };

        lua_State* L = s.lua.state();
        auto run = [&](const char* code) {
            INFO(code);
            REQUIRE(luaL_dostring(L, code) == LUA_OK);
        };
        // every text drawn, with where (T.Text ends in ui.DrawText(x, y, r, g, b, a, text))
        run(R"lua(
            drawn = {}
            local DrawText = ui.DrawText
            ui.DrawText = function(...)
              local x, y, text = select(1, ...), select(2, ...), select(7, ...)
              if type(text) == "string" then drawn[text] = { x, y } end
              return DrawText(...)
            end
            ui.SetSetting('hud.size.RaidWindow', '1200,640')
            ui.SetSetting('test.pos.RaidWindow', '20,20')
            eq.ShowWindow('RaidWindow', true)
        )lua");
        s.frames(4);
        REQUIRE(ImGui::FindWindowByName("UI errors") == nullptr);
        auto at = [&](const char* text) -> ImVec2 {
            lua_getglobal(L, "drawn");
            lua_getfield(L, -1, text);
            ImVec2 p(-1, -1);
            if (lua_istable(L, -1)) {
                lua_rawgeti(L, -1, 1);
                lua_rawgeti(L, -2, 2);
                p = ImVec2(static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1)));
                lua_pop(L, 2);
            }
            lua_settop(L, 0);
            return p;
        };
        ImGuiIO& io = ImGui::GetIO();
        auto drag = [&](ImVec2 from, ImVec2 to) {
            io.AddMousePosEvent(from.x, from.y);
            s.frames(1);
            io.AddMouseButtonEvent(0, true);
            s.frames(1);
            for (int i = 1; i <= 10; ++i) {
                io.AddMousePosEvent(from.x + (to.x - from.x) * static_cast<float>(i) / 10,
                                    from.y + (to.y - from.y) * static_cast<float>(i) / 10);
                s.frames(1);
            }
            io.AddMouseButtonEvent(0, false);
            s.frames(1);
            io.AddMousePosEvent(-1000, -1000);
            s.frames(2);
        };
        auto name = [&](const char* text) {
            ImVec2 p = at(text);
            REQUIRE(p.x >= 0);
            return ImVec2(p.x + 4, p.y + 4);
        };
        // (a point inside group g's card: under its header)
        auto card = [&](const char* header) {
            ImVec2 p = at(header);
            REQUIRE(p.x >= 0);
            return ImVec2(p.x + 20, p.y + 40);
        };

        // /raidwin (/raidwindow's prefix) asks the app for the raid window, which toggles it as the raid key does
        conn.send_command("/raidwin");
        CHECK(conn.take_window_toggle() == std::optional<std::string>("raid"));

        // unlocked: the drop locks the raid; nothing moves until the lock comes back
        CHECK(at("The game moves players only while the raid is locked. Dragging a player locks it for you.").x >= 0);
        drag(name("Alpha"), card("Group 3"));
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
        REQUIRE(rig.until([&] { return count(net::kRaidCmdLock, me, 0) == 1; }));
        CHECK(count(net::kRaidMoveGroup, "Alpha", 2) == 0);
        raid(net::kRaidLock, me);
        s.frames(2);
        REQUIRE(rig.until([&] { return count(net::kRaidMoveGroup, "Alpha", 2) == 1; }));
        CHECK(conn.raid().target == "Alpha");
        // EQEmu's answer (Raid::SendRaidMoveAll): the member removed and added again in its new group
        raid(net::kRaidRemoveMember, "Alpha");
        raid(net::kRaidAddMember, "Alpha", 2);
        s.frames(3);

        // locked: a drop moves at once, out of the groups too (parameter -1)
        drag(name("Bravo"), card("Group 5"));
        REQUIRE(rig.until([&] { return count(net::kRaidMoveGroup, "Bravo", 4) == 1; }));
        drag(name("Alpha"), card("Not in a group"));
        REQUIRE(rig.until([&] { return count(net::kRaidMoveGroup, "Alpha", 0xffffffffu) == 1; }));
        // a full group takes no one
        drag(name("Bravo"), card("Group 4"));
        rig.run_for(std::chrono::milliseconds(100));
        CHECK(count(net::kRaidMoveGroup, "Bravo", 3) == 0);
        CHECK(count(net::kRaidCmdLock, me, 0) == 1); // (locked once only)
        CHECK(ImGui::FindWindowByName("UI errors") == nullptr);
    }
    ImGui::DestroyContext(ctx);
}
