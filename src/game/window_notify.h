#pragma once

// /notify (MacroQuest's, which E3 uses to press a window's buttons): which window and child a line names, and the
// notification. A Hailstone addition: the original client has no such command. The app runs it
// (ClientApp::notify_command): with a UI loaded, on the stock window as a click would; without one, the buttons below
// run the connection's call the window's own handler makes.
//
//   /notify <window> <child> <notification> [value]
//   notifications: leftmouseup, rightmouseup (a click), listselect <row> (1-based), tabselect <page> (1-based)
//
// MQ finds a child by its XML item name or its ScreenID, in any case; the stock windows here know the ScreenID, in
// the XML's case. canonical_child() turns the item names and case E3 sends into it (from RoF2's EQUI_*.xml).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ascii_case.h"
#include "game/exchange.h"

namespace hailstone::game {

struct WindowNotify {
    std::string window, child;
    std::string notification; // lower case
    int value = 0;            // listselect's row, tabselect's page (1-based as sent)
    std::string error;        // empty when the line names a window, child and notification
};

// Pages MQ finds as windows: the stock window they are a page of.
inline std::string canonical_window(std::string_view w) {
    if (iequals(w, "TBW_PersonalPage") || iequals(w, "PersonalPage")) return "TributeBenefitWnd";
    if (iequals(w, "ActionsAbilitiesPage")) return "ActionsWindow";
    return std::string(w);
}

// A child's ScreenID, in the XML's case, for the item names and spellings scripts send; anything else unchanged.
inline std::string canonical_child(std::string_view c) {
    static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
        {"cd_textoutput", "TextOutput"},  {"CD_Yes_Button", "Yes_Button"}, {"CD_No_Button", "No_Button"},
        {"CD_OK_Button", "OK_Button"},    {"Yes_Button", "Yes_Button"},    {"No_Button", "No_Button"},
        {"OK_Button", "OK_Button"},       {"TSEL_AcceptButton", "AcceptButton"},
        {"TSEL_DeclineButton", "DeclineButton"}, {"TBWP_ActivateButton", "ActivateButton"},
        {"BIGB_AutoButton", "AutoButton"}, {"BIGB_DoneButton", "DoneButton"},
        {"IW_AltCurr_ReclaimButton", "AltCurr_ReclaimButton"},
        {"IW_AltCurr_CreateItemButton", "AltCurr_CreateItemButton"}, {"IW_AltCurr_PointList", "AltCurr_PointList"},
        {"RAID_UnlockButton", "RAID_UnlockButton"}, {"QTYW_Slider", "QTYW_Slider"},
        {"QTYW_Accept_Button", "QTYW_Accept_Button"}, {"QTYW_SliderInput", "QTYW_SliderInput"},
        {"LW_LootAllButton", "LootAllButton"}, {"LW_DoneButton", "DoneButton"}, {"LW_BroadcastButton", "BroadcastButton"},
    };
    for (const auto& [from, to] : kNames)
        if (iequals(c, from)) return std::string(to);
    return std::string(c);
}

inline WindowNotify parse_window_notify(std::string_view line) {
    WindowNotify out;
    const std::vector<std::string> w = exchange_words(line);
    if (w.size() < 3) {
        out.error = "Syntax: /notify <window|\"item\"> <control|0> <notification> [notification data]";
        return out;
    }
    out.window = canonical_window(w[0]);
    out.child = canonical_child(w[1]);
    for (char ch : w[2]) out.notification += ascii_lower(ch);
    if (out.notification == "listselect" || out.notification == "tabselect") {
        int n = 0;
        bool ok = w.size() >= 4 && !w[3].empty() && w[3].size() <= 6;
        for (char ch : ok ? std::string_view(w[3]) : std::string_view())
            if (ch < '0' || ch > '9') ok = false;
            else n = n * 10 + (ch - '0');
        if (!ok || n < 1) {
            out.error = "Invalid " + out.notification + " value";
            return out;
        }
        out.value = n;
    } else if (out.notification != "leftmouseup" && out.notification != "rightmouseup") {
        out.error = "Notification '" + w[2] + "' not supported";
    }
    return out;
}

// The connection's call a button's left click makes, for a client with no UI to click.
enum class NotifyAction : std::uint8_t {
    None, TradeAccept, TradeCancel, LootAll, LootDone, ConfirmYes, ConfirmNo, LargeDialogYes, LargeDialogNo,
    TaskDecline, BankAuto, BankDone, TributeToggle,
};

// After canonical_window and canonical_child.
inline NotifyAction notify_action(std::string_view window, std::string_view child) {
    auto is = [](std::string_view a, std::string_view b) { return iequals(a, b); };
    if (is(window, "TradeWnd")) {
        if (is(child, "TRDW_Trade_Button")) return NotifyAction::TradeAccept;
        if (is(child, "TRDW_Cancel_Button")) return NotifyAction::TradeCancel;
    } else if (is(window, "GiveWnd")) {
        if (is(child, "GVW_Give_Button")) return NotifyAction::TradeAccept;
        if (is(child, "GVW_Cancel_Button")) return NotifyAction::TradeCancel;
    } else if (is(window, "LootWnd")) {
        if (is(child, "LootAllButton")) return NotifyAction::LootAll;
        if (is(child, "DoneButton")) return NotifyAction::LootDone;
    } else if (is(window, "ConfirmationDialogBox")) {
        if (is(child, "Yes_Button")) return NotifyAction::ConfirmYes;
        if (is(child, "No_Button") || is(child, "OK_Button")) return NotifyAction::ConfirmNo;
    } else if (is(window, "LargeDialogWindow")) {
        if (is(child, "LDW_YesButton")) return NotifyAction::LargeDialogYes;
        if (is(child, "LDW_NoButton")) return NotifyAction::LargeDialogNo;
    } else if (is(window, "TaskSelectWnd")) {
        if (is(child, "DeclineButton")) return NotifyAction::TaskDecline;
    } else if (is(window, "BigBankWnd") || is(window, "BankWnd")) {
        if (is(child, "AutoButton")) return NotifyAction::BankAuto;
        if (is(child, "DoneButton")) return NotifyAction::BankDone;
    } else if (is(window, "TributeBenefitWnd")) {
        if (is(child, "ActivateButton")) return NotifyAction::TributeToggle;
    }
    return NotifyAction::None;
}

} // namespace hailstone::game
