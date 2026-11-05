// The social editor: SocialEditWnd (CSocialEditWnd @0x7588a0) over the character's socials (game/hotbuttons.h).
// FUN_00758bb0 opens it on social n (page n / 12, button n % 12, colour 0), FUN_00758730 fills the name, the five
// lines and the colour from the social, CSocialEditWnd::WndNotification @0x758c10 takes the clicks: Accept
// (sub_00758470) copies the boxes into the social (name 15 characters, lines 255, the colour), clears its cooldown,
// saves the socials (SaveSocialsToIni) and closes; Clear (sub_007585e0) empties the name and the lines; a colour
// button SEW_TC0..19Button gives the name box its text colour and becomes the social's colour.
// CSocialEditWnd::GetSocialTextColor @0x758700: a colour button's text colour, 0xff606060 out of range.

#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lua.hpp>

#include "app/actions_ui.h"
#include "app/lua_eq_windows.h"
#include "game/hotbuttons.h"

namespace nmsc::app {

namespace {

// The colour buttons' text colours (EQUI_SocialEditWnd.xml SEW_TC0Button .. SEW_TC19Button).
constexpr std::array<std::uint32_t, 20> kSocialColors = {
    0xf0f0f0, 0xf0b400, 0x008000, 0xb46400, 0x000080, 0xf000f0, 0x808080, 0xe0e0e0, 0x828200, 0x606060,
    0x000000, 0x8c0000, 0xa0a0a0, 0xf00000, 0x00f000, 0xf0f000, 0x0000f0, 0x0064f0, 0x00f0f0, 0x800080,
};

// n of "SEW_TC<n>Button" / "SEW_Line<n>Input", or -1.
int number_between(std::string_view id, std::string_view prefix, std::string_view suffix) {
    if (id.size() <= prefix.size() + suffix.size() || !id.starts_with(prefix) || !id.ends_with(suffix)) return -1;
    auto mid = id.substr(prefix.size(), id.size() - prefix.size() - suffix.size());
    int v = -1;
    auto [p, ec] = std::from_chars(mid.data(), mid.data() + mid.size(), v);
    return ec == std::errc() && p == mid.data() + mid.size() ? v : -1;
}

class SocialEditWindow : public StockWindow {
public:
    SocialEditWindow() : StockWindow("SocialEditWnd", false) {}

    bool shown(EqHost& h) override { return open_ && h.in_game; }

    // FUN_00758bb0: the social to edit, then FUN_00758730 and Show(1, 1, 1).
    bool activate(EqHost& h, int social) override {
        if (!h.hot_buttons || social < 0 || social >= game::kSocialPages * game::kSocialButtons) return false;
        page_ = social / game::kSocialButtons;
        button_ = social % game::kSocialButtons;
        color_ = 0;
        load(*h.hot_buttons);
        open_ = true;
        return true;
    }

    void child(EqHost&, std::string_view id, Child& out) override {
        if (id == "SEW_NameInput") {
            out.text = name_;
            // The name box's CRNormal: the chosen colour button's (FUN_00758730 / WndNotification); unset until a
            // social's colour 0..19 or a button set it.
            if (name_color_) out.color = argb_to_rgb(*name_color_);
        } else if (int n = number_between(id, "SEW_Line", "Input"); n >= 0 && n < game::kSocialLines) {
            out.text = lines_[static_cast<std::size_t>(n)];
        }
    }

    bool set_text(EqHost&, std::string_view id, const std::string& text) override {
        if (id == "SEW_NameInput") {
            // The name box's limit: 15 characters (5 when the client mode global 0xddfd54 is 1..3 or 6, which this
            // client has no counterpart of).
            name_ = text.substr(0, 15);
        } else if (int n = number_between(id, "SEW_Line", "Input"); n >= 0 && n < game::kSocialLines) {
            lines_[static_cast<std::size_t>(n)] = text.substr(0, 255);
        } else {
            return false;
        }
        return true;
    }

    bool click(EqHost& h, std::string_view id, std::string_view button, bool) override {
        if (button != "left") return false;
        if (id == "SEW_Accept_Button") {
            accept(h);
        } else if (id == "SEW_Clear_Button") {
            name_.clear();
            for (auto& l : lines_) l.clear();
        } else if (int n = number_between(id, "SEW_TC", "Button"); n >= 0 && n < static_cast<int>(kSocialColors.size())) {
            name_color_ = kSocialColors[static_cast<std::size_t>(n)];
            color_ = n;
        } else {
            return false;
        }
        return true;
    }

private:
    int social() const { return page_ * game::kSocialButtons + button_; }

    // FUN_00758730.
    void load(const game::HotButtonStore& store) {
        const game::Social& s = store.social(social());
        name_ = s.name;
        lines_ = s.lines;
        color_ = s.color;
        name_color_.reset();
        if (color_ >= 0 && color_ < static_cast<int>(kSocialColors.size()))
            name_color_ = kSocialColors[static_cast<std::size_t>(color_)];
    }

    // sub_00758470.
    void accept(EqHost& h) {
        if (h.hot_buttons) {
            game::Social& s = h.hot_buttons->social(social());
            s.name = name_.substr(0, 15);
            for (std::size_t i = 0; i < lines_.size(); ++i) s.lines[i] = lines_[i].substr(0, 255);
            s.color = color_;
            s.cooldown_start = s.cooldown_length = 0;
            if (h.actions) h.actions->socials_changed(); // SaveSocialsToIni(1)
        }
        open_ = false; // Deactivate
    }

    int page_ = 0, button_ = 0, color_ = 0;
    std::string name_;
    std::array<std::string, game::kSocialLines> lines_;
    std::optional<std::uint32_t> name_color_;
};

EqHost* host_of(lua_State* L) { return static_cast<EqHost*>(lua_touserdata(L, lua_upvalueindex(1))); }

// eq.OpenSocialEditor(index): the social editor on social 0..119 (FUN_00758bb0).
int OpenSocialEditor(lua_State* L) {
    EqHost& h = *host_of(L);
    auto social = static_cast<std::int32_t>(luaL_checkinteger(L, 1));
    if (h.open_social_editor) h.open_social_editor(social);
    return 0;
}

} // namespace

Rgb social_text_color(int color) {
    if (color >= 0 && color < static_cast<int>(kSocialColors.size()))
        return argb_to_rgb(kSocialColors[static_cast<std::size_t>(color)]);
    return argb_to_rgb(0xff606060);
}

void add_socialedit_windows(std::vector<std::unique_ptr<StockWindow>>& out) {
    out.push_back(std::make_unique<SocialEditWindow>());
}

void register_socialedit_api(lua_State* L, EqHost& h) {
    static const luaL_Reg fns[] = {{"OpenSocialEditor", OpenSocialEditor}, {nullptr, nullptr}};
    lua_pushlightuserdata(L, &h);
    luaL_setfuncs(L, fns, 1);
}

} // namespace nmsc::app
