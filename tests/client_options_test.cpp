// The Options window's game options (game/client_options.h): the defaults, eqclient.ini's keys, the anonymous and
// roleplay bits sharing one key, and the ranges.
#include <doctest/doctest.h>

#include <string>

#include "core/ini_file.h"
#include "game/client_options.h"

using namespace nmsc;
using game::ClientOption;

TEST_CASE("client options: table order, names and defaults") {
    for (std::size_t i = 0; i < game::kClientOptionCount; ++i) {
        const auto& o = game::kClientOptions[i];
        CHECK(static_cast<std::size_t>(o.id) == i);
        CHECK(game::find_client_option(o.name) == o.id);
        CHECK(o.def >= o.min);
        CHECK(o.def <= o.max);
    }
    CHECK_FALSE(game::find_client_option("no_such_option").has_value());
    game::ClientOptions opts;
    CHECK(opts.get(ClientOption::FastItemDestroy) == 0); // loadOptions: Destroy 0 (confirm)
    CHECK(opts.get(ClientOption::TradeWith) == 2);       // Trade 2: everyone
    CHECK(opts.on(ClientOption::LootAllConfirm));
    CHECK(opts.on(ClientOption::RaidInviteConfirm));
    CHECK_FALSE(opts.on(ClientOption::UseTellWindows));
    // COptionsWnd::sub_00700dc0 @0x700dc0: the mail boxes from the character ini's [MailOptions], defaults
    // forwarding 0, chat notification 0, audio notification 1, confirm deletion 1 (as MailMgr_Ctor's word +0xe0
    // = 0x100: chat 0, audio 1).
    CHECK_FALSE(opts.on(ClientOption::NewMailNotify));
    CHECK(opts.on(ClientOption::NewMailSound));
    CHECK(opts.on(ClientOption::AddressBookConfirmDelete));
    // CEverQuest::loadOptions: Show3dTargetIndicator and the buff / song display bytes default on.
    CHECK(opts.on(ClientOption::ShowTargetRing));
    for (auto id : {ClientOption::ShowBuffTimer, ClientOption::ShowBuffCounters, ClientOption::ShowBuffLimitUse,
                    ClientOption::ShowSongTimer, ClientOption::ShowSongCounters, ClientOption::ShowSongLimitUse})
        CHECK(opts.on(id));
}

TEST_CASE("client options: load and store eqclient.ini keys") {
    IniFile ini;
    ini.set_int("Options", "Destroy", 1);
    ini.set_int("Options", "Anonymous", 3);
    ini.set_int("Options", "NoSafeDrop", 5); // neither 0 nor 1: never
    ini.set_int("Defaults", "AllowAutoDuck", 0);
    ini.set_int("Main", "UseTellWindows", 1);
    game::ClientOptions opts;
    opts.load(ini);
    CHECK(opts.on(ClientOption::FastItemDestroy));
    CHECK(opts.on(ClientOption::Anonymous));
    CHECK(opts.on(ClientOption::Roleplaying));
    CHECK(opts.get(ClientOption::FastDrop) == -1);
    CHECK_FALSE(opts.on(ClientOption::AllowAutoDuck));
    CHECK(opts.on(ClientOption::UseTellWindows));

    opts.set(ClientOption::Anonymous, 0);
    IniFile out;
    opts.store(ClientOption::Anonymous, out);
    CHECK(out.get_int("Options", "Anonymous", -1) == 2); // the roleplay bit stays
    CHECK(opts.set(ClientOption::TradeWith, 9) == 2);    // clamped
    opts.store(ClientOption::TradeWith, out);
    CHECK(out.get_int("Options", "Trade", -1) == 2);
}

TEST_CASE("client options: the input options' defaults and ReadClientINIBool's reading") {
    game::ClientOptions opts;
    // CEverQuest::loadOptions @0x544be0: PetOwnerNames 1, LookSpring 0, MouseActive 1, MouseTurnZoom 1,
    // MouseRightHanded 1 (ReadClientINIBool), LeftClickTargetHelp / AllowAutoDuck / AllowAutoStand 1, ClickThroughMask
    // 0; CDisplay::InitNewUI: ContextMenus 1.
    CHECK(opts.on(ClientOption::PetOwnerNames));
    CHECK_FALSE(opts.on(ClientOption::LookSpring));
    CHECK(opts.on(ClientOption::MouseLook));
    CHECK(opts.on(ClientOption::MouseTurnZoom));
    CHECK(opts.on(ClientOption::MouseRightHanded));
    CHECK(opts.on(ClientOption::LeftClickTargetHelp));
    CHECK(opts.on(ClientOption::AllowAutoDuck));
    CHECK(opts.on(ClientOption::AllowAutoStand));
    CHECK(opts.on(ClientOption::ContextMenus));
    CHECK(opts.get(ClientOption::ClickThroughMask) == 0);

    IniFile ini;
    ini.set("Defaults", "LeftClickTargetHelp", "FALSE");
    ini.set("Defaults", "AllowAutoDuck", "false");
    ini.set("Defaults", "AllowAutoStand", "TRUE");
    ini.set("Options", "MouseRightHanded", "0");
    ini.set("Main", "ContextMenus", "");
    ini.set("Options", "ClickThroughMask", "9"); // atoi into the byte: kept whole
    opts.load(ini);
    CHECK_FALSE(opts.on(ClientOption::LeftClickTargetHelp));
    CHECK_FALSE(opts.on(ClientOption::AllowAutoDuck));
    CHECK(opts.on(ClientOption::AllowAutoStand));
    CHECK_FALSE(opts.on(ClientOption::MouseRightHanded));
    CHECK(opts.on(ClientOption::ContextMenus)); // empty: on
    CHECK(opts.get(ClientOption::ClickThroughMask) == 9);
}

TEST_CASE("client options: the context menus' test and the pet window's PopupOnPet") {
    CHECK(game::context_menus_open(true, false));
    CHECK_FALSE(game::context_menus_open(false, false));
    CHECK_FALSE(game::context_menus_open(true, true)); // dead or knocked out

    CHECK(game::pet_window_popup_setting(std::nullopt));
    CHECK(game::pet_window_popup_setting(std::string("TRUE")));
    CHECK_FALSE(game::pet_window_popup_setting(std::string("0")));
    CHECK_FALSE(game::pet_window_popup_setting(std::string("False")));

    bool had = false;
    CHECK_FALSE(game::pet_window_refresh(had, false, true)); // no change
    CHECK(game::pet_window_refresh(had, true, true));        // a pet came: show
    CHECK(had);
    CHECK_FALSE(game::pet_window_refresh(had, true, true));
    CHECK(game::pet_window_refresh(had, false, true));       // it went: hide
    CHECK_FALSE(game::pet_window_refresh(had, true, false)); // PopupOnPet off: the window stays as it is
    CHECK(had);
}
